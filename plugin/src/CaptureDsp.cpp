#include "CaptureDsp.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace t3k::capture {
namespace {

constexpr size_t maxFftSize = size_t{1} << 24;
constexpr size_t maxFileSamples = static_cast<size_t>(sampleRate) * 60 * 15;

bool finite(const std::vector<float>& samples) {
  return std::all_of(samples.begin(), samples.end(),
                     [](float value) { return std::isfinite(value); });
}

float peak(const std::vector<float>& samples) {
  float result = 0.0f;
  for (const float sample : samples)
    result = std::max(result, std::abs(sample));
  return result;
}

// The caller bounds required before this function; no signed overflow or
// floating-point rounding is involved in selecting a power of two.
int fftOrderFor(size_t required) {
  size_t size = 1;
  int order = 0;
  while (size < required) {
    size <<= 1;
    ++order;
  }
  return order;
}

Alignment alignmentError(const juce::String& error) {
  return {0, 0.0f, error};
}
IrResult irError(const juce::String& error) {
  return {{}, error};
}

}  // namespace

std::vector<float> makeSweep(double seconds, float levelDb) {
  if (!std::isfinite(seconds) || !std::isfinite(levelDb) || seconds <= 0.0 || seconds > 120.0)
    return {};
  const auto count = static_cast<size_t>(std::llround(seconds * sampleRate));
  if (count < 2)
    return {};
  const double duration = static_cast<double>(count) / sampleRate;
  const double logRatio = std::log(20000.0 / 20.0);
  const double amplitude = std::pow(10.0, std::min(levelDb, 0.0f) / 20.0);
  const double phaseScale = juce::MathConstants<double>::twoPi * 20.0 * duration / logRatio;
  const size_t fade =
      std::max(size_t{1}, std::min(static_cast<size_t>(sampleRate / 100), count / 2));
  std::vector<float> result(count);
  for (size_t i = 0; i < count; ++i) {
    const double time = static_cast<double>(i) / sampleRate;
    const double phase = phaseScale * (std::exp(time * logRatio / duration) - 1.0);
    const double edge =
        static_cast<double>(std::min({i, count - 1 - i, fade})) / static_cast<double>(fade);
    const double envelope = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * edge);
    result[i] = static_cast<float>(amplitude * std::sin(phase) * envelope);
  }
  return result;
}

std::vector<float> resample(const std::vector<float>& samples, double sourceRate, double destRate) {
  if (!std::isfinite(sourceRate) || !std::isfinite(destRate) || sourceRate < 8000.0 ||
      sourceRate > 384000.0 || destRate < 8000.0 || destRate > 384000.0 || samples.empty() ||
      samples.size() > maxFileSamples || !finite(samples))
    return {};
  if (std::abs(sourceRate - destRate) < 1.0e-9)
    return samples;
  const double outputLength = static_cast<double>(samples.size()) * destRate / sourceRate;
  if (!std::isfinite(outputLength) || outputLength < 0.5 ||
      outputLength > static_cast<double>(maxFileSamples))
    return {};
  const size_t outputCount = static_cast<size_t>(std::llround(outputLength));
  const double ratio = sourceRate / destRate;
  // Leave a 4% transition below the lower Nyquist. The kernel widens when
  // downsampling so its stop-band attenuation remains useful at every ratio.
  // The four-term Blackman-Harris window provides low sidelobes; the 96-zero
  // half width gives a narrow transition for the 44.1/48 kHz conversion.
  const double cutoff = 0.96 * std::min(1.0, destRate / sourceRate);
  const int radius = static_cast<int>(std::ceil(96.0 / cutoff));
  const size_t taps = static_cast<size_t>(radius * 2 + 1);
  constexpr size_t phases = 2048;
  try {
    // Interpolating adjacent fractional-phase kernels avoids per-sample
    // trigonometry while retaining sub-sample time anchoring. The final
    // phase is included so interpolation across phase one is continuous.
    std::vector<double> coefficients((phases + 1) * taps);
    for (size_t phase = 0; phase <= phases; ++phase) {
      const double fraction = static_cast<double>(phase) / phases;
      double sum = 0.0;
      for (int offset = -radius; offset <= radius; ++offset) {
        const double distance = offset - fraction;
        const double position = distance / radius;
        double value = 0.0;
        if (std::abs(position) <= 1.0) {
          const double argument = juce::MathConstants<double>::pi * cutoff * distance;
          const double sinc = std::abs(argument) < 1.0e-12 ? 1.0 : std::sin(argument) / argument;
          const double angle = juce::MathConstants<double>::pi * position;
          const double window = 0.35875 + 0.48829 * std::cos(angle) +
                                0.14128 * std::cos(2.0 * angle) + 0.01168 * std::cos(3.0 * angle);
          value = cutoff * sinc * window;
        }
        const size_t tap = static_cast<size_t>(offset + radius);
        coefficients[phase * taps + tap] = value;
        sum += value;
      }
      // Correct only finite-kernel DC error. This is the filter's unit-gain
      // calibration, not peak/level normalization of the captured signal.
      for (size_t tap = 0; tap < taps; ++tap)
        coefficients[phase * taps + tap] /= sum;
    }
    std::vector<float> output(outputCount, 0.0f);
    const auto last = static_cast<int64_t>(samples.size()) - 1;
    for (size_t i = 0; i < outputCount; ++i) {
      const double sourcePosition = static_cast<double>(i) * ratio;
      const auto centre = static_cast<int64_t>(std::floor(sourcePosition));
      const double phasePosition = (sourcePosition - static_cast<double>(centre)) * phases;
      const size_t phase = std::min(static_cast<size_t>(phasePosition), phases - 1);
      const double mix = phasePosition - static_cast<double>(phase);
      const double* first = coefficients.data() + phase * taps;
      const double* second = first + taps;
      const int from = static_cast<int>(std::max(-static_cast<int64_t>(radius), -centre));
      const int to = static_cast<int>(std::min(static_cast<int64_t>(radius), last - centre));
      double value = 0.0;
      for (int offset = from; offset <= to; ++offset) {
        const size_t tap = static_cast<size_t>(offset + radius);
        const double weight = first[tap] + mix * (second[tap] - first[tap]);
        value += samples[static_cast<size_t>(centre + offset)] * weight;
      }
      output[i] = static_cast<float>(value);
    }
    return finite(output) ? output : std::vector<float>{};
  } catch (const std::exception&) {
    return {};
  }
}

