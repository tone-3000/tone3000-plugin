#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/**
 * Short per-note delay engine behind two mode-exclusive features:
 *
 * - Mono doubler (sourceChannel 0): replaces the right channel with a
 *   delayed copy of the left post-chain signal — a classic slap double.
 * - Stereo offset (sourceChannel 1): delays the right chain in place,
 *   shifting it against the left chain for width.
 *
 * Two controls:
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
      bypass — or switching modes — so stale audio never leaks out). */
  void reset();

  /** Per-block parameter update, normalized 0..1 (knob values). */
  void setParams(float spreadNorm, float jitterNorm);

  /** Requires >= 2 channels: overwrites ch 1 with a delayed copy of
      `sourceChannel` (0 = mono double, 1 = in-place stereo offset). Onsets
      are detected on the source signal. */
  void process(juce::AudioBuffer<float>& buffer, int sourceChannel);

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
