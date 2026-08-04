#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

/**
 * Auto offset: one-shot time-alignment measurement between the two chains in
 * stereo chain mode. Different NAM models / IRs can carry different baked-in
 * latency, so two chains fed the same instrument can land a few ms apart;
 * this measures that misalignment from the user's real playing and produces
 * the corrective delay for the StereoOffset engine.
 *
 * Why listening, not an impulse: NAM chains are nonlinear (a gate eats a
 * quiet impulse, a hot one smears into distortion; neither measures the
 * path cleanly), and injecting a test signal would mean muting a live bus.
 * Both chains hear the same source, so cross-correlating their outputs
 * yields a sharp peak at the true lag even when the chains are voiced very
 * differently; same reasoning as the auto-balance listening flow, and the
 * same UX.
 *
 * Lifecycle (mirrors auto-balance; see Processor.h):
 *  - start() [message thread] arms the capture.
 *  - capture() [audio thread] appends the raw chain outputs (tapped BEFORE
 *    the StereoOffset delay, so the measurement is the chains' absolute
 *    misalignment, independent of the current knob) while state is
 *    Listening. Blocks below a -50 dBFS floor don't count (silence between
 *    phrases must not dilute the correlation); ~15 s without enough signal
 *    times out. Full capture flips to Captured and the audio thread is done.
 *  - analyze() [message thread, in Captured] runs the FFT cross-correlation
 *    and returns the result; the caller applies it to the host parameters
 *    (setValueNotifyingHost stays off the RT path).
 *
 * Analysis: circular cross-correlation via FFT (zero-padded past the lag
 * window, so wrap-around never contaminates it), peak-searched over ±24 ms,
 * the range the Offset knob can express (StereoOffsetParams::kMaxOffsetMs).
 * The result carries a confidence: the normalized correlation at the peak
 * (1 = identical up to gain and shift). A misalignment beyond ±24 ms or
 * genuinely unrelated outputs shows up as a noise-level peak, and the caller
 * rejects the measurement instead of setting a junk offset.
 *
 * Threading: the state atomic is the hand-off. The audio thread only writes
 * the capture buffer in Listening; the message thread only reads it in
 * Captured; the release-store on each state flip publishes the data across.
 */
class AutoOffset {
public:
  enum class State : int { Idle = 0, Listening, Captured, TimedOut };

  struct Result {
    /** Corrective offset in ms, the StereoOffset convention: positive delays
        the right chain (i.e. the left chain lags), negative the left. */
    float offsetMs = 0.0f;
    /** Normalized cross-correlation at the peak, 0..1. */
    float confidence = 0.0f;
  };

  static constexpr double kMeasureSeconds = 2.0;
  static constexpr double kTimeoutSeconds = 15.0;
  static constexpr double kFloorRms = 3.16e-3;  // -50 dBFS, same as auto-balance
  /** Lag search half-window: what the Offset knob can express
      (== StereoOffsetParams::kMaxOffsetMs; static-asserted in the .cpp). */
  static constexpr float kMaxLagMs = 24.0f;

  /** Allocates the capture buffer for this rate. Any armed measurement is
      dropped (a rate change invalidates the capture anyway). */
  void prepare(double sampleRate);

  void start();   // message thread: zero + arm
  void cancel();  // message thread

  State state() const {
    return static_cast<State>(stateFlag.load(std::memory_order_acquire));
  }

  /** Capture progress 0..1, for the UI poll. */
  float progress() const;

  /** Audio thread: append the raw (pre-offset) chain outputs while
      Listening. Requires >= 2 channels. */
  void capture(const juce::AudioBuffer<float>& buffer, int numSamples);

  /** Message thread, only valid in Captured: run the cross-correlation and
      go Idle. */
  Result analyze();

private:
  juce::AudioBuffer<float> captureBuffer;
  std::atomic<int> stateFlag{static_cast<int>(State::Idle)};
  std::atomic<int> written{0};
  std::atomic<juce::int64> elapsed{0};  // wall samples since arm, for the timeout
  int capacity = 0;
  double sampleRate = 48000.0;
};
