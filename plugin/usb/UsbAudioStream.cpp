#include "UsbAudioStream.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "PcmConvert.h"

namespace t3k::usb {
namespace {

// URB-level statuses the kernel reports on reap (they are -errno).
constexpr int kErrConnReset = -104;  // after DISCARDURB: expected at stop()
constexpr int kErrShutdown = -108;   // device unplugged
constexpr int kErrNoDevice = -19;

// The explicit feedback endpoint keeps its own cadence: the AMS-22 reports
// every 4 microframes, so 8 packets (4 ms) is enough resolution for pacing
// regardless of the selected audio buffer period.
constexpr int kFeedbackPackets = 8;

bool isDeviceGone(int status) {
  return status == kErrShutdown || status == kErrNoDevice;
}

}  // namespace

UsbAudioStream::UsbAudioStream(const StreamConfig& config, const Options& options,
                               UrbTransport& transport, Callback& callback)
    : config_(config),
      options_(options),
      transport_(transport),
      callback_(callback),
      clock_(static_cast<double>(config.sampleRate)),
      framesPerPacket_(config.nominalFramesPerPacket(config.speed)),
      bytesPerFrame_(config.bytesPerFrame()),
      targetFill_(std::max<size_t>(options.ringFrames / 2, 1)),
      captureRing_(options.ringFrames, config.bytesPerFrame()),
      // Pacing may add a frame to the nominal URB (plan section 6.4), so the
      // callback buffers must hold one more frame than callbackFrames().
      inScratch_(static_cast<size_t>(callbackFrames() + 2) * config.channels, 0.0f),
      outScratch_(static_cast<size_t>(callbackFrames() + 2) * config.channels, 0.0f) {}

int UsbAudioStream::callbackFrames() const {
  return options_.packetsPerUrb * framesPerPacket_;
}

int UsbAudioStream::inFlightCount() const {
  int n = 0;
  for (const auto& u : urbs_) {
    if (u->inFlight) ++n;
  }
  return n;
}

void UsbAudioStream::prepareCaptureUrb(Urb& urb) {
  urb.isCapture = true;
  urb.status = 0;  // cleared here so a retried URB starts clean
  urb.endpoint = config_.captureEndpoint;
  urb.packetLengths.assign(options_.packetsPerUrb, 0);
  urb.packetActual.assign(options_.packetsPerUrb, 0);
  urb.packetStatus.assign(options_.packetsPerUrb, 0);
  // Capture packets tolerate +/-1 frame (plan section 6.3): ask for the max
  // and believe actual_length on reap.
  const uint32_t packetMax = static_cast<uint32_t>((framesPerPacket_ + 1) * bytesPerFrame_);
  for (uint32_t& len : urb.packetLengths) len = packetMax;
  urb.buffer.assign(packetMax * options_.packetsPerUrb, 0);
}

void UsbAudioStream::prepareFeedbackUrb(Urb& urb) {
  urb.isCapture = true;   // IN direction
  urb.status = 0;
  urb.isFeedback = true;
  urb.endpoint = config_.feedbackEndpoint;
  urb.packetLengths.assign(kFeedbackPackets,
                           std::max<uint8_t>(config_.feedbackBytes, 1));
  urb.packetActual.assign(kFeedbackPackets, 0);
  urb.packetStatus.assign(kFeedbackPackets, 0);
  urb.buffer.assign(static_cast<size_t>(urb.packetLengths[0]) * kFeedbackPackets,
                    0);
}

void UsbAudioStream::preparePlaybackUrb(Urb& urb) {
  urb.isCapture = false;
  urb.status = 0;
  urb.endpoint = config_.playbackEndpoint;
  urb.packetLengths.assign(options_.packetsPerUrb, 0);
  urb.packetActual.assign(options_.packetsPerUrb, 0);
  urb.packetStatus.assign(options_.packetsPerUrb, 0);

  // Pacing: the PI controller on the capture ring fill adjusts the URB total
  // by +/-1 frame, applied to the last packet (plan section 6.4). A
  // synchronous endpoint takes exactly the nominal count: its clock is the
  // USB frame clock, so a trimmed packet only drops or doubles a sample, and
  // the integrator can latch at +1 with the ring pinned at target (every
  // callback one frame short, 2026-10-28).
  const int nominalTotal = callbackFrames();
  const int total =
      config_.playbackSynchronous
          ? nominalTotal
          : std::max(1, clock_.playbackFramesPerPacket(
                            nominalTotal, captureRing_.readAvailable(), targetFill_));
  const int adjust = total - nominalTotal;

  size_t bytes = 0;
  for (int p = 0; p < options_.packetsPerUrb; ++p) {
    const int frames = std::max(0, framesPerPacket_ + (p == options_.packetsPerUrb - 1
                                                           ? adjust
                                                           : 0));
    urb.packetLengths[p] = static_cast<uint32_t>(frames * bytesPerFrame_);
    bytes += urb.packetLengths[p];
  }
  urb.buffer.assign(bytes, 0);

  // Run the DSP directly into the URB: capture ring in, wire bytes out. The
  // only intermediate copy in the whole path is the capture ring (D3).
  const int frames = static_cast<int>(bytes / static_cast<size_t>(bytesPerFrame_));
  wireScratch_.resize(static_cast<size_t>(frames) * bytesPerFrame_);
  // Output policy (plan M2.2): a short capture repeats the last frame rather
  // than handing the DSP a hole, and the underrun is counted.
  captureRing_.readOrRepeat(wireScratch_.data(), static_cast<size_t>(frames));
  playbackStats_.ringUnderruns = captureRing_.underruns();
  pcm::wireToFloat(wireScratch_.data(), inScratch_.data(), static_cast<size_t>(frames),
                   config_.channels, config_.subslotBytes);
  std::fill(outScratch_.begin(), outScratch_.end(), 0.0f);
  callback_.process(inScratch_.data(), outScratch_.data(), frames, config_.channels);
  pcm::floatToWire(outScratch_.data(), urb.buffer.data(), static_cast<size_t>(frames),
                   config_.channels, config_.subslotBytes);
}

void UsbAudioStream::handleCapture(Urb& urb) {
  ++captureStats_.urbsReaped;
  // One URB-level error (e.g. -ECONNRESET after DISCARDURB, or a protocol
  // error) counts as one xrun; the per-packet checks below would otherwise
  // report the same failure once per packet.
  const bool urbError = urb.status != 0;
  if (urbError) noteXrun(captureStats_);
  if (!urbError && lastCaptureStartFrame_ >= 0) {
    const int delta = urb.startFrame - lastCaptureStartFrame_;
    captureStats_.startFrameStep = delta;
    if (captureStartFrameDelta_ == 0) {
      captureStartFrameDelta_ = delta;
    } else if (delta != captureStartFrameDelta_) {
      noteXrun(captureStats_);  // the frame clock skipped or reordered
      if (delta > 0) captureStartFrameDelta_ = delta;  // re-anchor after a gap
    }
  }
  lastCaptureStartFrame_ = urb.startFrame;
  captureStats_.lastStartFrame = urb.startFrame;

  size_t offset = 0;
  int64_t framesInUrb = 0;
  const int capturePackets = static_cast<int>(urb.packetLengths.size());
  for (int p = 0; p < capturePackets; ++p) {
    ++captureStats_.packets;
    // Xrun check against the nominal packet with the documented +/-1 frame
    // tolerance (the allocation is nominal+1; actual_length decides).
    const uint32_t nominal = static_cast<uint32_t>(framesPerPacket_ * bytesPerFrame_);
    const uint32_t actual = urb.packetActual[p];
    const uint32_t tolerance = static_cast<uint32_t>(bytesPerFrame_);
    if (!urbError && (urb.packetStatus[p] != 0 || actual + tolerance < nominal ||
                      actual > nominal + tolerance)) {
      noteXrun(captureStats_);
    }
    if (actual > 0 && urb.packetStatus[p] == 0) {
      captureRing_.writeDropOldest(urb.buffer.data() + offset,
                                   actual / static_cast<size_t>(bytesPerFrame_));
      captureStats_.bytesTransferred += actual;
      framesInUrb += actual / bytesPerFrame_;
    }
    offset += urb.packetLengths[p];
  }
  // Input policy (plan M2.2): writeDropOldest always accepts; frames the
  // consumer never saw are counted as overruns.
  captureStats_.ringOverruns = captureRing_.overruns();

  // Drift: explicit feedback wins when present; otherwise observed capture
  // progress (plan section 6.4). Feedback stops arriving if the endpoint
  // stalls, so expire it and fall back rather than pacing on a stale value.
  if (options_.nowNs) {
    const uint64_t now = options_.nowNs();
    if (lastCaptureNs_ != 0 && now > lastCaptureNs_ && framesInUrb > 0) {
      clock_.addCaptureFrames(framesInUrb, now - lastCaptureNs_);
    }
    lastCaptureNs_ = now;
    if (clock_.hasFeedback() && lastFeedbackNs_ != 0 &&
        now > lastFeedbackNs_ && now - lastFeedbackNs_ > 2ull * 1000 * 1000 * 1000) {
      clock_.clearFeedback();
    }
  }
  updateDriftStats();
}

void UsbAudioStream::updateDriftStats() {
  const double ppm = clock_.driftPpm();
  captureStats_.driftPpm = ppm;
  playbackStats_.driftPpm = ppm;
  feedbackStats_.driftPpm = ppm;
}

void UsbAudioStream::handleFeedback(Urb& urb) {
  ++feedbackStats_.urbsReaped;
  const size_t fbBytes = std::max<uint8_t>(config_.feedbackBytes, 1);
  size_t offset = 0;
  const uint8_t* lastValid = nullptr;

  // Walk the URB's own packet count, not options_.packetsPerUrb: the
  // feedback URB carries kFeedbackPackets (its own 8-packet cadence) no
  // matter how long the audio URBs are. Reading it with the audio count
  // walked off the vectors and crashed parseFeedback on every buffer
  // above 1 ms (SIGSEGV at 96-frame blocks, 2026-10-19).
  const int packets = static_cast<int>(urb.packetLengths.size());
  for (int p = 0; p < packets; ++p) {
    ++feedbackStats_.packets;
    const uint32_t actual = urb.packetActual[p];
    if (urb.packetStatus[p] != 0 || actual < fbBytes) {
      ++feedbackStats_.feedbackErrors;
    } else {
      lastValid = urb.buffer.data() + offset;
      ++feedbackStats_.feedbackPackets;
    }
    offset += urb.packetLengths[p];
  }

  if (lastValid == nullptr) {
    updateDriftStats();
    return;  // a URB with no usable packet: keep the previous estimate
  }

  const double value = ClockModel::parseFeedback(lastValid, config_.feedback16_16);
  clock_.setFeedback(value, framesPerPacket_);
  feedbackStats_.feedbackValue = value;
  if (options_.nowNs) lastFeedbackNs_ = options_.nowNs();
  updateDriftStats();
}

void UsbAudioStream::handlePlayback(Urb& urb) {
  ++playbackStats_.urbsReaped;
  const bool urbError = urb.status != 0;
  if (urbError) noteXrun(playbackStats_);
  if (!urbError && lastPlaybackStartFrame_ >= 0) {
    const int delta = urb.startFrame - lastPlaybackStartFrame_;
    playbackStats_.startFrameStep = delta;
    if (playbackStartFrameDelta_ == 0) {
      playbackStartFrameDelta_ = delta;
    } else if (delta != playbackStartFrameDelta_) {
      noteXrun(playbackStats_);
      if (delta > 0) playbackStartFrameDelta_ = delta;
    }
  }
  lastPlaybackStartFrame_ = urb.startFrame;
  playbackStats_.lastStartFrame = urb.startFrame;

  for (int p = 0; p < static_cast<int>(urb.packetLengths.size()); ++p) {
    ++playbackStats_.packets;
    const uint32_t expected = urb.packetLengths[p];
    const uint32_t actual = urb.packetActual[p];
    if (!urbError && (urb.packetStatus[p] != 0 || actual != expected)) {
      noteXrun(playbackStats_);
    }
    playbackStats_.bytesTransferred += actual;
  }
}

bool UsbAudioStream::submitWithRetry(Urb& urb, StreamStats& stats) {
  const int attempts = std::max(options_.submitAttempts, 1);
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (transport_.submit(&urb)) return true;
    ++stats.submitFailures;
    // A dead device is not a transient failure: let the caller quiesce
    // rather than burn the retry budget on every remaining URB.
    if (transport_.deviceGone()) return false;
    if (attempt + 1 < attempts) {
      ++stats.submitRetries;
      if (options_.submitBackoffMicros > 0) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(options_.submitBackoffMicros));
      }
    }
  }
  return false;
}

