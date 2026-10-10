#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "NAM/dsp.h"
#include "ChainDomain.h"

/**
 * A JUCE-compatible host for a nam::DSP model, running in the chain domain
 * (kChainBaseSampleRate × oversampling factor; see ChainDomain.h).
 * Sample-rate conversion is NOT this class's job (the whole chain stage sits
 * behind one resampling boundary + oversampler in the processor), so all this
 * does is:
 *  - float ↔ double conversion (NAM models process NAM_SAMPLE == double),
 *  - mono processing with fan-out to stereo buffers,
 *  - slimmable (A2 container) tier selection,
 *  - phase-interleaved oversampled processing (below).
 *
 * Phase-interleaved oversampling:
 * A NAM model oversampled by N with its convolution dilations scaled by N is
 * mathematically identical to N independent copies of the *unscaled* model,
 * each processing every Nth sample of the oversampled stream at the native
 * rate: every scaled dilation lands taps exactly N samples apart (within one
 * phase), and 1×1 convolutions/activations are per-sample. So instead of
 * patching dilation scaling into NeuralAmpModelerCore, the engine holds N
 * instances of the same model and interleaves them. The receptive field stays
 * constant in seconds (the model sounds the same) while its nonlinear
 * harmonics land in the widened band where the chain's decimation filter
 * removes them instead of letting them alias.
 *
 * Recurrent architectures (LSTM) update state on consecutive samples and
 * can't be phase-split; the loader gives them a single instance that runs
 * time-scaled at the full chain rate (matching NAM-Oversampler's behavior
 * for such models). In practice every loadable model is phase-safe (the
 * catalog and the local-file gate only admit A2 WaveNets), so this is a
 * defensive path.
 *
 * Voices (dual mono):
 * A NAM model is mono, so by default channel 0 goes through the model and
 * the result is fanned out to channel 1. With two voices the engine holds a
 * second, independent set of phase instances for channel 1, so a stereo
 * buffer is processed as two separate mono signals through the same model
 * (the mono chain's "Dual Mono" input mode; see InputMode in Processor.h).
 * The voice count is fixed at construction like the phase count: the
 * processor rebuilds the engine when the mode's requirement changes (a
 * dormant voice would resume with stale model history, and nam::DSP has no
 * RT-safe reset). process() only *uses* voice 1 when asked (`dualMono`)
 * and the buffer is stereo; otherwise voice 0 fans out exactly as a
 * single-voice engine does, so the two schedules never mix channels.
 *
 * Multi-core:
 * Every (voice, phase) instance is fully independent (separate model,
 * disjoint I/O buffers), so process() forks them as one flat job set across
 * the processor's RtWorkerPool (the same pool that forks the stereo lanes; a
 * lane job forking its phases is the pool's supported one-deep nesting; a
 * dual-mono engine runs in mono chain mode, where no lane fork exists, so
 * the voice×phase fork never nests). Scheduling is the only thing that
 * changes: deinterleave/reinterleave stay on the calling thread and every
 * job writes only its own buffers, so parallel output is bit-identical to
 * serial. Passing no pool runs the sequential loop.
 */
class RtWorkerPool;

class NamEngine {
public:
  /** Largest voice count an engine can carry (channel 0 and channel 1). */
  static constexpr int kMaxVoices = 2;

  /** Takes ownership of the model instances, all built from the same model
      config, laid out voice-major: `voices` groups of `phases` instances,
      where `phases` is 1 or `oversampleFactor`. One phase runs at the full
      chain rate (kChainBaseSampleRate × `oversampleFactor`); N > 1 phases
      run phase-interleaved, each at 1/Nth of it. The loader picks both
      counts: `oversampleFactor` phases for phase-safe architectures, one
      otherwise; two voices for the dual-mono input mode, one otherwise.
      Throws std::invalid_argument on empty/null instances, an invalid
      factor or voice count, or a size that isn't voices × {1, factor}. */
  NamEngine(std::vector<std::unique_ptr<nam::DSP>> instances, int oversampleFactor,
            int voices = 1);
  ~NamEngine() = default;

  NamEngine(const NamEngine&) = delete;
  NamEngine& operator=(const NamEngine&) = delete;
  NamEngine(NamEngine&&) = default;
  NamEngine& operator=(NamEngine&&) = default;

