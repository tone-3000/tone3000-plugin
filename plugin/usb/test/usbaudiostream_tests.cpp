// L0: the URB scheduling machine against a scripted fake transport (plan
// section 6.3: submission order, xrun accounting, unplug and teardown).

#include <cstdint>
#include <vector>

#include "PcmConvert.h"
#include "UsbAudioStream.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr int kSubslot = 3;
constexpr int kBpf = kChannels * kSubslot;  // 6 bytes per frame

StreamConfig testConfig() {
  StreamConfig cfg;
  cfg.sampleRate = kRate;
  cfg.channels = kChannels;
  cfg.subslotBytes = kSubslot;
  cfg.bitResolution = 24;
  cfg.speed = UsbSpeed::high;
  cfg.playbackEndpoint = 0x02;
  cfg.captureEndpoint = 0x81;
  return cfg;
}

struct FakeTransport : UrbTransport {
  std::vector<Urb*> inFlight;
  std::vector<Urb*> completed;
  int discards = 0;
  bool rejectSubmits = false;
  bool rejectFeedbackSubmits = false;
  bool rejectPlaybackSubmits = false;
  bool gone = false;
  /** Fail this many submit() calls, then accept again (transient error). */
  int failNextSubmits = 0;

  bool deviceGone() const override { return gone; }

  bool submit(Urb* urb) override {
    if (failNextSubmits > 0) {
      --failNextSubmits;
      return false;
    }
    if (rejectSubmits) return false;
    if (rejectPlaybackSubmits && !urb->isCapture) return false;
    if (rejectFeedbackSubmits && urb->isFeedback) return false;
    inFlight.push_back(urb);
    return true;
  }
  Urb* reap(int) override {
    if (completed.empty()) return nullptr;
    Urb* urb = completed.front();
    completed.erase(completed.begin());
    return urb;
  }
  void discard(Urb* urb) override {
    ++discards;
    // The kernel reaps discarded URBs with -ECONNRESET; mirror that.
    urb->status = -104;
    completed.push_back(urb);
    for (size_t i = 0; i < inFlight.size(); ++i) {
      if (inFlight[i] == urb) {
        inFlight.erase(inFlight.begin() + static_cast<long>(i));
        break;
      }
    }
  }
  /** Mark every packet as arriving full and error-free. */
  static void fillFull(Urb* urb) {
    for (size_t p = 0; p < urb->packetLengths.size(); ++p) {
      urb->packetActual[p] = urb->packetLengths[p];
      urb->packetStatus[p] = 0;
    }
    urb->status = 0;
  }
  /** Hand a completed URB to the stream; packet fields are the caller's. */
  void complete(Urb* urb, int startFrame) {
    urb->startFrame = startFrame;
    for (size_t i = 0; i < inFlight.size(); ++i) {
      if (inFlight[i] == urb) {
        inFlight.erase(inFlight.begin() + static_cast<long>(i));
        break;
      }
    }
    completed.push_back(urb);
  }
};

struct RecordingCallback : UsbAudioStream::Callback {
  int frames = 0;
  int channels = 0;
  int calls = 0;
  std::vector<float> lastIn;
  float outValue = 0.25f;
  void process(const float* in, float* out, int f, int ch) override {
    frames = f;
    channels = ch;
    ++calls;
    lastIn.assign(in, in + static_cast<size_t>(f) * ch);
    for (int i = 0; i < f * ch; ++i) out[i] = outValue;
  }
};

Urb* firstOf(FakeTransport& t, bool capture) {
  for (Urb* u : t.inFlight) {
    if (u->isCapture == capture) return u;
  }
  return nullptr;
}

Urb* firstFeedback(FakeTransport& t) {
  for (Urb* u : t.inFlight) {
    if (u->isFeedback) return u;
  }
  return nullptr;
}

StreamConfig feedbackConfig() {
  StreamConfig cfg = testConfig();
  cfg.feedbackEndpoint = 0x82;
  cfg.feedback16_16 = true;
  cfg.feedbackBytes = 4;
  return cfg;
}

TEST(UsbAudioStream, StartSubmitsBothDirections) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(transport.inFlight.size(), 6u);  // 3 capture + 3 playback
  EXPECT_EQ(stream.inFlightCount(), 6);
  EXPECT_EQ(stream.callbackFrames(), 48);  // 8 packets * 6 frames at 48 kHz HS
}

