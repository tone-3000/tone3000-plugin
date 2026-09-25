#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>

/**
 * Polyphonic pitch shifter for the input stage: the Transpose group on the
 * faceplate. Shifts the raw instrument signal by whole semitones (plus a
 * fine trim) before it reaches the NAM/IR chain, so a guitar in standard
 * tuning drives the amp as if it were tuned down (or up), the same job as
 * the input transpose on Neural DSP's X-series plugins or a Digitech Drop.
 *
 * The engine is Signalsmith Stretch (Geraint Luff / Signalsmith Audio, MIT;
 * https://github.com/Signalsmith-Audio/signalsmith-stretch), the polyphonic
 * phase-vocoder from the ADC22 talk "Four Ways To Write A Pitch-Shifter".
 * It runs at a fixed 1:1 time ratio here; only the frequency map moves.
 * Everything below is a thin real-time wrapper: this class owns no DSP of
 * its own beyond routing.
 *
 * Latency. A phase vocoder has to see a whole analysis window before it can
 * place a partial, and the added latency is exactly that window
 * (inputLatency + outputLatency == blockSamples). The window also sets the
 * frequency resolution: the closer two simultaneous fundamentals sit, the
 * longer the window needed to keep them apart, otherwise the shifted chord
 * warbles. So Window is the one real trade-off and the deck exposes it as
 * three detents (kWindowMs), the middle one the default; riffs and power
 * chords are fine on the short one, strummed clean chords want the long
 * one. The processor does not call this class while the group is powered
 * off, so a powered-off Transpose is a bit-exact, zero-latency passthrough
 * (the "fresh default chain is transparent" invariant in processor_tests),
 * and the reported latency only ever changes on the power switch or a
 * Window change, never as the knob sweeps through 0 (the engine keeps
 * running at a 1.0 ratio there, so timing never jumps mid-riff).
 *
 * Tonality limit (Signalsmith's term): above the limit frequency the shift
 * becomes a constant offset instead of a ratio, so the pick attack and
 * fret noise up high keep their character while the notes below move. 0
 * (the default) is a pure shift, which is what a down-tuned guitar sounds
 * like; the deck's Tonality knob is there for players who find a large
 * downshift too dull through their amp.
 *
 * Formant preservation is deliberately not used: a physically lower-tuned
 * guitar moves its whole spectrum, and "correcting" formants makes the
 * shift sound like an effect.
 *
 * CPU is a fraction of a percent of one core at any window (the FFT is on
 * Accelerate on Apple targets, Signalsmith's own elsewhere); the three
 * engines are pre-built in prepare() so a Window change is a pointer swap
 * plus a reset, and process() never allocates.
 *
 * Threading: prepare()/reset() from prepareToPlay, setParams() and process()
 * from the audio thread. Stereo: both channels share one engine (Signalsmith
 * keeps the channels' phases coherent), a mono buffer is fed to both engine
 * lanes and read back from the first.
 */
class Transpose {
public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kSemitoneRange = 12;    // knob is ±12
  static constexpr float kCentsRange = 50.0f;  // fine trim is ±50
  // Tonality knob span (log); the top end means off (a pure shift).
  static constexpr float kTonalityMinHz = 1000.0f;
  static constexpr float kTonalityOffHz = 20000.0f;

  // Analysis window: the whole of the added latency (see the class comment).
  enum class Window { fast, balanced, smooth };
  static constexpr std::array<int, 3> kWindowMs{30, 60, 100};
  static int windowMs(Window w) { return kWindowMs[static_cast<size_t>(w)]; }
  static Window windowFromIndex(int index) {
    return static_cast<Window>(juce::jlimit(0, static_cast<int>(kWindowMs.size()) - 1, index));
  }

  /** The user-facing controls, in real units (the APVTS stores them the
      same way). Semitones + cents form one pitch ratio. */
  struct Params {
    int semitones = 0;
    float cents = 0.0f;
    // Frequency above which the shift is an offset, not a ratio. 0: off.
    float tonalityHz = 0.0f;
    Window window = Window::balanced;
  };

  Transpose();
  ~Transpose();

  /** Builds every window's engine for this rate; the scratch buffer is sized
      to maxBlockSamples and larger blocks are processed in slices. */
  void prepare(double sampleRate, int maxBlockSamples);

  /** Clears the engine's analysis/overlap state. Call on the off->on
      transition so the first powered block never hears stale audio. */
  void reset();

  /** Audio thread, once per block. Each field early-outs when unchanged; a
      window change swaps to that window's engine and resets it. */
  void setParams(const Params& p);

  /** Audio thread. Shifts up to kMaxChannels in place. */
  void process(juce::AudioBuffer<float>& buffer);

  /** Added latency for the current window at the prepared rate. */
  int latencySamples() const;
  /** The same figure for any window/rate (the message thread reports it to
      the host before the audio thread has switched). */
  static int latencySamples(Window w, double sampleRate) {
    return static_cast<int>(sampleRate * windowMs(w) * 0.001);
  }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