bool UsbAudioStream::resubmit(Urb& urb) {
  if (!running_) return false;
  if (urb.isFeedback) {
    prepareFeedbackUrb(urb);
  } else if (urb.isCapture) {
    prepareCaptureUrb(urb);
  } else {
    preparePlaybackUrb(urb);
  }
  urb.inFlight = true;
  StreamStats& stats =
      urb.isFeedback ? feedbackStats_ : (urb.isCapture ? captureStats_ : playbackStats_);
  ++stats.urbsSubmitted;
  if (!submitWithRetry(urb, stats)) {
    urb.inFlight = false;
    if (urb.isFeedback) {
      // Pacing falls back to the capture-ring PI; audio must keep running.
      feedbackEnabled_ = false;
      ++feedbackStats_.feedbackErrors;
      clock_.clearFeedback();
      updateDriftStats();
      return false;
    }
    if (transport_.deviceGone()) {
      running_ = false;  // unplug: quiesce (plan pitfall 4)
      return false;
    }
    // Transient: keep the URB for the next pump instead of shrinking the
    // pool permanently. The gap is an xrun, not a teardown.
    noteXrun(stats);
    retryQueue_.push_back(&urb);
    return false;
  }
  return true;
}

bool UsbAudioStream::drainRetryQueue() {
  if (retryQueue_.empty()) return false;
  std::vector<Urb*> still;
  still.reserve(retryQueue_.size());
  bool submittedAny = false;
  for (Urb* urb : retryQueue_) {
    if (!running_) break;
    StreamStats& stats = urb->isFeedback
                             ? feedbackStats_
                             : (urb->isCapture ? captureStats_ : playbackStats_);
    urb->inFlight = true;
    ++stats.urbsSubmitted;
    if (submitWithRetry(*urb, stats)) {
      submittedAny = true;
      continue;
    }
    urb->inFlight = false;
    if (transport_.deviceGone()) {
      running_ = false;
      break;
    }
    if (urb->isFeedback) {
      feedbackEnabled_ = false;
      ++feedbackStats_.feedbackErrors;
      clock_.clearFeedback();
      updateDriftStats();
      continue;  // feedback is optional: drop it, keep the audio going
    }
    still.push_back(urb);
  }
  retryQueue_.swap(still);
  return submittedAny;
}