Alignment estimateDelay(const std::vector<float>& reference,
                        const std::vector<float>& returned,
                        int maxDelay) {
  if (maxDelay < 0 || maxDelay > sampleRate * 30)
    return alignmentError("Alignment search must be between 0 and 30 seconds");
  if (reference.size() > maxFileSamples || returned.size() > maxFileSamples)
    return alignmentError("Alignment recordings must be 15 minutes or shorter");
  if (!finite(reference) || !finite(returned))
    return alignmentError("Alignment audio contains NaN or infinite samples");
  const float referencePeak = peak(reference);
  if (referencePeak <= 1.0e-6f || peak(returned) <= 1.0e-6f)
    return alignmentError("Cannot align a silent reference or return");
  const auto first = std::find_if(reference.begin(), reference.end(), [referencePeak](float v) {
    return std::abs(v) >= referencePeak * 0.01f;
  });
  if (first == reference.end())
    return alignmentError("No active reference audio");
  const size_t start = static_cast<size_t>(std::distance(reference.begin(), first));
  const size_t count = std::min(reference.size() - start, static_cast<size_t>(sampleRate) * 2);
  if (count < 64)
    return alignmentError("Not enough active reference audio for alignment");
  if (returned.size() < start + count)
    return alignmentError("Return recording is too short for alignment");
  const size_t lags = std::min(static_cast<size_t>(maxDelay), returned.size() - start - count);
  const size_t returnCount = count + lags;
  const int order = fftOrderFor(count + returnCount - 1);
  const size_t size = size_t{1} << order;
  try {
    juce::dsp::FFT fft(order);
    std::vector<float> reversed(size * 2, 0.0f);
    std::vector<float> captured(size * 2, 0.0f);
    for (size_t i = 0; i < count; ++i)
      reversed[i] = reference[start + count - 1 - i];
    std::copy_n(returned.begin() + static_cast<std::ptrdiff_t>(start), returnCount,
                captured.begin());
    fft.performRealOnlyForwardTransform(reversed.data());
    fft.performRealOnlyForwardTransform(captured.data());
    for (size_t bin = 0; bin < size; ++bin) {
      const float ar = captured[2 * bin], ai = captured[2 * bin + 1];
      const float br = reversed[2 * bin], bi = reversed[2 * bin + 1];
      captured[2 * bin] = ar * br - ai * bi;
      captured[2 * bin + 1] = ar * bi + ai * br;
    }
    fft.performRealOnlyInverseTransform(captured.data());

    double referenceEnergy = 0.0;
    double returnEnergy = 0.0;
    for (size_t i = 0; i < count; ++i) {
      const double r = reference[start + i];
      const double y = returned[start + i];
      referenceEnergy += r * r;
      returnEnergy += y * y;
    }
    if (referenceEnergy <= 1.0e-12)
      return alignmentError("The reference window is too quiet");
    size_t bestLag = 0;
    double bestScore = 0.0;
    for (size_t lag = 0; lag <= lags; ++lag) {
      if (returnEnergy > 1.0e-12) {
        const double score = std::abs(static_cast<double>(captured[count - 1 + lag])) /
                             std::sqrt(referenceEnergy * returnEnergy);
        if (score > bestScore) {
          bestScore = score;
          bestLag = lag;
        }
      }
      if (lag < lags) {
        const double added = returned[start + lag + count];
        const double removed = returned[start + lag];
        returnEnergy = std::max(0.0, returnEnergy + added * added - removed * removed);
      }
    }
    // Recompute on the original samples. This is independent of any FFT
    // inverse scaling convention or floating-point spectral round-off.
    double dot = 0.0;
    returnEnergy = 0.0;
    for (size_t i = 0; i < count; ++i) {
      const double r = reference[start + i];
      const double y = returned[start + bestLag + i];
      dot += r * y;
      returnEnergy += y * y;
    }
    const double denominator = std::sqrt(referenceEnergy * returnEnergy);
    const float confidence =
        denominator > 1.0e-12
            ? static_cast<float>(std::clamp(std::abs(dot) / denominator, 0.0, 1.0))
            : 0.0f;
    return {static_cast<int>(bestLag), confidence, {}};
  } catch (const std::exception& e) {
    return alignmentError("Unable to allocate/process alignment: " + juce::String(e.what()));
  }
}

