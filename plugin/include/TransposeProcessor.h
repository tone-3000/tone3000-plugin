#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch.h>

/**
 * Real-time integer-semitone pitch shifter for the Transpose knob, applied
 * to the raw guitar input before it reaches the NAM/amp chain.
 *
 * Wraps Signalsmith Stretch (MIT), a phase-vocoder pitch/time library. Only
 * the pitch side is used here: process() is always called with equal
 * input/output sample counts, so the stretch ratio stays fixed at 1.0 and
 * only setSemitones() changes the pitch.
 *
 * Latency and window size: presetDefault()/presetCheaper() (the library's
 * built-in presets) pick ~120 ms analysis windows, tuned for offline/
 * non-monitored use - far too much added latency for a guitarist monitoring
 * themselves live through an amp sim. Instead this wraps configure()
 * directly with a ~20 ms window (kWindowMs below), which keeps the same 4x
 * STFT overlap presetDefault() uses (quality first, per the brief - dial
 * back the overlap, not the window, if profiling says the engine is too
 * expensive).
 *
 * No formant preservation: this version of Signalsmith Stretch has no
 * formant API (setFormantFactor/setFormantSemitones from the original brief
 * don't exist here - that was based on a different library). Large shifts
 * will sound more "chipmunk/darth vader" than a formant-aware shifter would.
 *
 * Bypassed at 0 semitones (the default): process() skips the engine
 * entirely and getLatencySamples() reports 0. This is a hard product
 * requirement here, not just an optimization - this codebase pins "a fresh,
 * default-parameter chain is bit-exact and zero-latency transparent" as a
 * literal test (see ProcessorTest.EmptyChainAt48kIsTransparentWithZeroLatency
 * in processor_tests.cpp), and an unconditionally-running phase vocoder
 * would break that even with the transpose factor at 1.0 (FFT round-trip
 * noise floor, plus a fixed ~20 ms of reported latency on every chain).
 *
 * The cost of bypassing is a latency/timing discontinuity right at the
 * knob's 0 <-> nonzero boundary: engaging goes from 0 added latency to
 * ~kWindowMs of added latency (and Processor.cpp re-reports it to the host
 * via setLatencySamples() when this happens, so PDC stays correct), which
 * is an audible timing jump, not just a click. A fully seamless crossfaded
 * bypass would need a delay-compensated dry path and is out of scope for a
 * first version; revisit if this proves audible in practice.
 *
 * Threading: prepare()/reset() from prepareToPlay, setSemitones() and
 * process() from the audio thread. process() reads/writes through a
 * pre-sized scratch buffer (Signalsmith Stretch requires distinct
 * input/output buffers) that only reallocates if a block arrives larger
 * than what prepare() sized it for.
 */
class TransposeProcessor {
public:
  static constexpr int kMaxChannels = 2;

  void prepare(double newSampleRate, int newNumChannels, int maxBlockSamples) {
    sampleRate = newSampleRate;
    numChannels = juce::jlimit(1, kMaxChannels, newNumChannels);

    const int blockSamples = juce::jmax(64, static_cast<int>(sampleRate * kWindowMs * 0.001));
    const int intervalSamples = juce::jmax(1, static_cast<int>(blockSamples / kOverlap));
    stretch.configure(numChannels, blockSamples, intervalSamples);
    engineLatencySamples = stretch.inputLatency() + stretch.outputLatency();

    scratch.setSize(numChannels, juce::jmax(1, maxBlockSamples));

    // configure() rebuilds the engine's analysis state; force setSemitones()
    // below to re-push the transpose factor into the fresh engine rather
    // than early-outing on "value unchanged".
    lastPushedSemitones = -1;
    setSemitones(currentSemitones);
    reset();
  }

  /** Clears the engine's internal analysis/overlap-add state. Call on the
      bypassed->active transition or a transport restart so stale windowed
      audio never bleeds into the first active block. */
  void reset() { stretch.reset(); }

  /** Audio thread, once per block. Early-outs when unchanged so
      setTransposeSemitones (a few transcendentals + resets some internal
      frequency-map state) only runs on actual knob moves. Resets the engine
      on the bypassed->active transition (see the class comment) so a stale
      analysis window from the last time this was engaged never bleeds into
      the first active block. */
  void setSemitones(int newSemitones) {
    if (currentSemitones == 0 && newSemitones != 0)
      reset();
    currentSemitones = newSemitones;
    if (newSemitones == lastPushedSemitones)
      return;
    lastPushedSemitones = newSemitones;
    stretch.setTransposeSemitones(static_cast<float>(newSemitones));
  }

  /** 0 when bypassed (0 semitones); see the class comment. */
  int getLatencySamples() const { return currentSemitones == 0 ? 0 : engineLatencySamples; }

  /** Audio thread. Pitch-shifts up to kMaxChannels in place; no-op while
      bypassed (0 semitones). */
  void process(juce::AudioBuffer<float>& buffer) {
    if (currentSemitones == 0)
      return;

    const int numSamples = buffer.getNumSamples();
    const int channelsAvailable = buffer.getNumChannels();
    if (numSamples <= 0 || channelsAvailable <= 0)
      return;

    // Signalsmith Stretch requires separate input/output buffers; it can't
    // process in place. avoidReallocating=true means this only allocates if
    // a block arrives bigger than prepare() sized it for.
    scratch.setSize(numChannels, numSamples, false, false, true);

    // The engine was configure()'d for `numChannels` channels (prepare()
    // always requests stereo - see Processor.cpp), but the live buffer can
    // have fewer (a genuinely mono host bus). Build a read-pointer array of
    // the configured width, mirroring the last available channel into any
    // slot the buffer doesn't have, so the engine never reads past the
    // buffer's real channel count.
    const float* readPointers[kMaxChannels];
    for (int ch = 0; ch < numChannels; ++ch)
      readPointers[ch] = buffer.getReadPointer(juce::jmin(ch, channelsAvailable - 1));

    stretch.process(readPointers, numSamples, scratch.getArrayOfWritePointers(), numSamples);

    const int channelsToProcess = juce::jmin(channelsAvailable, numChannels);
    for (int ch = 0; ch < channelsToProcess; ++ch)
      buffer.copyFrom(ch, 0, scratch, ch, 0, numSamples);
  }

private:
  // ~20 ms window, same 4x overlap as the library's own presetDefault().
  static constexpr double kWindowMs = 20.0;
  static constexpr double kOverlap = 4.0;

  signalsmith::stretch::SignalsmithStretch<float> stretch;
  juce::AudioBuffer<float> scratch;

  double sampleRate = 48000.0;
  int numChannels = 1;
  int currentSemitones = 0;
  int lastPushedSemitones = -1;
  int engineLatencySamples = 0;
};
