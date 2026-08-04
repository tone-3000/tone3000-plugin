#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

/**
 * Spread: a mono-to-stereo ADT-style doubler (mono chain mode only; see
 * plugin/docs/spread.md for the design overview).
 *
 * The mono chain output is split at 130 Hz (Linkwitz-Riley 4th order). The
 * low band feeds both channels untouched, mono-safe by construction. The
 * high band goes dry to one channel and through the "lag deck", a wobbling
 * fractional delay (4-point Lagrange, mandatory: linear interpolation under a
 * time-varying fractional offset low-passes in rhythm with the wobble) plus a
 * static six-stage allpass decorrelation cascade and a +1.5 dB precedence
 * trim, to the other. The wobble (random-walk modulation of the delay time:
 * white noise through two cascaded 0.3 Hz one-poles) is what makes the lag
 * read as a second human performance, and it keeps the mono-sum comb moving
 * so fold-down reads as gentle chorus, never a stationary notch.
 *
 * The allpass cascade decorrelates phase without touching magnitude, the
 * same principle as the allpass decorrelators evaluated in O. Das, "An
 * Open-Source Stereo Widening Plugin", Proc. 27th Int. Conf. on Digital
 * Audio Effects (DAFx24), Guildford, UK, 2024
 * (https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf).
 *
 * The single musical control is a signed Offset in ms (±24): the sign picks
 * the lagged channel ("knob points at the fake one"; precedence pulls the
 * image toward the dry side), the magnitude the base delay. The signed value
 * runs through one ~100 ms one-pole so sweeps across center pass cleanly
 * through identity instead of leaving decaying lag on the old side, and
 * knob moves read as a tape-style varispeed glide.
 *
 * Wobble depth is absolute, not relative to the offset: 100% = ±1.2 ms of
 * slow drift around the dialed offset (≈ ±2-4 cents of continuous pitch
 * wander; pitch shift is the derivative of delay time). The normalization
 * of the filtered noise is computed analytically in prepare() (see there).
 *
 * Center identity: at T = 0 the lag deck's output still differs from dry
 * (the allpass cascade and lag gain stay engaged), but the center detent
 * must be exactly dual-mono. So I blend the lag path back to the dry high
 * band as |t| falls below kCenterBlendMs, which keeps sweeps continuous
 * (the tape-flange zone effectively starts around 1 ms).
 *
 * Engage/bypass is a ~25 ms equal-gain crossfade between the untouched input
 * and the doubled image: unlike the stereo offset there is no glide-through-
 * zero trick available, because even at 0 ms the deck's output differs from
 * the input (LR4 recombination is allpass-flat, not identity). While the
 * switch is on the deck always runs at full strength: there is no wet/dry;
 * Offset is the only musical dimension.
 *
 * Correlation: process() keeps a ~300 ms running normalized L/R
 * cross-correlation of its output, published through an atomic for the UI
 * meter. Normalized correlation is invariant to the per-channel balance
 * gains applied downstream, so measuring here equals measuring at the bus.
 *
 * Audio thread only (the correlation atomic is read by the UI thread); zero
 * allocation after prepare().
 */

/** Decoded spread parameters. Normalized knob values map here in exactly
    one place so the DSP and any UI readouts agree. */
struct SpreadParams {
  // Same span as the stereo-mode corrective Offset (StereoOffsetParams) so
  // the two faces of the stereo-image slot read identically.
  static constexpr float kMaxOffsetMs = 24.0f;

  float offsetMs = 0.0f;     // signed; > 0 lags the right channel
  float wobbleDepth = 0.25f;  // 0..1 of the ±1.2 ms wobble range

  /** offsetNorm: bipolar 0..1, 0.5 = center = 0 ms. wobbleNorm: 0..1. Values
      within a hair of center decode to exactly zero so the knob detent
      genuinely means zero. */
  static SpreadParams fromNormalized(float offsetNorm, float wobbleNorm) {
    constexpr float kEps = 0.005f;
    SpreadParams p;
    const float bipolar = juce::jlimit(0.0f, 1.0f, offsetNorm) * 2.0f - 1.0f;
    p.offsetMs = std::abs(bipolar) < kEps ? 0.0f : bipolar * kMaxOffsetMs;
    p.wobbleDepth = juce::jlimit(0.0f, 1.0f, wobbleNorm);
    return p;
  }
};