IrResult extractIr(const std::vector<float>& reference,
                   const std::vector<float>& returned,
                   int length) {
  if (reference.size() < 64)
    return irError("The IR sweep is too short");
  if (returned.size() < reference.size())
    return irError("The recording must contain the full IR sweep");
  if (length < 1 || length > sampleRate * 30)
    return irError("IR length must be between one sample and 30 seconds");
  if (returned.size() > maxFftSize)
    return irError("The IR recording is too long to process");
  if (!finite(reference) || !finite(returned))
    return irError("IR audio contains NaN or infinite samples");
  if (peak(reference) <= 1.0e-6f)
    return irError("The reference sweep is silent");
  if (peak(returned) <= 1.0e-6f)
    return irError("No return signal was recorded; check your routing");
  const int order = fftOrderFor(returned.size());
  const size_t size = size_t{1} << order;
  try {
    juce::dsp::FFT fft(order);
    std::vector<float> excitation(size * 2, 0.0f);
    std::vector<float> response(size * 2, 0.0f);
    std::copy(reference.begin(), reference.end(), excitation.begin());
    std::copy(returned.begin(), returned.end(), response.begin());
    fft.performRealOnlyForwardTransform(excitation.data());
    fft.performRealOnlyForwardTransform(response.data());
    float maxPower = 0.0f;
    for (size_t bin = 0; bin < size; ++bin) {
      const float re = excitation[2 * bin], im = excitation[2 * bin + 1];
      maxPower = std::max(maxPower, re * re + im * im);
    }
    if (!std::isfinite(maxPower) || maxPower <= 0.0f)
      return irError("Invalid reference spectrum");
    const float floor = std::max(maxPower * 1.0e-8f, std::numeric_limits<float>::min());
    for (size_t bin = 0; bin < size; ++bin) {
      const float ar = response[2 * bin], ai = response[2 * bin + 1];
      const float br = excitation[2 * bin], bi = excitation[2 * bin + 1];
      const float divisor = br * br + bi * bi + floor;
      response[2 * bin] = (ar * br + ai * bi) / divisor;
      response[2 * bin + 1] = (ai * br - ar * bi) / divisor;
    }
    fft.performRealOnlyInverseTransform(response.data());
    const size_t usable = returned.size();
    const auto end = response.begin() + static_cast<std::ptrdiff_t>(usable);
    if (!std::all_of(response.begin(), end, [](float v) { return std::isfinite(v); }))
      return irError("IR processing produced invalid samples");
    const auto mainPeak = std::max_element(
        response.begin(), end, [](float a, float b) { return std::abs(a) < std::abs(b); });
    if (mainPeak == end || std::abs(*mainPeak) <= 1.0e-9f)
      return irError("The recorded response is too weak to extract an IR");
    const float threshold = std::abs(*mainPeak) * 0.25f;
    const auto onset = std::find_if(response.begin(), mainPeak + 1,
                                    [threshold](float v) { return std::abs(v) >= threshold; });
    const size_t first = static_cast<size_t>(std::distance(response.begin(), onset));
    std::vector<float> ir(static_cast<size_t>(length), 0.0f);
    const size_t available = std::min(ir.size(), usable - first);
    std::copy_n(onset, available, ir.begin());
    const float irPeak = peak(ir);
    if (irPeak <= 1.0e-9f)
      return irError("No usable impulse was found");
    for (float& sample : ir)
      sample *= 0.9f / irPeak;
    return {std::move(ir), {}};
  } catch (const std::exception& e) {
    return irError("Unable to allocate/process IR: " + juce::String(e.what()));
  }
}

}  // namespace t3k::capture
