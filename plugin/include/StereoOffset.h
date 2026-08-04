#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/**
 * Stereo-mode corrective offset: a short static delay applied to one chain,
 * in place, purely for time-aligning the two chains (e.g. captures of the
 * same performance that land a few ms apart). Stereo chain mode only; mono
 * mode has Spread instead (see Spread.h), a separate feature with its own
 * parameters.
 *
 * Control (see StereoOffsetParams for the normalized encoding): one bipolar
 * knob. Center = 0 ms; left of center delays the left chain, right of center
 * the right chain, up to ±24 ms.
 *
 * Lifecycle: while the power switch is on the engine always runs (a 0 ms
 * delay is identity, and the per-sample cost is one delay push/pop on one
 * channel), so knob moves never hard-enable/disable DSP. Turning the power
 * off glides the delay to zero first and only then goes idle; crossing the
 * knob through center glides to zero, swaps the delayed channel, and glides
 * back up. Every transition passes through 0 ms, which is why none of them
 * click: at zero delay the output equals the input, sample for sample.
 *
 * The delay time ramps through a SmoothedValue into a Lagrange-interpolated
 * DelayLine (4-point cubic: a static fractional delay through linear
 * interpolation would carry a fixed HF droop, wrong for a corrective tool).
 * The ramp (kRampSeconds) is chosen so the delay never grows faster than
 * real time; after a line clear, reads can never land on unwritten (stale
 * or zero) samples.
 *
 * Audio thread only; zero allocation after prepare().
 */

/** Decoded offset parameters. Normalized knob values map here in exactly one
    place so the DSP and any UI readouts agree. */
struct StereoOffsetParams {
  static constexpr float kMaxOffsetMs = 24.0f;

  int targetChannel = 1;  // channel that gets delayed (0 = left, 1 = right)
  float offsetMs = 0.0f;

  /** offsetNorm: bipolar 0..1, 0.5 = center = 0 ms. Values within a hair of
      center decode to exactly zero so the knob detent genuinely means zero. */
  static StereoOffsetParams fromNormalized(float offsetNorm) {
    constexpr float kEps = 0.005f;
    StereoOffsetParams p;
    const float bipolar = juce::jlimit(0.0f, 1.0f, offsetNorm) * 2.0f - 1.0f;
    p.targetChannel = bipolar < 0.0f ? 0 : 1;
    const float amount = std::abs(bipolar);
    p.offsetMs = amount < kEps ? 0.0f : amount * kMaxOffsetMs;
    return p;
  }
};

class StereoOffset {
public:
  void prepare(double sampleRate, int maxBlockSize);

  /** Per-block parameter update. `engaged` is the power switch: turning it
      off starts a glide-out (isRunning() stays true until the delay lands on
      zero); turning it on from idle starts clean at 0 ms. */
  void setTarget(const StereoOffsetParams& params, bool engaged);

  /** True while the engine needs process() this block. False = fully idle,
      safe to skip. */
  bool isRunning() const { return running; }

  /** Immediate hard stop, no glide. Only safe while the bus is already
      silent; the chain-edit fade covers mono/stereo mode switches, which is
      the one caller. */
  void forceIdle() { running = false; engaged = false; }

  /** Requires >= 2 channels: delays the current offset channel in place.
      Completes pending glide-out transitions (side swap / idle). */
  void process(juce::AudioBuffer<float>& buffer);

private:
  void retargetDelay();

  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
  juce::SmoothedValue<float> delaySamples;

  bool running{false};
  bool engaged{false};
  int currentChannel{1};  // side being delayed right now
  int desiredChannel{1};  // side the knob asks for (adopted via a zero glide)
  float offsetMs{0.0f};

  double sampleRate{48000.0};

  // Delay ramp: longer than the max delay swing (24 ms) so delay time never
  // grows faster than real time; see class comment.
  static constexpr double kRampSeconds = 0.040;
};
