#pragma once

// Self-measurement of round-trip latency (plan section 4.1): generate a
// deterministic probe burst, cross-correlate a capture against it, and turn
// the correlation peaks into a latency in samples. Used by the probe app's
// rtl mode and covered by host tests against synthetic captures.
//
// The sanity metric is the correlation peak against the next-largest lobe at
// least one burst length away: below 3.0 the measurement is garbage (noise
// floor, wrong channel, or the signal never arrived).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace t3k::usb {
namespace rtl {

/** 2 ms exponential sine sweep at -12 dBFS (default probe; its
    autocorrelation is a sharp peak, robust against band-limited devices). */
std::vector<float> makeSweep(double sampleRate, double burstMs, float ampDb);

/** Length-4096 maximum-length sequence at -12 dBFS (deterministic). */
std::vector<float> makeMls(float ampDb);

struct FindResult {
  /** Start index of the probe inside the record, in frames. */
  int64_t lag = 0;
  /** Peak / next-lobe ratio. < 3.0 means garbage. */
  double peakRatio = 0.0;
  bool valid = false;
};

/** Cross-correlate rec against probe; find the burst. recFrames/probeFrames
    in frames (mono). */
FindResult findBurst(const float* rec, size_t recFrames, const float* probe,
                     size_t probeFrames);

/** Round-trip latency in milliseconds from the two lags. */
inline double rtlMs(int64_t pathLag, int64_t refLag, double sampleRate) {
  return static_cast<double>(pathLag - refLag) / sampleRate * 1000.0;
}

/** Median of the per-trial values (plan section 4.1 step 4). */
double median(std::vector<double> values);

}  // namespace rtl
}  // namespace t3k::usb
