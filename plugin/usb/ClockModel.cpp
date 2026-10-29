#include "ClockModel.h"

#include <algorithm>
#include <cmath>

namespace t3k::usb {
namespace {

// The device rate may legitimately wander a few hundred ppm, and a cold
// start can read a little worse, but a feedback value far outside this band
// is a garbled packet, not a clock.
constexpr double kMinFeedbackRatio = 0.5;
constexpr double kMaxFeedbackRatio = 2.0;

}  // namespace

ClockModel::ClockModel(double nominalRate, double kp, double ki)
    : nominalRate_(nominalRate), kp_(kp), ki_(ki) {}

void ClockModel::addCaptureFrames(int64_t frames, uint64_t elapsedNs) {
  windowFrames_ += frames;
  windowNs_ += elapsedNs;
  // Keep the window bounded (~10 s) so the estimate tracks slow temperature
  // drift instead of averaging the device's whole history.
  const uint64_t kMaxWindowNs = 10ull * 1000 * 1000 * 1000;
  if (windowNs_ > kMaxWindowNs) {
    const double keep = static_cast<double>(kMaxWindowNs) * 0.5 / windowNs_;
    windowFrames_ = static_cast<int64_t>(windowFrames_ * keep);
    windowNs_ = static_cast<uint64_t>(windowNs_ * keep);
  }
}

void ClockModel::setFeedback(double framesPerPacket, int nominalPerPacket) {
  if (nominalPerPacket <= 0) return;
  const double ratio = framesPerPacket / static_cast<double>(nominalPerPacket);
  if (ratio < kMinFeedbackRatio || ratio > kMaxFeedbackRatio) return;
  feedbackValid_ = true;
  feedbackValue_ = framesPerPacket;
  feedbackRatio_ = ratio;
}

void ClockModel::clearFeedback() {
  feedbackValid_ = false;
  feedbackValue_ = 0.0;
  feedbackRatio_ = 1.0;
  feedbackRemainder_ = 0.0;
}

double ClockModel::measuredRate() const {
  if (feedbackValid_) return nominalRate_ * feedbackRatio_;
  if (windowNs_ == 0) return nominalRate_;
  return static_cast<double>(windowFrames_) * 1e9 / static_cast<double>(windowNs_);
}

double ClockModel::driftPpm() const {
  if (feedbackValid_) return (feedbackRatio_ - 1.0) * 1e6;
  return (measuredRate() - nominalRate_) / nominalRate_ * 1e6;
}

void ClockModel::reset() {
  windowFrames_ = 0;
  windowNs_ = 0;
  fillErrorSum_ = 0.0;
  clearFeedback();
}

int ClockModel::playbackFramesPerPacket(int nominalPerPacket, size_t fillFrames,
                                        size_t targetFillFrames) {
  if (feedbackValid_) {
    // Feedforward: the device's own rate decides the average packet size.
    // Fractional frames carry across calls so a 500 ppm drift turns into one
    // extra frame every ~2000/ppm packets instead of a permanent bias.
    feedbackRemainder_ += static_cast<double>(nominalPerPacket) * feedbackRatio_;
    const double whole = std::floor(feedbackRemainder_);
    feedbackRemainder_ -= whole;
    const int base = static_cast<int>(whole);
    // Trim only the residual fill error; no integrator, so a start-up ring
    // that has not filled yet cannot wind the controller up.
    const double error = static_cast<double>(fillFrames) -
                         static_cast<double>(targetFillFrames);
    int trim = static_cast<int>(std::lround(kp_ * error));
    trim = std::clamp(trim, -1, 1);
    return std::max(1, base + trim);
  }

  const double error = static_cast<double>(fillFrames) - static_cast<double>(targetFillFrames);
  fillErrorSum_ += error;
  // Anti-windup: a bounded integrator, so a long outage cannot shove the
  // output around afterwards.
  fillErrorSum_ = std::clamp(fillErrorSum_, -1e6, 1e6);
  const double out = kp_ * error + ki_ * fillErrorSum_;
  return nominalPerPacket + static_cast<int>(std::clamp(std::lround(out), -1L, 1L));
}

double ClockModel::parseFeedback(const uint8_t* data, bool format16_16) {
  // Feedback values are frames per microframe (HS) or frame (FS). UAC1's
  // 10.14 form is a 3-byte value; reading a fourth byte would pull in
  // whatever the device put after it.
  uint32_t raw = 0;
  if (format16_16) {
    raw = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
          (static_cast<uint32_t>(data[2]) << 16) |
          (static_cast<uint32_t>(data[3]) << 24);
    return static_cast<double>(raw) / 65536.0;
  }
  raw = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16);
  return static_cast<double>(raw) / 16384.0;
}

}  // namespace t3k::usb