TEST(UsbAudioStream, CaptureReapFillsRingAndResubmits) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  Urb* urb = firstOf(transport, /*capture=*/true);
  ASSERT_NE(urb, nullptr);
  // Packets full of a recognizable wire pattern (frame n = value n).
  for (int p = 0; p < 8; ++p) {
    for (int f = 0; f < 6; ++f) {
      const int32_t v = p * 6 + f;
      uint8_t* at = urb->buffer.data() + (p * 7 + f) * kBpf;  // 7-frame packets
      at[0] = static_cast<uint8_t>(v & 0xff);
      at[1] = static_cast<uint8_t>((v >> 8) & 0xff);
      at[2] = static_cast<uint8_t>((v >> 16) & 0xff);
      at[3] = 0;
      at[4] = 0;
      at[5] = 0;
    }
    urb->packetActual[p] = urb->packetLengths[p];  // full packet (7 frames)
    urb->packetStatus[p] = 0;
  }
  transport.complete(urb, /*startFrame=*/1000);

  ASSERT_TRUE(stream.pumpOnce(0));
  // 8 packets * 7 frames arrived; nominal is 6, so the tolerance check must
  // accept this without an xrun.
  EXPECT_EQ(stream.captureStats().urbsReaped, 1u);
  EXPECT_EQ(stream.captureStats().packets, 8u);
  EXPECT_EQ(stream.captureStats().xruns, 0u);
  EXPECT_EQ(transport.inFlight.size(), 6u);  // resubmitted
}

TEST(UsbAudioStream, PlaybackReapRunsCallbackAndFillsWire) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  // start() primes every playback URB through the callback (the URBs must
  // leave the door with data), so the reap is one call past the priming.
  const int primed = cb.calls;
  EXPECT_EQ(primed, 3);

  Urb* urb = firstOf(transport, /*capture=*/false);
  ASSERT_NE(urb, nullptr);
  FakeTransport::fillFull(urb);
  transport.complete(urb, /*startFrame=*/2000);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(cb.calls, primed + 1);
  EXPECT_EQ(cb.channels, kChannels);

  // The URB buffer must be the callback's output on the wire.
  const int frames = cb.frames;
  std::vector<float> back(static_cast<size_t>(frames) * kChannels);
  pcm::wireToFloat(urb->buffer.data(), back.data(), static_cast<size_t>(frames),
                   kChannels, kSubslot);
  for (float v : back) EXPECT_NEAR(v, cb.outValue, 1e-4);
  EXPECT_EQ(stream.playbackStats().urbsReaped, 1u);
}

TEST(UsbAudioStream, DuplexCallbackSeesCapturedAudio) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  // Capture first: 8 packets x 6 frames of a ramp in the left channel.
  Urb* cap = firstOf(transport, /*capture=*/true);
  ASSERT_NE(cap, nullptr);
  for (int p = 0; p < 8; ++p) {
    for (int f = 0; f < 6; ++f) {
      const int32_t v = p * 6 + f;
      uint8_t* at = cap->buffer.data() + (p * 7 + f) * kBpf;
      at[0] = static_cast<uint8_t>(v & 0xff);
      at[1] = static_cast<uint8_t>((v >> 8) & 0xff);
      at[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    }
    cap->packetActual[p] = cap->packetLengths[p];
    cap->packetStatus[p] = 0;
  }
  transport.complete(cap, 0);
  ASSERT_TRUE(stream.pumpOnce(0));

  // Then a playback reap: the callback must see those samples as input.
  Urb* play = firstOf(transport, /*capture=*/false);
  ASSERT_NE(play, nullptr);
  FakeTransport::fillFull(play);
  transport.complete(play, 8);
  const int primed = cb.calls;
  ASSERT_TRUE(stream.pumpOnce(0));
  ASSERT_EQ(cb.calls, primed + 1);
  // The PI controller trims the URB by one frame while the ring sits below
  // its 50% target: the callback sees 46..48 frames here.
  const int frames = cb.frames;
  ASSERT_GE(frames, 46);
  ASSERT_LE(frames, 48);
  ASSERT_EQ(cb.lastIn.size(), static_cast<size_t>(frames) * kChannels);
  for (int f = 0; f < frames; ++f) {
    EXPECT_NEAR(cb.lastIn[static_cast<size_t>(f) * kChannels],
                pcm::int24ToFloat(f), 1e-5)
        << "frame " << f;
    EXPECT_NEAR(cb.lastIn[static_cast<size_t>(f) * kChannels + 1], 0.0f, 1e-5);
  }
}