  /** Prepare for processing in the chain domain. `maxBlockSize` is the
      largest per-call frame count (the chain-domain block size). */
  void prepare(int maxBlockSize);

  /** Process a chain-domain buffer in place. Default: channel 0 through
      voice 0, fanned out to channel 1 if present. With `dualMono` set on a
      two-voice engine and a stereo buffer, channel 1 goes through voice 1
      instead (two independent mono signals through the same model); any
      other combination falls back to the fan-out, so a single-voice engine
      or a mono buffer behaves identically whatever `dualMono` says. Must be
      prepared first. With a pool and more than one (voice, phase) job, the
      jobs fork across the pool's workers (bit-identical to the serial loop;
      see the header comment); a job failure rethrows as std::runtime_error
      on this thread once every job has completed. `pool` may be null. */
  void process(juce::AudioBuffer<float>& buffer, RtWorkerPool* pool = nullptr,
               bool dualMono = false);

  /** The rate the model reports it was trained at. Purely informational;
      the chain always feeds it the chain rate (A2 models are all 48 kHz). */
  double getModelSampleRate() const { return modelSampleRate; }

  /** The oversampling factor this engine was built for. The apply path
      compares it against the live factor: a mismatch (the setting changed
      while the load was in flight) means the phase count is wrong and the
      block must be rebuilt. */
  int getOversampleFactor() const { return oversampleFactor; }

  /** The voice count this engine was built for (1, or 2 for dual mono).
      Compared by the apply path against the live requirement exactly like
      the factor above; a mismatch rebuilds the block. */
  int getVoiceCount() const { return voices; }

  /** Phase instances per voice (1, or the oversampling factor). */
  int getPhaseCount() const { return phases; }

  bool hasInputLevel() const { return primary().HasInputLevel(); }
  double getInputLevel() const { return primary().GetInputLevel(); }
  bool hasOutputLevel() const { return primary().HasOutputLevel(); }
  double getOutputLevel() const { return primary().GetOutputLevel(); }
  bool hasLoudness() const { return primary().HasLoudness(); }
  double getLoudness() const { return primary().GetLoudness(); }

  /**
   * Requested slimmable size (0.0 = lite, 1.0 = full). Clamped to [0.0, 1.0].
   * A no-op for models that aren't SlimmableModel (non-container A2 files).
   * NAM tier mappers assign the boundary value to the tier above (a two-tier
   * container selects lite for [0, 0.5) and full for [0.5, 1.0]), so the lite
   * request must be 0.0; 0.5 would select full.
   * Applied in prepare() and immediately if already prepared. Fans out to
   * every phase instance so all phases always run the same tier.
   */
  void setSlimmableSize(double val);

  double getSlimmableSize() const noexcept { return requestedSlimmableSize; }

private:
  /** Instance 0: the reference for metadata queries (all instances share
      one model config, so levels/loudness/rate are identical). */
  nam::DSP& primary() const { return *instances.front(); }

  /** Flat index of (voice, phase) in `instances` and the I/O slot arrays. */
  int slotIndex(int voice, int phase) const noexcept { return voice * phases + phase; }

  /** The rate each instance runs at: chain rate for a single phase, the
      base rate for interleaved phases. */
  double instanceSampleRate() const {
    return kChainBaseSampleRate * oversampleFactor / static_cast<double>(phases);
  }

  std::vector<std::unique_ptr<nam::DSP>> instances;
  int oversampleFactor = 1;
  int voices = 1;
  int phases = 1;
  double modelSampleRate;

  // Per-(voice, phase) double-precision I/O (NAM expects double); the
  // deinterleave IS the float→double conversion. A single instance is just
  // the one-voice, one-phase case of the same path. Indexed by slotIndex;
  // sized in prepare(). phaseFrames is per phase only: both voices split
  // the same chunk with the same phase offset, so their counts are equal.
  std::vector<std::vector<double>> slotInputs;
  std::vector<std::vector<double>> slotOutputs;
  std::vector<int> phaseFrames;

  // Which phase the next incoming sample belongs to. Chain buffers are
  // normally divisible by the phase count (the oversampler guarantees it),
  // but tracking the offset keeps every instance's sample stream continuous
  // even if a defensive slice path hands over a partial block.
  int phaseOffset = 0;

  bool isPrepared = false;
  int maxBlockSize = 0;

  double requestedSlimmableSize{1.0};
};
