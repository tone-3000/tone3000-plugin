#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/**
 * Mono-chain stereo doubler. Replaces the right channel with a short delayed
 * copy of the left (post-chain) signal — a classic slap double. Two controls:
 *
 * - spread: base delay in ms (0..kMaxSpreadMs).
 * - jitter: ± range of random per-note variation. A lightweight envelope
 *   onset detector re-rolls the offset at each note/chord attack, so every
 *   hit lands with a slightly different double — like a real second take.
 *
 * Delay-time changes ramp through a SmoothedValue into a linear-interpolated
 * DelayLine, so retargets glide (brief, tape-like pitch bend) instead of
 * zipping or clicking.
 *
 * Audio thread only; process() does zero allocation. Cost when active is one
 * delay-line push/pop plus two one-pole envelopes per sample, on one channel.
 * When bypassed the processor skips the call entirely (single branch).
 */
class Doubler {
public:
  static constexpr float kMaxSpreadMs = 24.0f;
  static constexpr float kMaxJitterMs = 12.0f;

  void prepare(double sampleRate, int maxBlockSize);

  /** Clear delay contents + detector state (call when re-engaging after
      bypass so stale audio never leaks out of the delay line). */
  void reset();

  /** Per-block parameter update, normalized 0..1 (knob values). */
  void setParams(float spreadNorm, float jitterNorm);

  /** Requires >= 2 channels: reads ch 0, overwrites ch 1 with the double. */
  void process(juce::AudioBuffer<float>& buffer);

private:
  void retargetDelay();

  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
  juce::SmoothedValue<float> delaySamples;

  float spreadMs{12.0f};
  float jitterMs{3.0f};
  float jitterOffsetMs{0.0f};

  // Onset detector: fast envelope crossing above the slow envelope by
  // kOnsetRatio marks an attack; a refractory hold keeps one retrigger per
  // note/chord instead of one per string.
  float envFast{0.0f};
  float envSlow{0.0f};
  int refractorySamplesLeft{0};

  double sampleRate{48000.0};
  float fastAttack{0.0f}, fastRelease{0.0f};
  float slowCoeff{0.0f};
  int refractorySamples{0};

  juce::Random random;

  static constexpr float kOnsetRatio = 2.0f;
  static constexpr float kOnsetFloor = 0.003f;  // ~-50 dB: ignore noise-floor "onsets"
};