TEST(UsbAudioStream, PlaybackPacingCanUseOneExtraFrame) {
  // A high ring drives the pacer to +1 frame; the callback buffers must hold
  // it (the pre-M2.2 scratch allocation overflowed here).
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  for (int i = 0; i < 8; ++i) {
    Urb* cap = firstOf(transport, /*capture=*/true);
    ASSERT_NE(cap, nullptr);
    FakeTransport::fillFull(cap);  // 8 packets x 6 frames = 48
    transport.complete(cap, i * 8);
    ASSERT_TRUE(stream.pumpOnce(0));
  }
  EXPECT_EQ(stream.captureStats().ringOverruns, 0u);

  Urb* play = firstOf(transport, /*capture=*/false);
  ASSERT_NE(play, nullptr);
  FakeTransport::fillFull(play);
  transport.complete(play, 0);
  ASSERT_TRUE(stream.pumpOnce(0));

  EXPECT_EQ(cb.frames, 49);
  int frames = 0;
  for (uint32_t len : play->packetLengths) frames += static_cast<int>(len / kBpf);
  EXPECT_EQ(frames, 49);
  // Every packet still holds whole frames.
  for (uint32_t len : play->packetLengths) EXPECT_EQ(len % kBpf, 0u);
}

TEST(UsbAudioStream, SynchronousPlaybackIgnoresRingFill) {
  // Same high ring as above, but a synchronous endpoint follows the USB
  // frame clock: every playback URB stays at the nominal 48 frames.
  StreamConfig cfg = testConfig();
  cfg.playbackSynchronous = true;
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(cfg, {}, transport, cb);
  ASSERT_TRUE(stream.start());

  for (int i = 0; i < 8; ++i) {
    Urb* cap = firstOf(transport, /*capture=*/true);
    ASSERT_NE(cap, nullptr);
    FakeTransport::fillFull(cap);
    transport.complete(cap, i * 8);
    ASSERT_TRUE(stream.pumpOnce(0));
  }

  Urb* play = firstOf(transport, /*capture=*/false);
  ASSERT_NE(play, nullptr);
  FakeTransport::fillFull(play);
  transport.complete(play, 0);
  ASSERT_TRUE(stream.pumpOnce(0));

  EXPECT_EQ(cb.frames, 48);
  for (uint32_t len : play->packetLengths) EXPECT_EQ(len, static_cast<uint32_t>(6 * kBpf));
}

TEST(UsbAudioStream, PlaybackUnderrunRepeatsAndCounts) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  const uint64_t startUnderruns = stream.playbackStats().ringUnderruns;
  ASSERT_GE(startUnderruns, 3u);  // priming before any capture arrived

  Urb* play = firstOf(transport, /*capture=*/false);
  ASSERT_NE(play, nullptr);
  FakeTransport::fillFull(play);
  transport.complete(play, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.playbackStats().ringUnderruns, startUnderruns + 1);
  // readOrRepeat always hands the callback a full buffer.
  EXPECT_GE(cb.frames, 47);
  EXPECT_LE(cb.frames, 49);
}


TEST(UsbAudioStream, RingPolicyCountersAreSurfaced) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  // The three playback URBs primed before any capture count as underruns.
  const uint64_t startUnderruns = stream.playbackStats().ringUnderruns;

  // Overfill the ring: 12 x 48 frames into a 512-frame ring.
  for (int i = 0; i < 12; ++i) {
    Urb* cap = firstOf(transport, /*capture=*/true);
    ASSERT_NE(cap, nullptr);
    FakeTransport::fillFull(cap);
    transport.complete(cap, i * 8);
    ASSERT_TRUE(stream.pumpOnce(0));
  }
  // A playback reap reads across the lap, counting the overrun. The ring had
  // data, so no output underrun is added.
  Urb* play = firstOf(transport, /*capture=*/false);
  ASSERT_NE(play, nullptr);
  FakeTransport::fillFull(play);
  transport.complete(play, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.playbackStats().ringUnderruns, startUnderruns);

  // The overrun is visible on the capture stats after the next capture reap.
  Urb* cap = firstOf(transport, /*capture=*/true);
  ASSERT_NE(cap, nullptr);
  FakeTransport::fillFull(cap);
  transport.complete(cap, 12 * 8);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().ringOverruns, 1u);
}


TEST(UsbAudioStream, ShortCapturePacketCountsXrun) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  Urb* urb = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(urb);
  urb->packetActual[0] = 4 * kBpf;  // 4 frames: 2 short of nominal 6
  transport.complete(urb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 1u);
}

