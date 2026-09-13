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
 * Window size (kWindowMs): a phase vocoder's analysis window sets both pitch
 * stability (needs to span a few cycles of the lowest fundamental, ~12ms for
 * a guitar's low E) and frequency resolution (bin width = sampleRate /
 * blockSamples), which chords need enough of to keep closely-spaced
 * simultaneous fundamentals from blurring together in the engine's
 * peak-picking (findPeaks() in signalsmith-stretch.h). 80ms is the shortest
 * window that holds chords without warbling, found by manual bisection
 * (40-75ms all warbled on chords; 85-120ms are no cleaner, just more
 * latency). kOverlap is a separate, unrelated knob (STFT hop size / CPU
 * cost, not latency or frequency resolution) - see presetDefault()'s own 4x
 * overlap, matched here.
 *
 * TODO: 80ms is this algorithm's floor for chord quality - a single global
 * STFT window can't decouple "chord-safe frequency resolution" from
 * "low latency". If ~80ms proves too much for live monitoring, the next step
 * is a multi-resolution STFT (short windows for high frequencies, long for
 * low) rather than retuning this window further; tracked as a follow-up
 * feature, not a bug in this version.
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
 * noise floor, plus a fixed ~kWindowMs of reported latency on every chain).
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
  // See the class comment for why 80ms (chord frequency resolution vs
  // latency) and why kOverlap is unrelated to both.
  static constexpr double kWindowMs = 80.0;
  static constexpr double kOverlap = 4.0;

  signalsmith::stretch::SignalsmithStretch<float> stretch;
  juce::AudioBuffer<float> scratch;

  double sampleRate = 48000.0;
  int numChannels = 1;
  int currentSemitones = 0;
  int lastPushedSemitones = -1;
  int engineLatencySamples = 0;
};
