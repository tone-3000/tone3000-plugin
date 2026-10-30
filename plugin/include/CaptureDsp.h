#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace t3k::capture {

constexpr int sampleRate = 48000;

/** IR-only exponential sine sweep, 20 Hz to 20 kHz, with 10 ms edge fades.
    NAM capture must use the unmodified official TONE3000 training file.
    Invalid duration/level returns an empty vector. Duration is limited to
    120 seconds; levels above 0 dB are clamped to 0 dB. */
std::vector<float> makeSweep(double seconds, float levelDb = 0.0f);

/** Offline, band-limited sample-rate conversion with zero padding outside
    the recording. A centered windowed-sinc kernel introduces no causal
    filter delay: output sample n is anchored to input n*sourceRate/destRate.
    Length is exactly round(input length * destRate / sourceRate). No gain
    normalization or independent time shift is applied to the audio. Use the
    SAME conversion for both tracks of a dry/wet pair. Same-rate finite input
    is copied bit-exactly. Invalid/nonfinite data or rates, output longer than
    15 minutes at 48 kHz, or allocation failure returns an empty vector.
    This is an offline worker operation, never an audio callback operation. */
std::vector<float> resample(const std::vector<float>& samples, double sourceRate, double destRate);

struct Alignment {
  int offset = 0;
  float confidence = 0.0f;
  juce::String error;
};

/** Nonnegative return delay in samples, using up to two seconds of active
    reference audio and an FFT correlation search. Confidence is absolute
    normalized correlation, 0..1; inverted signals are valid. No input gain,
    samples or timing are modified. Maximum search is 30 seconds.
    A caller must reject weak/ambiguous measurements or request manual
    alignment; confidence alone does not prove alignment of periodic audio. */
Alignment estimateDelay(const std::vector<float>& reference,
                        const std::vector<float>& returned,
                        int maxDelay);

struct IrResult {
  std::vector<float> samples;
  juce::String error;
};

/** Linear IR recovery by regularized spectral division. Removes transport
    AND acoustic arrival delay using the first significant onset (25% of
    peak), then normalizes the exported IR peak to 0.9. Weak early arrivals
    can be omitted. Absolute gain and pre-arrival delay are intentionally
    lost. This cannot model nonlinear or time-varying effects.
    The return must contain the whole sweep and its tail. Output is padded
    or truncated to length (1 sample to 30 seconds). Not for NAM audio. */
IrResult extractIr(const std::vector<float>& reference,
                   const std::vector<float>& returned,
                   int length);

}  // namespace t3k::capture