TEST(UsbAudioStream, PacketStatusErrorCountsXrun) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  Urb* urb = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(urb);
  urb->packetStatus[3] = -71;  // -EPROTO
  transport.complete(urb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 1u);
}

TEST(UsbAudioStream, StartFrameGapCountsXrun) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  // Two reaps establish the normal step (8 microframes at HS); the third
  // arrives 20 late and must flag. Continuity is judged against the observed
  // step because usbfs start_frame units are HCD-defined.
  Urb* first = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(first);
  transport.complete(first, 1000);
  ASSERT_TRUE(stream.pumpOnce(0));
  Urb* second = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(second);
  transport.complete(second, 1008);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 0u);
  Urb* third = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(third);
  transport.complete(third, 1008 + 20);  // a 20-microframe hole
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 1u);
}

TEST(UsbAudioStream, UnplugStopsResubmission) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  Urb* urb = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(urb);
  urb->status = -108;  // -ESHUTDOWN
  transport.complete(urb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_FALSE(stream.running());
  EXPECT_EQ(transport.inFlight.size(), 5u);  // nothing resubmitted
}

TEST(UsbAudioStream, StopDiscardsAndCollectsEverything) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  stream.stop();
  EXPECT_EQ(transport.discards, 6);
  EXPECT_EQ(stream.inFlightCount(), 0);  // all reaped back
  EXPECT_FALSE(stream.running());
}

TEST(UsbAudioStream, TransportErrorQuiescesInsteadOfXrunning) {
  // Unplug mid-stream: reap reports a fatal error (no URB comes back). The
  // machine must go quiescent, not count a late reap every timeout.
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  transport.gone = true;
  EXPECT_FALSE(stream.pumpOnce(0));
  EXPECT_FALSE(stream.running());
  EXPECT_EQ(stream.captureStats().xruns, 0u);
  // Further pumps stay quiet too.
  EXPECT_FALSE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 0u);
}

TEST(UsbAudioStream, LateReapTimeoutCountsXrun) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  EXPECT_FALSE(stream.pumpOnce(50));  // nothing completed in time
  EXPECT_EQ(stream.captureStats().xruns, 1u);
}

TEST(UsbAudioStream, NonBlockingProbeDoesNotCountXrun) {
  // pumpOnce(0) is the drain loop's probe: no completion is normal, not a
  // late reap.
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  EXPECT_FALSE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 0u);
}

TEST(UsbAudioStream, StartsFeedbackUrbsWhenExplicitEndpointExists) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(feedbackConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  EXPECT_EQ(transport.inFlight.size(), 9u);  // 3 capture + 3 playback + 3 feedback
  Urb* fb = firstFeedback(transport);
  ASSERT_NE(fb, nullptr);
  EXPECT_EQ(fb->endpoint, 0x82);
  EXPECT_EQ(fb->packetLengths.size(), 8u);
  for (uint32_t len : fb->packetLengths) EXPECT_EQ(len, 4u);
}

TEST(UsbAudioStream, FeedbackReapDrivesClockAndDriftStats) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(feedbackConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  Urb* fb = firstFeedback(transport);
  ASSERT_NE(fb, nullptr);
  // 6.003 frames per microframe in 16.16 = +501 ppm at 48 kHz high speed.
  const uint32_t raw = 393413;
  for (int p = 0; p < 8; ++p) {
    uint8_t* at = fb->buffer.data() + static_cast<size_t>(p) * 4;
    at[0] = static_cast<uint8_t>(raw & 0xff);
    at[1] = static_cast<uint8_t>((raw >> 8) & 0xff);
    at[2] = static_cast<uint8_t>((raw >> 16) & 0xff);
    at[3] = static_cast<uint8_t>((raw >> 24) & 0xff);
    fb->packetActual[p] = 4;
    fb->packetStatus[p] = 0;
  }
  transport.complete(fb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));

  EXPECT_TRUE(stream.clock().hasFeedback());
  EXPECT_NEAR(stream.feedbackStats().feedbackValue, 6.003, 1e-4);
  EXPECT_EQ(stream.feedbackStats().feedbackPackets, 8u);
  EXPECT_EQ(stream.feedbackStats().feedbackErrors, 0u);
  EXPECT_NEAR(stream.playbackStats().driftPpm, 501.0, 1.0);
  EXPECT_NEAR(stream.captureStats().driftPpm, 501.0, 1.0);
  // The reaped feedback URB went back out.
  EXPECT_EQ(transport.inFlight.size(), 9u);
}