bool UsbAudioStream::start() {
  if (running_) return true;
  urbs_.clear();
  retryQueue_.clear();
  captureRing_.clear();
  lastCaptureStartFrame_ = -1;
  lastPlaybackStartFrame_ = -1;
  lastCaptureNs_ = 0;
  lastFeedbackNs_ = 0;
  clock_.clearFeedback();
  feedbackEnabled_ = config_.feedbackEndpoint != 0;

  const int n = std::max(options_.urbsInFlight, 1);
  for (int i = 0; i < n; ++i) {
    if (config_.captureEndpoint != 0) {
      urbs_.push_back(std::make_unique<Urb>());
      prepareCaptureUrb(*urbs_.back());
    }
    if (config_.playbackEndpoint != 0) {
      urbs_.push_back(std::make_unique<Urb>());
      preparePlaybackUrb(*urbs_.back());
    }
    if (feedbackEnabled_) {
      urbs_.push_back(std::make_unique<Urb>());
      prepareFeedbackUrb(*urbs_.back());
    }
  }
  running_ = true;
  for (auto& u : urbs_) {
    u->inFlight = true;
    StreamStats& stats =
        u->isFeedback ? feedbackStats_ : (u->isCapture ? captureStats_ : playbackStats_);
    ++stats.urbsSubmitted;
    if (!submitWithRetry(*u, stats)) {
      u->inFlight = false;
      if (u->isFeedback) {
        // Feedback is an optimization: keep the audio stream alive and let
        // the capture-ring PI carry pacing.
        feedbackEnabled_ = false;
        ++feedbackStats_.feedbackErrors;
        continue;
      }
      running_ = false;
      // Discard/reap whatever already went to the kernel before the Urb
      // objects are freed (the caller resets the stream on failure).
      stop();
      return false;
    }
  }
  return true;
}