class Spread {
public:
  void prepare(double sampleRate, int maxBlockSize);

  /** Per-block parameter update. `engaged` is the power switch: turning it
      off starts the fade-out (isRunning() stays true until it lands);
      turning it on from idle resets the deck and fades in. */
  void setTarget(const SpreadParams& params, bool engaged);

  /** True while the engine needs process() this block. False = fully idle,
      safe to skip. */
  bool isRunning() const { return running; }

  /** Immediate hard stop, no fade. Only safe while the bus is already
      silent; the chain-edit fade covers mono/stereo mode switches, which is
      the one caller. */
  void forceIdle();

  /** Requires >= 2 channels. The deck is seeded from channel 0 (the mono
      chain output); the engage/bypass crossfade endpoints are each channel's
      own untouched signal, so with a true stereo source in mono chain mode
      (where the channels differ) the fade lands exactly on the input.
      Writes the stereo image in place. */
  void process(juce::AudioBuffer<float>& buffer);

  /** Latest output correlation (-1..1) for the UI meter; 1 when idle or on
      silence. Readable from any thread. */
  float correlation() const { return correlationOut.load(std::memory_order_relaxed); }

private:
  void resetDeck();

  /** First-order allpass (transposed direct form II, one state). Static
      coefficients: movement comes from the delay wobble; modulating allpass
      coefficients would reintroduce phasiness. */
  struct Allpass {
    float a = 0.0f;
    float z = 0.0f;
    float process(float x) noexcept {
      const float v = x - a * z;
      const float y = a * v + z;
      z = v;
      return y;
    }
  };

  // Fixed design values (rationale in plugin/docs/spread.md).
  static constexpr double kCrossoverHz = 130.0;   // locks low E dual-mono
  static constexpr int kNumAllpasses = 6;
  static constexpr double kAllpassLowHz = 300.0;  // cascade fc log-spaced
  static constexpr double kAllpassHighHz = 6000.0;
  static constexpr float kLagGainDb = 1.5f;       // precedence patch, not cure
  static constexpr double kWobbleRateHz = 0.3;    // random-walk LPF corner
  static constexpr float kWobbleMaxMs = 1.2f;     // ≈ ±2-4 cents drift at 100%
  static constexpr double kOffsetSmoothSeconds = 0.1;  // signed-value one-pole
  static constexpr float kCenterBlendMs = 1.0f;   // lag→ref blend below this
  static constexpr double kFadeSeconds = 0.025;   // engage/bypass crossfade
  static constexpr double kCorrSeconds = 0.3;     // correlation window
  // Mean-square floor (~-100 dBFS) below which correlation reports 1:
  // silence is trivially mono-compatible, not "decorrelated".
  static constexpr float kCorrFloor = 1.0e-10f;

  juce::dsp::LinkwitzRileyFilter<float> crossover;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
  std::array<Allpass, kNumAllpasses> allpasses;

  bool running{false};
  bool engaged{false};

  float targetOffsetMs{0.0f};
  float offsetStateMs{0.0f};  // smoothed SIGNED offset (sign = lagged side)
  float offsetCoeff{0.0f};

  // Two cascaded one-poles (12 dB/oct). One pole is not enough: its 6 dB/oct
  // tail leaves ~1% of the noise variance above 20 Hz, and audio-rate
  // delay-time noise FMs the lag channel into broadband fizz (regression
  // covered by SpreadTest.WobbleAddsNoBroadbandFizz).
  float wobbleState1{0.0f};
  float wobbleState2{0.0f};
  float wobbleCoeff{0.0f};
  float wobbleNorm{1.0f};  // analytic ~unit-peak normalization, see prepare()
  juce::Random random;
  juce::LinearSmoothedValue<float> wobbleDepth;

  // Engage/bypass crossfade: 0 = untouched input, 1 = doubled image.
  juce::LinearSmoothedValue<float> wetGain;

  // Correlation followers (one-pole means of L·R, L², R²) + published value.
  float corrLR{0.0f}, corrLL{0.0f}, corrRR{0.0f};
  std::atomic<float> correlationOut{1.0f};

  double sampleRate{48000.0};
  float msToSamples{48.0f};
  float lagGain{1.0f};
};