TEST(UsbAudioStream, FeedbackReapUsesItsOwnPacketCountAtLargerAudioUrbs) {
  // The feedback URB keeps its 8-packet cadence while the audio URBs grow
  // (96-frame blocks = 16 packets at 48 kHz). Walking the feedback URB with
  // the audio count read past its vectors and crashed parseFeedback
  // (SIGSEGV when the buffer was raised to 96, 2026-10-19).
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream::Options options;
  options.packetsPerUrb = 16;
  UsbAudioStream stream(feedbackConfig(), options, transport, cb);
  ASSERT_TRUE(stream.start());

  Urb* fb = firstFeedback(transport);
  ASSERT_NE(fb, nullptr);
  ASSERT_EQ(fb->packetLengths.size(), 8u);
  // 6.003 frames per microframe in 16.16 = +501 ppm at 48 kHz high speed.
  const uint32_t raw = 393413;
  for (size_t p = 0; p < fb->packetLengths.size(); ++p) {
    uint8_t* at = fb->buffer.data() + p * 4;
    at[0] = static_cast<uint8_t>(raw & 0xff);
    at[1] = static_cast<uint8_t>((raw >> 8) & 0xff);
    at[2] = static_cast<uint8_t>((raw >> 16) & 0xff);
    at[3] = static_cast<uint8_t>((raw >> 24) & 0xff);
    fb->packetActual[p] = 4;
    fb->packetStatus[p] = 0;
  }
  transport.complete(fb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));

  EXPECT_EQ(stream.feedbackStats().packets, 8u);
  EXPECT_EQ(stream.feedbackStats().feedbackPackets, 8u);
  EXPECT_EQ(stream.feedbackStats().feedbackErrors, 0u);
  EXPECT_NEAR(stream.feedbackStats().feedbackValue, 6.003, 1e-4);
}

TEST(UsbAudioStream, FeedbackSubmitFailureFallsBackInsteadOfStopping) {
  FakeTransport transport;
  RecordingCallback cb;
  transport.rejectFeedbackSubmits = true;
  UsbAudioStream stream(feedbackConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(transport.inFlight.size(), 6u);  // audio URBs unaffected
  EXPECT_FALSE(stream.clock().hasFeedback());
  EXPECT_EQ(stream.feedbackStats().feedbackErrors, 3u);
}

TEST(UsbAudioStream, FeedbackStallAtRuntimeFallsBackToCaptureEstimate) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(feedbackConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());
  Urb* fb = firstFeedback(transport);
  ASSERT_NE(fb, nullptr);
  const uint32_t raw = 393216;  // 6.0 in 16.16
  for (int p = 0; p < 8; ++p) {
    uint8_t* at = fb->buffer.data() + static_cast<size_t>(p) * 4;
    at[0] = static_cast<uint8_t>(raw & 0xff);
    at[1] = static_cast<uint8_t>((raw >> 8) & 0xff);
    at[2] = static_cast<uint8_t>((raw >> 16) & 0xff);
    at[3] = static_cast<uint8_t>((raw >> 24) & 0xff);
    fb->packetActual[p] = 4;
    fb->packetStatus[p] = 0;
  }
  transport.complete(fb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_TRUE(stream.clock().hasFeedback());

  // The endpoint stops accepting URBs (device quirk): audio keeps running,
  // pacing reverts to the capture-based estimate.
  transport.rejectFeedbackSubmits = true;
  transport.complete(fb, 8);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_TRUE(stream.running());
  EXPECT_FALSE(stream.clock().hasFeedback());
}


TEST(UsbAudioStream, DriftIsEstimatedFromCaptureProgress) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream::Options opts;
  // Synthetic time: each reap advances 1 ms + 500 ppm of clock, while the
  // device delivers the nominal 48 frames per reap => the device is slow by
  // 500 ppm.
  uint64_t now = 0;
  opts.nowNs = [&now] { return now; };
  UsbAudioStream stream(testConfig(), opts, transport, cb);
  ASSERT_TRUE(stream.start());

  for (int i = 0; i < 200; ++i) {
    now += 1000500;  // 1.0005 ms of wall time
    Urb* urb = firstOf(transport, /*capture=*/true);
    urb->packetActual.assign(8, static_cast<uint32_t>(6 * kBpf));
    urb->packetStatus.assign(8, 0);
    urb->packetLengths.assign(8, static_cast<uint32_t>(6 * kBpf));
    transport.complete(urb, i * 8);
    ASSERT_TRUE(stream.pumpOnce(0));
  }
  EXPECT_NEAR(stream.captureStats().driftPpm, -500.0, 20.0);
}

