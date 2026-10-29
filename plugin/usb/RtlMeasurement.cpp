#include "RtlMeasurement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace t3k::usb {
namespace rtl {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::vector<float> makeSweep(double sampleRate, double burstMs, float ampDb) {
  const size_t n = std::max<size_t>(1, static_cast<size_t>(sampleRate * burstMs / 1000.0));
  std::vector<float> out(n);
  const float amp = std::pow(10.0f, ampDb / 20.0f);
  // Exponential sine sweep f0..f1 across the burst (ESS): frequency rises
  // exponentially so every decade of the band gets equal energy.
  const double f0 = 200.0;
  const double f1 = 0.4 * sampleRate;
  const double k = static_cast<double>(n) / (sampleRate * std::log(f1 / f0));
  // 1 ms raised edges so the click itself does not dominate the spectrum.
  const size_t fade = std::max<size_t>(1, static_cast<size_t>(sampleRate * 0.001));
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const double sweep = std::sin(2.0 * kPi * f0 * k * (std::exp(t / k) - 1.0));
    const double edge = std::min(1.0, static_cast<double>(std::min(i, n - 1 - i)) / fade);
    out[i] = static_cast<float>(amp * sweep * edge);
  }
  return out;
}

std::vector<float> makeMls(float ampDb) {
  // Length-4096 maximum-length sequence from x^12 + x^6 + x^4 + x + 1,
  // deterministic from the all-ones seed.
  const size_t length = 4096;
  std::vector<float> out(length);
  const float amp = std::pow(10.0f, ampDb / 20.0f);
  uint32_t reg = 0xFFF;
  for (size_t i = 0; i < length; ++i) {
    const uint32_t bit = ((reg >> 11) ^ (reg >> 5) ^ (reg >> 3) ^ (reg >> 0)) & 1u;
    reg = ((reg << 1) | bit) & 0xFFF;
    out[i] = bit ? amp : -amp;
  }
  return out;
}

FindResult findBurst(const float* rec, size_t recFrames, const float* probe,
                     size_t probeFrames) {
  FindResult result;
  if (rec == nullptr || probe == nullptr || recFrames < probeFrames || probeFrames == 0) {
    return result;
  }
  // Direct cross-correlation at lag L in [0, recFrames - probeFrames]:
  // corr(L) = sum_i rec[L+i] * probe[i]. The peak location is the probe's
  // start index in the record.
  const size_t lags = recFrames - probeFrames + 1;
  std::vector<double> corr(lags, 0.0);
  double best = -1.0;
  size_t bestLag = 0;
  for (size_t L = 0; L < lags; ++L) {
    double sum = 0.0;
    for (size_t i = 0; i < probeFrames; ++i) sum += rec[L + i] * probe[i];
    corr[L] = std::abs(sum);
    if (corr[L] > best) {
      best = corr[L];
      bestLag = L;
    }
  }
  // Next lobe: the largest correlation at least one burst length away from
  // the peak, so the main lobe's own skirt does not count.
  const size_t guard = std::max<size_t>(probeFrames, 1);
  double nextBest = 0.0;
  for (size_t L = 0; L < lags; ++L) {
    const size_t d = L > bestLag ? L - bestLag : bestLag - L;
    if (d < guard) continue;
    nextBest = std::max(nextBest, corr[L]);
  }
  result.lag = static_cast<int64_t>(bestLag);
  result.peakRatio = nextBest > 0.0 ? best / nextBest : std::numeric_limits<double>::infinity();
  result.valid = result.peakRatio >= 3.0;
  return result;
}

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  return (n % 2 == 1) ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

}  // namespace rtl
}  // namespace t3k::usb
