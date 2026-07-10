#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/**
 * Stereo spread: a short per-note delay applied to one channel, shared by
 * both modes (they're mutually exclusive, so one engine + one parameter set
 * serves both):
 *
 * - Mono mode: the chain output is doubled onto both channels and the spread
 *   side is delayed — a classic slap double.
 * - Stereo mode: the spread side's chain is delayed in place, shifting it
 *   against the other chain for width.
 *
 * Controls (see SpreadParams for the normalized encoding):
 * - spread: bipolar knob. Center = 0 ms; left of center delays the left
 *   channel, right of center the right channel.
 * - jitter: ± range of random per-note variation, re-rolled at each detected
 *   note/chord attack so every hit lands like a slightly different take.
 *
 * Lifecycle: while the power switch is on the engine always runs (a 0 ms
 * delay is identity, and the per-sample cost is one delay push/pop on one
 * channel), so knob moves never hard-enable/disable DSP. Turning the power
 * off glides the delay to zero first and only then goes idle; crossing the
 * knob through center glides to zero, swaps the delayed channel, and glides
 * back up. Every transition passes through 0 ms, which is why none of them
 * click: at zero delay the output equals the input, sample for sample.
 *
 * The delay time ramps through a SmoothedValue into a linear-interpolated
 * DelayLine. The ramp (kRampSeconds) is chosen so the delay never grows
 * faster than real time — after a line clear, reads can never land on
 * unwritten (stale or zero) samples — and jitter retargets read as a subtle
 * tape-like bend rather than a pitch jerk.
 *
 * Onset detection runs on the *dry pre-chain input* (analyzeOnsets), not the
 * processed output: amps compress pick attacks into mush and their noise
 * floor fakes constant "onsets". The detector is a classic fast/slow
 * envelope crossing with two guards against machine-gunning on sustained or
 * compressed material: a refractory hold after each trigger, plus a re-arm
 * hysteresis — no new trigger until the fast envelope has dipped back near
 * the slow one (i.e. the previous note actually decayed or was muted).
 *
 * Audio thread only; zero allocation after prepare().
 */

/** Decoded spread parameters. Normalized knob values map here in exactly one
    place so the DSP and any future UI readouts agree. */
struct SpreadParams {
  static constexpr float kMaxSpreadMs = 24.0f;
  // ±4 ms: enough to hear a "different take" randomness without ever
  // crossing into chorus/vibrato territory (studios typically land 1–4 ms).
  static constexpr float kMaxJitterMs = 4.0f;

  int targetChannel = 1;  // channel that gets delayed (0 = left, 1 = right)
  float spreadMs = 0.0f;
  float jitterMs = 0.0f;

  /** amountNorm: bipolar 0..1, 0.5 = center = 0 ms. jitterNorm: 0..1.
      Values within a hair of zero decode to exactly zero so knob detents
      genuinely mean zero. */
  static SpreadParams fromNormalized(float amountNorm, float jitterNorm) {
    constexpr float kEps = 0.005f;
    SpreadParams p;
    const float bipolar = juce::jlimit(0.0f, 1.0f, amountNorm) * 2.0f - 1.0f;
    p.targetChannel = bipolar < 0.0f ? 0 : 1;
    const float amount = std::abs(bipolar);
    p.spreadMs = amount < kEps ? 0.0f : amount * kMaxSpreadMs;
    const float jitter = juce::jlimit(0.0f, 1.0f, jitterNorm);
    p.jitterMs = jitter < kEps ? 0.0f : jitter * kMaxJitterMs;
    return p;
  }
};

class Spread {
public:
  void prepare(double sampleRate, int maxBlockSize);

  /** Per-block parameter update. `engaged` is the power switch: turning it
      off starts a glide-out (isRunning() stays true until the delay lands on
      zero); turning it on from idle starts clean at 0 ms. */
  void setTarget(const SpreadParams& params, bool engaged);

  /** True while the engine needs analyzeOnsets() + process() this block.
      False = fully idle, safe to skip both. */
  bool isRunning() const { return running; }

  /** Feed the dry pre-chain input (channel 0) to the onset detector; an
      attack re-rolls the jitter offset. Call before the chain overwrites the
      buffer. No-op while jitter is zero (the followers still track so the
      detector state is warm when jitter comes up). */
  void analyzeOnsets(const float* dryInput, int numSamples);

  /** Requires >= 2 channels: delays the current spread channel in place.
      Completes pending glide-out transitions (side swap / idle). */
  void process(juce::AudioBuffer<float>& buffer);

private:
  void retargetDelay();
  void resetDetector();

  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
  juce::SmoothedValue<float> delaySamples;

  bool running{false};
  bool engaged{false};
  int currentChannel{1};  // side being delayed right now
  int desiredChannel{1};  // side the knob asks for (adopted via a zero glide)
  float spreadMs{0.0f};
  float jitterMs{0.0f};
  float jitterOffsetMs{0.0f};

  // Onset detector state (see class comment).
  float envFast{0.0f};
  float envSlow{0.0f};
  bool armed{true};
  int refractorySamplesLeft{0};

  double sampleRate{48000.0};
  float fastAttack{0.0f}, fastRelease{0.0f};
  float slowCoeff{0.0f};
  int refractorySamples{0};

  juce::Random random;

  // Delay ramp: longer than the max delay swing (36 ms) so delay time never
  // grows faster than real time — see class comment.
  static constexpr double kRampSeconds = 0.040;
  static constexpr float kOnsetRatio = 2.0f;   // fast must exceed slow by 2x to fire
  static constexpr float kRearmRatio = 1.2f;   // and dip back under 1.2x to re-arm
  static constexpr float kOnsetFloor = 0.003f; // ~-50 dB: ignore noise-floor "onsets"
};
