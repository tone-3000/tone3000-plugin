#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

/**
 * Zero-latency one-knob noise gate for the input stage.
 *
 * The only user control is the threshold (the faceplate Gate knob); every
 * other behaviour is fixed, tuned for guitar/bass sources:
 *
 *  - Sidechain detector: gating decisions are made from a band-passed copy
 *    of the signal (12 dB/oct highpass at 80 Hz, 6 dB/oct lowpass at 5 kHz)
 *    so mains hum can't hold the gate open and broadband hiss can't trigger
 *    it. The audible path is never filtered.
 *  - Peak envelope follower with a fast rise and a slower fall, so the
 *    detector reacts to pick attacks instantly but rides note decay smoothly
 *    instead of chattering on waveform ripple.
 *  - Hysteresis + hold: the gate opens at the threshold but only starts
 *    closing once the envelope has spent the whole hold time below a second
 *    threshold 5 dB lower. Palm mutes and staccato playing can hover around
 *    the knob setting without open/close flutter.
 *  - Downward-expander close: a closing gate never slams to silence. The
 *    target gain tracks the decaying envelope cubically (a 4:1 expander
 *    curve) down to a -80 dB floor, so note tails fade naturally instead of
 *    being cut. The same curve eases the gate open as the envelope
 *    approaches the threshold from below - a soft knee for free.
 *  - Gain smoothing: ~0.2 ms attack and ~100 ms release one-pole ramps.
 *    The near-instant attack is what makes zero lookahead viable: the gate
 *    is fully open before a pick transient develops.
 *
 * No lookahead and no internal buffering - zero added latency, safe for
 * live monitoring. Channels (up to 2) are fully independent: in stereo mode
 * the two lanes may carry different instruments, and for mono/duplicated
 * sources identical inputs produce identical gains anyway.
 *
 * Threading: prepare()/reset() from prepareToPlay, setThresholdDb() and
 * process() from the audio thread. process() allocates nothing and costs a
 * handful of multiplies per sample. Denormals are handled by the caller's
 * ScopedNoDenormals.
 */
class NoiseGate {
public:
  static constexpr int kMaxChannels = 2;

  void prepare(double newSampleRate) {
    sampleRate = newSampleRate;

    detectorRiseCoeff = onePoleCoeff(kDetectorRiseMs);
    detectorFallCoeff = onePoleCoeff(kDetectorFallMs);
    attackCoeff = onePoleCoeff(kAttackMs);
    releaseCoeff = onePoleCoeff(kReleaseMs);
    holdSamples = static_cast<int>(kHoldMs * 0.001 * sampleRate);

    // Sidechain highpass: TPT state-variable filter (Zavalishin), Butterworth Q.
    const float g =
        std::tan(juce::MathConstants<float>::pi * static_cast<float>(kHighpassHz / sampleRate));
    const float k = juce::MathConstants<float>::sqrt2;  // 1/Q
    svfK = k;
    svfA1 = 1.0f / (1.0f + g * (g + k));
    svfA2 = g * svfA1;
    svfA3 = g * svfA2;

    // Sidechain lowpass: one pole.
    lowpassCoeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi *
                                   static_cast<float>(kLowpassHz / sampleRate));

    reset();
  }

  /** Clears all envelope/filter/gain state (gate starts closed). Also call on
      the enabled->disabled->enabled transition so a stale envelope never
      decides the first block. */
  void reset() {
    for (auto& channel : channels)
      channel = {};
  }

  /** Audio thread, once per block. The knob value is the open threshold; the
      close threshold sits kHysteresisDb below it. Early-outs when unchanged,
      so the transcendentals only run on actual knob moves. */
  void setThresholdDb(float thresholdDb) {
    if (thresholdDb == currentThresholdDb)
      return;
    currentThresholdDb = thresholdDb;
    openThreshold = juce::Decibels::decibelsToGain(thresholdDb);
    closeThreshold = juce::Decibels::decibelsToGain(thresholdDb - kHysteresisDb);
    invOpenThreshold = 1.0f / openThreshold;
  }

  /** Audio thread. Gates up to kMaxChannels in place. */
  void process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    for (int ch = 0; ch < numChannels; ++ch)
      processChannel(channels[static_cast<size_t>(ch)], buffer.getWritePointer(ch), numSamples);
  }

