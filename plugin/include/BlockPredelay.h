#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

/**
 * Predelay for IR blocks: delays the wet signal before it reaches the
 * convolver, so the IR's tail starts later without touching the loaded
 * impulse response itself (padding the IR's own buffer would force a full
 * convolver rebuild — with its ~150ms silent install fade — on every live
 * change, and would inflate the uniform engine's kernel-length-bound CPU
 * cost and its short/long classification).
 *
 * Always runs at kChainBaseSampleRate, called from inside the block's
 * base-rate island (see ChainOversampler) right before
 * juce::dsp::Convolution::process() — the same rate convolution itself
 * always runs at, regardless of the chain's oversampling factor.
 *
 * Threading model, mirrors BlockEq: prepare()/setDelayMs() run on the
 * message thread (under chainMutex); process() runs on the audio thread
 * and performs zero allocation. The ring buffer is sized once in
 * prepare() for the maximum delay (kMaxDelayMs) and never resized.
 *
 * Click-free live changes: setDelayMs() ramps a *smoothed delay time*
 * (LinearSmoothedValue<float>, in fractional samples, over kRampSeconds)
 * rather than jumping the read offset directly, and process() reads the
 * ring buffer at that fractional position with linear interpolation every
 * sample — so the delay itself glides continuously and there is no
 * stepping artifact mid-ramp. A large/fast change is still audible as a
 * brief pitch/doppler bend (inherent to any live-changing delay line, not
 * a bug) rather than a click. kRampSeconds was picked by ear, A/B-ing fast
 * slider drags and large jumps against the alternative (padding the loaded
 * IR, rejected — see the note above on why); 10ms proved too short in
 * practice (audible noise on UI knob drags) and was widened to 300ms.
 *
 * Structural changes (block (re)prepare, state restore) go through
 * prepare()'s initialMs argument and snap directly, no ramp: there is no
 * live signal continuity to protect at that point, and ramping in from 0
 * on every host resample/oversampling change would itself be audible.
 */
class BlockPredelay {
public:
  static constexpr float kMaxDelayMs = 1000.0f;
  // Live-change ramp time, picked by ear (see class comment).
  static constexpr float kRampSeconds = 0.3f;

  /** Message thread. (Re)allocates the ring buffer for kMaxDelayMs at
      `sampleRate` and snaps directly to `initialMs` (no ramp, no
      allocation on any later call). */
  void prepare(double sampleRate, float initialMs);

  /** Message thread (under chainMutex). Ramps toward `ms` over
      kRampSeconds; clamped to [0, kMaxDelayMs]. */
  void setDelayMs(float ms);

  /** Audio thread. Processes up to 2 channels in place; zero allocation.
      Always call for a loaded IR block (no flat-skip/isActive() gate):
      the per-sample cost is one ring write + one linearly-interpolated
      read per channel, trivial next to the convolution it feeds, and
      skipping it based on the target being 0 would risk truncating an
      in-flight downward ramp's still-delayed tail. */
  void process(juce::AudioBuffer<float>& buffer);

private:
  std::array<std::vector<float>, 2> ring;
  size_t capacity{0};
  size_t writePos{0};
  double sampleRate{48000.0};
  juce::LinearSmoothedValue<float> delaySamplesSmoothed;
};