// --- M2.3 recovery paths ---------------------------------------------------

UsbAudioStream::Options fastRetryOptions() {
  UsbAudioStream::Options opts;
  opts.submitAttempts = 3;
  opts.submitBackoffMicros = 0;  // keep the host suite instant
  return opts;
}

TEST(UsbAudioStream, TransientSubmitFailuresAreRetried) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), fastRetryOptions(), transport, cb);
  transport.failNextSubmits = 2;  // first capture URB fails twice, then lands
  ASSERT_TRUE(stream.start());
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(transport.inFlight.size(), 6u);
  EXPECT_EQ(stream.captureStats().submitFailures, 2u);
  EXPECT_EQ(stream.captureStats().submitRetries, 2u);
}

TEST(UsbAudioStream, ExhaustedSubmitKeepsStreamRunningAndRequeues) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), fastRetryOptions(), transport, cb);
  ASSERT_TRUE(stream.start());

  transport.failNextSubmits = 3;  // exhausts one resubmit's budget
  Urb* urb = firstOf(transport, /*capture=*/true);
  ASSERT_NE(urb, nullptr);
  FakeTransport::fillFull(urb);
  transport.complete(urb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  // The failed URB is deferred, not dropped: audio keeps running.
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(stream.inFlightCount(), 5);
  EXPECT_EQ(stream.captureStats().submitFailures, 3u);
  EXPECT_EQ(stream.captureStats().xruns, 1u);

  // The next pump drains the deferred queue before blocking in reap (it
  // returns false because no reap completed, but the URB is back in flight).
  stream.pumpOnce(0);
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(stream.inFlightCount(), 6);
}

TEST(UsbAudioStream, SubmitFailureOnGoneDeviceQuiesces) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), fastRetryOptions(), transport, cb);
  ASSERT_TRUE(stream.start());

  transport.rejectSubmits = true;
  transport.gone = true;
  Urb* urb = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(urb);
  transport.complete(urb, 0);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_FALSE(stream.running());
  EXPECT_EQ(stream.captureStats().submitFailures, 1u);  // no retry budget burned
}

TEST(UsbAudioStream, StartFailureDiscardsOutstandingUrbs) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), fastRetryOptions(), transport, cb);
  // The capture URB lands, the playback one never does: start must unwind
  // the URBs already handed to the kernel before returning.
  transport.rejectPlaybackSubmits = true;
  EXPECT_FALSE(stream.start());
  EXPECT_FALSE(stream.running());
  EXPECT_EQ(stream.inFlightCount(), 0);
  EXPECT_GE(transport.discards, 1);
  EXPECT_EQ(stream.playbackStats().submitFailures, 3u);
}

TEST(UsbAudioStream, DiscardedUrbCountsOneXrunAndResubmits) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), fastRetryOptions(), transport, cb);
  ASSERT_TRUE(stream.start());

  // Simulate a device-side error burst: cancel an in-flight capture URB.
  // The kernel reaps it with -ECONNRESET; that is one xrun, not eight.
  ASSERT_EQ(stream.discardOneInFlight(), 1);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_TRUE(stream.running());
  EXPECT_EQ(stream.captureStats().xruns, 1u);
  EXPECT_EQ(transport.inFlight.size(), 6u);  // resubmitted after the error
}

TEST(UsbAudioStream, UrErrorDoesNotTriggerStartFrameXrun) {
  FakeTransport transport;
  RecordingCallback cb;
  UsbAudioStream stream(testConfig(), {}, transport, cb);
  ASSERT_TRUE(stream.start());

  // Establish a frame step, then deliver an errored URB whose stale
  // start_frame would otherwise look like a discontinuity.
  Urb* first = firstOf(transport, /*capture=*/true);
  FakeTransport::fillFull(first);
  transport.complete(first, 1000);
  ASSERT_TRUE(stream.pumpOnce(0));

  Urb* second = firstOf(transport, /*capture=*/true);
  ASSERT_NE(second, nullptr);
  second->status = -104;
  transport.complete(second, 9000);
  ASSERT_TRUE(stream.pumpOnce(0));
  EXPECT_EQ(stream.captureStats().xruns, 1u);  // the URB error only
  EXPECT_TRUE(stream.running());
}

}  // namespace
}  // namespace t3k::usb
