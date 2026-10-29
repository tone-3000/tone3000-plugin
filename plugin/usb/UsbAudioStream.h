#pragma once

// The isochronous URB scheduling machine (plan section 6.3). Owns the URB
// pools for both directions, the capture ring between them, and the xrun
// bookkeeping; talks to the outside only through an UrbTransport (the raw
// usbfs ioctls on Android: USBDEVFS_SUBMITURB / REAPURB / DISCARDURB on the
// fd from UsbDeviceConnection.getFileDescriptor) and a Callback (the audio
// processor, invoked on the USB reaping thread per design D3).
//
// The transport is an interface so the whole machine runs on the host
// against a scripted fake: every ordering, xrun, and teardown case is
// exercised in tests without hardware.
//
// Machine per direction (section 6.3):
//   1. N URBs of P packets each; packet length holds the nominal frame count
//      (+/-1 frame tolerance on capture; the actual length decides).
//   2. Submit all N; playback URBs are filled before submission.
//   3. Reap on the USB thread. Capture packets land in the ring
//      (writeDropOldest); playback URBs are refilled from the callback and
//      resubmitted.
//   4. start_frame continuity is tracked; gaps count as xruns.
//   5. stop() discards outstanding URBs, waits for the reaps and reports the
//      stream as quiescent. Returning to altsetting 0 is the caller's job
//      (the control path owns SET_INTERFACE).
//
// Playback pacing is feedforward from the explicit feedback endpoint when
// the device has one (plan section 6.4; the AMS-22 reports on 0x82): the
// device's real rate scales the playback packet size, and a fill controller
// trims the residual by +/-1 frame. Without an explicit endpoint the shell
// leaves feedbackEndpoint = 0 and the PI on the capture ring fill supplies
// the whole correction (implicit feedback). A synchronous playback endpoint
// (config.playbackSynchronous) skips pacing entirely: nominal packets only.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "AudioRing.h"
#include "ClockModel.h"
#include "UsbAudioTypes.h"

namespace t3k::usb {

/** One isochronous URB: a buffer plus per-packet descriptors. Mirrors
    usbdevfs_urb but stays portable; the transport maps it. */
struct Urb {
  uint8_t endpoint = 0;
  std::vector<uint8_t> buffer;
  /** Per-packet requested and actual lengths, and status (0 or -errno). */
  std::vector<uint32_t> packetLengths;
  std::vector<uint32_t> packetActual;
  std::vector<int32_t> packetStatus;
  int startFrame = 0;
  int status = 0;  // URB-level status (0 or -errno)
  bool inFlight = false;
  bool isCapture = true;
  /** Capture URBs on the explicit feedback endpoint: IN direction, but the
      payload is a rate value, not audio, and it never touches the ring. */
  bool isFeedback = false;
};

class UrbTransport {
 public:
  virtual ~UrbTransport() = default;
  /** Submit for async completion. False = rejected (device gone). */
  virtual bool submit(Urb* urb) = 0;
  /** Block up to timeoutMs for a completed URB; nullptr on timeout. */
  virtual Urb* reap(int timeoutMs) = 0;
  /** Ask the kernel to cancel an in-flight URB (it will still be reaped). */
  virtual void discard(Urb* urb) = 0;
  /** True once the transport has seen a fatal error (device unplugged:
      reap returns -ENODEV/-ENOTTY/-ESHUTDOWN instead of URBs). The stream
      machine goes quiescent instead of counting xruns forever. */
  virtual bool deviceGone() const { return false; }
};

class UsbAudioStream {
 public:
  struct Callback {
    virtual ~Callback() = default;
    /** Run on the USB thread once per playback URB. in/out interleaved
        floats, frames * channels. Input frames that never arrived are zero;
        the callback may write anything into out. */
    virtual void process(const float* in, float* out, int frames, int channels) = 0;
  };

