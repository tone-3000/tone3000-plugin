#include "AutoOffset.h"
#include "StereoOffset.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>

// The lag window is exactly what the Offset knob can correct.
static_assert(AutoOffset::kMaxLagMs == StereoOffsetParams::kMaxOffsetMs,
              "auto-offset must search the range the Offset knob can express");

void AutoOffset::prepare(double newSampleRate) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  capacity = static_cast<int>(std::llround(kMeasureSeconds * sampleRate));
  captureBuffer.setSize(2, capacity);
  stateFlag.store(static_cast<int>(State::Idle), std::memory_order_release);
}

void AutoOffset::start() {
  // Zeroing before the release-store publishes the reset counters together
  // with the state flip (the audio thread only touches them in Listening).
  written.store(0, std::memory_order_relaxed);
  elapsed.store(0, std::memory_order_relaxed);
  stateFlag.store(static_cast<int>(State::Listening), std::memory_order_release);
}

void AutoOffset::cancel() {
  stateFlag.store(static_cast<int>(State::Idle), std::memory_order_release);
}

float AutoOffset::progress() const {
  return capacity > 0
             ? juce::jlimit(0.0f, 1.0f, static_cast<float>(written.load(std::memory_order_relaxed)) /
                                            static_cast<float>(capacity))
             : 0.0f;
}

void AutoOffset::capture(const juce::AudioBuffer<float>& buffer, int numSamples) {
  if (state() != State::Listening || buffer.getNumChannels() < 2)
    return;

  elapsed.fetch_add(numSamples, std::memory_order_relaxed);

  const float* l = buffer.getReadPointer(0);
  const float* r = buffer.getReadPointer(1);

  // Signal gate, same policy as auto-balance: blocks whose loudest channel
  // is below the floor don't count. Gated-out blocks splice the capture, but
  // both channels are cut at identical sample indices, so the relative lag
  // structure survives (only pairs straddling a splice decorrelate — a few
  // samples per splice against a 2 s window).
  double sumL = 0.0, sumR = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    sumL += static_cast<double>(l[i]) * l[i];
    sumR += static_cast<double>(r[i]) * r[i];
  }
  const double blockRms = std::sqrt(std::max(sumL, sumR) / std::max(1, numSamples));

  if (blockRms > kFloorRms) {
    const int have = written.load(std::memory_order_relaxed);
    const int take = std::min(numSamples, capacity - have);
    captureBuffer.copyFrom(0, have, l, take);
    captureBuffer.copyFrom(1, have, r, take);
    written.store(have + take, std::memory_order_relaxed);

    if (have + take >= capacity) {
      stateFlag.store(static_cast<int>(State::Captured), std::memory_order_release);
      return;
    }
  }

  if (elapsed.load(std::memory_order_relaxed) >
      static_cast<juce::int64>(kTimeoutSeconds * sampleRate))
    stateFlag.store(static_cast<int>(State::TimedOut), std::memory_order_release);
}

AutoOffset::Result AutoOffset::analyze() {
  Result result;
  if (state() != State::Captured) {
    stateFlag.store(static_cast<int>(State::Idle), std::memory_order_release);
    return result;
  }

  const int n = written.load(std::memory_order_relaxed);
  const int maxLag = static_cast<int>(std::ceil(kMaxLagMs * 0.001 * sampleRate));
  const float* l = captureBuffer.getReadPointer(0);
  const float* r = captureBuffer.getReadPointer(1);

  // Circular cross-correlation via FFT: c = IFFT(FFT(L) · conj(FFT(R))),
  // where c[k] = Σₙ L[n]·R[n−k]. A peak at positive k means L is a delayed
  // copy of R (the left chain lags) → delay the right chain → positive ms,
  // matching the StereoOffset sign convention. Zero-padding the FFT past
  // n + maxLag keeps the wrap-around out of the searched window; negative
  // lags live at indices fftSize − k. One-shot on the message thread, so
  // allocating here is fine.
  const int fftOrder = static_cast<int>(std::ceil(std::log2(std::max(2, n + maxLag))));
  const int fftSize = 1 << fftOrder;
  juce::dsp::FFT fft(fftOrder);

  std::vector<float> specL(static_cast<size_t>(fftSize) * 2, 0.0f);
  std::vector<float> specR(static_cast<size_t>(fftSize) * 2, 0.0f);
  std::copy(l, l + n, specL.begin());
  std::copy(r, r + n, specR.begin());
  fft.performRealOnlyForwardTransform(specL.data());
  fft.performRealOnlyForwardTransform(specR.data());

  // In-place complex multiply specL · conj(specR), interleaved re/im bins.
  for (int bin = 0; bin < fftSize; ++bin) {
    const float ar = specL[static_cast<size_t>(2 * bin)];
    const float ai = specL[static_cast<size_t>(2 * bin) + 1];
    const float br = specR[static_cast<size_t>(2 * bin)];
    const float bi = specR[static_cast<size_t>(2 * bin) + 1];
    specL[static_cast<size_t>(2 * bin)] = ar * br + ai * bi;
    specL[static_cast<size_t>(2 * bin) + 1] = ai * br - ar * bi;
  }
  fft.performRealOnlyInverseTransform(specL.data());

  int bestLag = 0;
  float bestCorr = specL[0];
  for (int lag = 1; lag <= maxLag; ++lag) {
    if (specL[static_cast<size_t>(lag)] > bestCorr) {
      bestCorr = specL[static_cast<size_t>(lag)];
      bestLag = lag;
    }
    if (specL[static_cast<size_t>(fftSize - lag)] > bestCorr) {
      bestCorr = specL[static_cast<size_t>(fftSize - lag)];
      bestLag = -lag;
    }
  }

  // Confidence: normalized correlation at the winning lag, recomputed in the
  // time domain (immune to FFT scaling). 1 = identical up to gain and shift.
  double dot = 0.0, energyL = 0.0, energyR = 0.0;
  const int from = std::max(0, bestLag);
  const int to = std::min(n, n + bestLag);
  for (int i = from; i < to; ++i) {
    dot += static_cast<double>(l[i]) * r[i - bestLag];
    energyL += static_cast<double>(l[i]) * l[i];
    energyR += static_cast<double>(r[i - bestLag]) * r[i - bestLag];
  }
  const double denom = std::sqrt(std::max(energyL * energyR, 1.0e-24));

  result.offsetMs = juce::jlimit(-kMaxLagMs, kMaxLagMs,
                                 static_cast<float>(bestLag * 1000.0 / sampleRate));
  result.confidence = juce::jlimit(0.0f, 1.0f, static_cast<float>(dot / denom));

  stateFlag.store(static_cast<int>(State::Idle), std::memory_order_release);
  return result;
}
