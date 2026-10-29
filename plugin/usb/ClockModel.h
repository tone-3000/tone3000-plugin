#pragma once

// Device clock drift estimation and playback packet regulation (plan
// section 6.4). The interface clock and the host clock differ by up to
// ~+/-500 ppm; without compensation the ring drifts and xruns appear every
// few minutes. Two jobs:
//
//   1. Estimate the device's real rate: from the explicit feedback endpoint
//      when the device has one (the AMS-22 does: 0x82, UAC2 16.16), else
//      from observed capture frames/time. Report drift in ppm.
//   2. Regulate playback packet sizing on the ring fill level (PI on the
//      fill error, output in whole frames per packet) so the fill stays near
//      its target. With explicit feedback the device's rate is the
//      feedforward term and the fill controller only trims the residual.
//
// Pure C++ with an injectable time base (nanoseconds as a plain integer), so
// the convergence tests run on synthetic clocks.

#include <cstddef>
#include <cstdint>

namespace t3k::usb {

class ClockModel {
 public:
  explicit ClockModel(double nominalRate, double kp = 0.05, double ki = 0.002);

  /** Feed observed capture progress: frames received over elapsedNs. */
  void addCaptureFrames(int64_t frames, uint64_t elapsedNs);

  /** Feed one explicit-feedback value: frames per packet at the stream's
      packet cadence (per microframe at high speed, per frame at full
      speed). nominalPerPacket is the device's nominal frames per packet
      (sampleRate / 8000 at high speed). Values outside a plausible range
      are ignored so a garbled feedback packet cannot derail pacing. */
  void setFeedback(double framesPerPacket, int nominalPerPacket);
  /** Drop back to capture-based estimation (feedback endpoint stopped). */
  void clearFeedback();
  bool hasFeedback() const { return feedbackValid_; }
  /** Last accepted feedback value, frames per packet (0 = none yet). */
  double feedbackFramesPerPacket() const { return feedbackValue_; }

  /** Measured device rate from the observation window (Hz). */
  double measuredRate() const;
  /** (measured - nominal) / nominal * 1e6. Positive = device runs fast. */
  double driftPpm() const;
  void reset();

  /** Playback pacing: how many frames to put in the next packet given the
      ring fill. With explicit feedback the nominal packet size is scaled by
      the device's reported rate (fractional frames accumulate across
      packets) and the fill controller trims by at most +/-1 frame. Without
      feedback this is the original PI on the fill error. */
  int playbackFramesPerPacket(int nominalPerPacket, size_t fillFrames,
                              size_t targetFillFrames);

  /** Parse an explicit feedback endpoint value: UAC2 uses 16.16 (4 bytes),
      UAC1 10.14 (3 bytes). */
  static double parseFeedback(const uint8_t* data, bool format16_16);

 private:
  double nominalRate_;
  double kp_, ki_;
  int64_t windowFrames_ = 0;
  uint64_t windowNs_ = 0;
  double fillErrorSum_ = 0.0;

  bool feedbackValid_ = false;
  double feedbackValue_ = 0.0;
  /** Reported frames per packet divided by nominal frames per packet. */
  double feedbackRatio_ = 1.0;
  /** Fractional frame owed to feedforward pacing, carried between packets. */
  double feedbackRemainder_ = 0.0;
};

}  // namespace t3k::usb