  struct Options {
    int urbsInFlight = 3;   // plan M1.5 default
    int packetsPerUrb = 8;  // 1 ms per URB at high speed
    size_t ringFrames = 512;
    /** Bounded retry for a rejected submit() (plan M2.3). A transient usbfs
        failure must not tear the stream down; only deviceGone() is fatal.
        Attempts includes the first try; the backoff is per retry and 0 in
        tests so the host suite stays instant. */
    int submitAttempts = 3;
    int submitBackoffMicros = 250;
    /** Monotonic nanoseconds for drift measurement; null disables drift
        tracking (the estimator then reports 0 ppm). */
    std::function<uint64_t()> nowNs;
  };

  UsbAudioStream(const StreamConfig& config, const Options& options,
                 UrbTransport& transport, Callback& callback);

  /** Allocate the URB pools and submit the first round. */
  bool start();
  /** Discard outstanding URBs, reap them back and go quiescent. */
  void stop();

  /** Handle one reap. False = nothing completed within timeoutMs. */
  bool pumpOnce(int timeoutMs);

  bool running() const { return running_; }
  /** Frames the callback sees per invocation at the nominal pacing. */
  int callbackFrames() const;
  const StreamStats& captureStats() const { return captureStats_; }
  const StreamStats& playbackStats() const { return playbackStats_; }
  const StreamStats& feedbackStats() const { return feedbackStats_; }
  ClockModel& clock() { return clock_; }

  int inFlightCount() const;

  /** Recovery hook (plan M2.3): cancel one in-flight URB so the next reap
      takes the error path exactly as a device-side error burst would. The
      kernel reaps it with -ECONNRESET; the machine counts the xrun and
      resubmits. Returns the number of URBs cancelled (0 or 1). */
  int discardOneInFlight();

 private:
  void prepareCaptureUrb(Urb& urb);
  void preparePlaybackUrb(Urb& urb);
  void prepareFeedbackUrb(Urb& urb);
  void handleCapture(Urb& urb);
  void handlePlayback(Urb& urb);
  void handleFeedback(Urb& urb);
  bool resubmit(Urb& urb);
  /** Bounded submit retry; false only after exhausting attempts or when the
      transport has seen a fatal device error. */
  bool submitWithRetry(Urb& urb, StreamStats& stats);
  /** Retry URBs whose deferred submission failed a previous round. */
  bool drainRetryQueue();
  void noteXrun(StreamStats& stats) { ++stats.xruns; }
  void updateDriftStats();

  StreamConfig config_;
  Options options_;
  UrbTransport& transport_;
  Callback& callback_;
  ClockModel clock_;
  int framesPerPacket_;
  int bytesPerFrame_;
  size_t targetFill_;

  std::vector<std::unique_ptr<Urb>> urbs_;
  /** URBs whose submit failed transiently; retried on the next pump before
      blocking in reap. They are not in flight and hold no kernel resources. */
  std::vector<Urb*> retryQueue_;
  AudioRing captureRing_;
  std::vector<float> inScratch_;
  std::vector<float> outScratch_;
  std::vector<uint8_t> wireScratch_;

  StreamStats captureStats_;
  StreamStats playbackStats_;
  StreamStats feedbackStats_;
  bool running_ = false;
  /** Cleared for the rest of the stream if feedback URBs cannot be used;
      pacing then falls back to the capture-ring PI. */
  bool feedbackEnabled_ = false;
  uint64_t lastFeedbackNs_ = 0;
  int lastCaptureStartFrame_ = -1;
  int lastPlaybackStartFrame_ = -1;
  // Observed start_frame increment between consecutive URBs of one
  // direction. usbfs reports start_frame in units the HCD chooses (1 ms
  // frames on most, microframes on others), so continuity is judged against
  // the observed step, not an assumed one.
  int captureStartFrameDelta_ = 0;
  int playbackStartFrameDelta_ = 0;
  uint64_t lastCaptureNs_ = 0;
};

}  // namespace t3k::usb