void UsbAudioStream::stop() {
  if (!running_ && inFlightCount() == 0 && retryQueue_.empty()) return;
  running_ = false;
  // Deferred URBs never reached the kernel; dropping them is enough.
  retryQueue_.clear();
  for (auto& u : urbs_) {
    if (u->inFlight) transport_.discard(u.get());
  }
  // The kernel reaps discarded URBs with -ECONNRESET; collect them so no URB
  // object is freed while the driver still holds it (plan pitfall 4).
  for (int tries = 0; inFlightCount() > 0 && tries < 64; ++tries) {
    Urb* urb = transport_.reap(50);
    if (urb == nullptr) continue;
    urb->inFlight = false;
  }
}

int UsbAudioStream::discardOneInFlight() {
  for (auto& u : urbs_) {
    if (u->inFlight) {
      transport_.discard(u.get());
      return 1;
    }
  }
  return 0;
}

bool UsbAudioStream::pumpOnce(int timeoutMs) {
  // A transient submit failure leaves the URB out of flight; retry it before
  // blocking in reap, or a persistent-but-recovering condition would spin
  // the pool down to nothing.
  if (running_) drainRetryQueue();
  if (!running_) return false;

  Urb* urb = transport_.reap(timeoutMs);
  if (urb == nullptr) {
    if (transport_.deviceGone()) {
      running_ = false;  // unplug during operation: quiesce (plan pitfall 4)
      return false;
    }
    // A zero timeout is a non-blocking probe used to drain ready completions;
    // only a real wait that expires late counts as an xrun.
    if (running_ && timeoutMs > 0) noteXrun(captureStats_);  // late reap
    return false;
  }
  urb->inFlight = false;
  if (isDeviceGone(urb->status)) {
    running_ = false;  // unplug: stop resubmitting (plan pitfall 4)
    return true;
  }
  if (urb->isFeedback) {
    handleFeedback(*urb);
  } else if (urb->isCapture) {
    handleCapture(*urb);
  } else {
    handlePlayback(*urb);
  }
  if (running_) resubmit(*urb);
  return true;
}

}  // namespace t3k::usb