private:
  // ── Fixed tuning (guitar/bass-voiced) ──
  static constexpr double kHighpassHz = 80.0;    // below: mains hum, rumble
  static constexpr double kLowpassHz = 5000.0;   // above: hiss, no fundamentals
  static constexpr float kDetectorRiseMs = 0.2f;
  static constexpr float kDetectorFallMs = 25.0f;
  static constexpr float kAttackMs = 0.2f;
  static constexpr float kHoldMs = 50.0f;
  static constexpr float kReleaseMs = 100.0f;
  static constexpr float kHysteresisDb = 5.0f;
  static constexpr float kFloorGain = 1.0e-4f;   // -80 dB: silent, never a hard zero

  enum class State { closed, open, holding };

  struct Channel {
    float svfIc1 = 0.0f, svfIc2 = 0.0f;  // sidechain SVF integrator state
    float lowpassState = 0.0f;
    float envelope = 0.0f;
    float gain = 0.0f;
    int holdCounter = 0;
    State state = State::closed;
  };

  void processChannel(Channel& c, float* samples, int numSamples) const noexcept {
    for (int i = 0; i < numSamples; ++i) {
      const float x = samples[i];

      // Sidechain band-pass (detector only; the audible path stays untouched).
      const float v3 = x - c.svfIc2;
      const float v1 = svfA1 * c.svfIc1 + svfA2 * v3;
      const float v2 = c.svfIc2 + svfA2 * c.svfIc1 + svfA3 * v3;
      c.svfIc1 = 2.0f * v1 - c.svfIc1;
      c.svfIc2 = 2.0f * v2 - c.svfIc2;
      const float highpassed = x - svfK * v1 - v2;
      c.lowpassState += lowpassCoeff * (highpassed - c.lowpassState);

      // Peak envelope: fast rise, slow fall.
      const float rectified = std::abs(c.lowpassState);
      c.envelope += (rectified > c.envelope ? detectorRiseCoeff : detectorFallCoeff) *
                    (rectified - c.envelope);

      // Hysteresis + hold state machine.
      switch (c.state) {
        case State::closed:
          if (c.envelope >= openThreshold)
            c.state = State::open;
          break;
        case State::open:
          if (c.envelope < closeThreshold) {
            c.state = State::holding;
            c.holdCounter = holdSamples;
          }
          break;
        case State::holding:
          if (c.envelope >= closeThreshold)
            c.state = State::open;  // tail came back up: no re-trigger penalty
          else if (--c.holdCounter <= 0)
            c.state = State::closed;
          break;
      }

      // Target gain: unity while open/holding; a cubic downward-expander
      // curve of the envelope while closed (env < openThreshold, so the
      // ratio is < 1 and the cube only ever attenuates).
      float targetGain = 1.0f;
      if (c.state == State::closed) {
        const float ratio = c.envelope * invOpenThreshold;
        targetGain = juce::jmax(ratio * ratio * ratio, kFloorGain);
      }

      // Smooth the gain itself so opening never clicks and closing breathes.
      c.gain += (targetGain > c.gain ? attackCoeff : releaseCoeff) * (targetGain - c.gain);
      samples[i] = x * c.gain;
    }
  }

  float onePoleCoeff(float ms) const {
    if (ms <= 0.0f || sampleRate <= 0.0)
      return 1.0f;
    return 1.0f - std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate)));
  }

  double sampleRate = 48000.0;

  // Detector/gain coefficients (recomputed in prepare()).
  float detectorRiseCoeff = 1.0f, detectorFallCoeff = 0.01f;
  float attackCoeff = 1.0f, releaseCoeff = 0.01f;
  int holdSamples = 0;
  float svfK = 1.0f, svfA1 = 1.0f, svfA2 = 0.0f, svfA3 = 0.0f;
  float lowpassCoeff = 1.0f;

  // Threshold state (recomputed only on knob moves; sentinel forces the
  // first setThresholdDb() to compute).
  float currentThresholdDb = 1.0f;
  float openThreshold = 1.0f, closeThreshold = 1.0f, invOpenThreshold = 1.0f;

  std::array<Channel, kMaxChannels> channels;
};
