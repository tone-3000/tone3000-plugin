#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>

/**
 * Polyphonic pitch shifter for the input stage: the Pitch group on the
 * faceplate. Shifts the raw instrument signal by up to two octaves either
 * way before it reaches the NAM/IR chain. Stepped, it transposes: a guitar
 * in standard tuning drives the amp as if it were tuned down (or up), the
 * job of a Digitech Drop. Continuous, it sweeps like a whammy pedal, which
 * is what a MIDI expression pedal on the knob gets.
 *
 * Engine: a correlation-spliced delay line, the Eventide H949 "de-glitch"
 * idea (plugin/docs/pitch-shift.md records the research that chose it over
 * the phase vocoder it replaced). One read tap runs through a ring buffer
 * at the pitch ratio, so its delay behind the write head drifts between a
 * floor (kMinDelayMs) and the buffer size (the Window). When it reaches the
 * end it has to jump back; the jump lands where the buffer's recent
 * waveform best matches the tap's (normalised cross-correlation, scored as
 * damage per splice over splices per second so a long jump with a good
 * match beats a short perfect one), and the two taps crossfade. On periodic
 * material that is a whole number of periods, so the joint is inaudible
 * and the fade is short (30 ms); a chord gets the best compromise lag, and
 * the worse the match the longer the fade (up to 120 ms), so the partials
 * that don't line up drift across instead of clicking. Time-domain, so bass
 * is no harder than guitar: a 41 Hz E1 shifts as cleanly as an E4, which is
 * where the vocoder fell down.
 *
 * Onset re-sync: a pick attack (a jump in the high-passed input's energy
 * over its recent floor and ceiling) splices the tap straight to the
 * freshest end of the buffer, so attacks arrive a few ms late regardless of
 * where the tap had drifted; only the previous note's tail absorbs the
 * joint. The felt latency is therefore set by the attacks, not by the
 * buffer, and the buffer size (the deck's Buffer knob, the Window here) is
 * a quality trade: the lowest note it can hold a full period of, and how
 * often it splices. 20 ms is a guitar setting (an E1 period is 24 ms);
 * 30 ms is the default and works for bass; 40 / 60 splice less often.
 *
 * Latency reported to the host: the tap's mean delay, (floor + buffer) / 2,
 * a constant per Window and rate; the semitone knob never moves it (the
 * engine keeps running at a 1.0 ratio through 0, where the tap simply
 * stops drifting).
 *
 * The shift is continuous (semitones is a float): the deck's STEP toggle
 * decides whether the processor hands over whole semitones or the knob's
 * exact position, so with STEP off the knob sweeps like a whammy pedal. A
 * pitch change takes effect on the next sample; a pending lag search
 * survives it unless the drift changes direction or jumps by a lot, so a
 * sweep never leaves the tap at the buffer end without a plan.
 *
 * Lifecycle, like the image decks': every transition passes through a
 * blend, never a jump. Power blends the shifted signal against the dry
 * over kBlendSeconds and the engine keeps running until a fade-out lands
 * (isRunning), after which the processor stops calling it, so a powered-off
 * PitchShift is a bit-exact, zero-latency passthrough (the "fresh default
 * chain is transparent" invariant in processor_tests). A Window change
 * keeps the rings (they are sized for the largest window) and lets a tap
 * outside the new range splice back in like any drift splice.
 *
 * Tonality limit: a Linkwitz-Riley crossover after the shifter; the band
 * below the limit comes from the shifted signal, the band above from the
 * dry (delayed by the floor so it lands with the re-synced attacks), so
 * pick noise and string squeak keep their character while the notes move.
 * 0 (the default) is a pure shift, which is what a down-tuned guitar sounds
 * like. Formant preservation is deliberately not offered: a physically
 * lower-tuned guitar moves its whole spectrum, and "correcting" formants
 * makes the shift sound like an effect.
 *
 * CPU: 0.15-0.35% of one core. A drift splice's lag search (coarse-to-fine
 * over the buffer) is spread over the 4 ms before the tap reaches the
 * buffer end, so no single sample carries a burst: the worst block at a
 * 16-sample host buffer is ~100 us at 48 kHz. Only the onset re-sync
 * searches at once, over a 4 ms range. process() never allocates.
 *
 * Threading: prepare() from prepareToPlay; setEnabled(), setParams() and
 * process() from the audio thread. Stereo: the channels share one control
 * path (the onset detector and the lag search run on their mean) and one
 * tap position, so the image never smears; each channel keeps its own ring.
 */
class PitchShift {
public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kSemitoneRange = 24;  // knob is ±24
  // Tonality knob span (log); the top end means off (a pure shift).
  static constexpr float kTonalityMinHz = 1000.0f;
  static constexpr float kTonalityOffHz = 20000.0f;

  // The delay buffer the read tap drifts across (see the class comment);
  // the deck's Buffer knob, one detent each. The floor is the tap's
  // closest approach to the write head, where attacks are re-synced to.
  enum class Window { ms20, ms30, ms40, ms60 };
  static constexpr std::array<int, 4> kWindowMs{20, 30, 40, 60};
  static constexpr Window kDefaultWindow = Window::ms30;
  static constexpr double kMinDelayMs = 2.0;
  static int windowMs(Window w) { return kWindowMs[static_cast<size_t>(w)]; }
  static Window windowFromIndex(int index) {
    return static_cast<Window>(juce::jlimit(0, static_cast<int>(kWindowMs.size()) - 1, index));
  }

  /** The user-facing controls, in real units (the APVTS stores them the
      same way). Semitones is continuous; the processor rounds it when the
      STEP toggle is on, so the engine never knows about the toggle. */
  struct Params {
    float semitones = 0.0f;
    // Frequency above which the dry bypasses the shifter. 0: off.
    float tonalityHz = 0.0f;
    Window window = kDefaultWindow;
  };

  PitchShift();
  ~PitchShift();

  /** Sizes the rings for the largest window at this rate. Block size is
      not a constraint (the engine is per-sample). */
  void prepare(double sampleRate, int maxBlockSamples);

  /** Power. On restarts the engine at the floor and blends the shift in;
      off blends it out, and isRunning() stays true until that lands. */
  void setEnabled(bool on);
  bool isRunning() const;

  /** Audio thread, once per block. Each field early-outs when unchanged. */
  void setParams(const Params& p);

  /** Audio thread. Shifts up to kMaxChannels in place; a no-op unless
      running. */
  void process(juce::AudioBuffer<float>& buffer);

  /** Added latency for the current window at the prepared rate. */
  int latencySamples() const;
  /** The same figure for any window/rate (the message thread reports it to
      the host before the audio thread has switched): the tap's mean delay. */
  static int latencySamples(Window w, double sampleRate) {
    return (minDelaySamples(sampleRate) + windowSamples(w, sampleRate)) / 2;
  }
  /** The reported latency in ms, for readouts (11 / 16 / 21 / 31). */
  static double latencyMs(Window w) { return (kMinDelayMs + windowMs(w)) * 0.5; }

  static int minDelaySamples(double sampleRate) { return static_cast<int>(sampleRate * kMinDelayMs * 0.001); }
  static int windowSamples(Window w, double sampleRate) {
    return static_cast<int>(sampleRate * windowMs(w) * 0.001);
  }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
