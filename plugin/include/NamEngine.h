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
 * for such models).
 */
class NamEngine {
public:
  /** Takes ownership of the phase instances, all built from the same model
      config. One instance runs at the full chain rate (kChainBaseSampleRate ×
      `oversampleFactor`); N > 1 instances run phase-interleaved, each at
      1/Nth of it. The loader picks the count: `oversampleFactor` instances
      for phase-safe architectures, one otherwise. Throws
      std::invalid_argument on empty/null instances or an invalid factor. */
  NamEngine(std::vector<std::unique_ptr<nam::DSP>> instances, int oversampleFactor);
  ~NamEngine() = default;

  NamEngine(const NamEngine&) = delete;
  NamEngine& operator=(const NamEngine&) = delete;
  NamEngine(NamEngine&&) = default;
  NamEngine& operator=(NamEngine&&) = default;

  /** Prepare for processing in the chain domain. `maxBlockSize` is the
      largest per-call frame count (the chain-domain block size). */
  void prepare(int maxBlockSize);

  /** Process a chain-domain buffer in place: channel 0 through the model,
      fanned out to channel 1 if present. Must be prepared first. */
  void process(juce::AudioBuffer<float>& buffer);

  /** The rate the model reports it was trained at. Purely informational;
      the chain always feeds it the chain rate (A2 models are all 48 kHz). */
  double getModelSampleRate() const { return modelSampleRate; }

  /** The oversampling factor this engine was built for. The apply path
      compares it against the live factor: a mismatch (the setting changed
      while the load was in flight) means the phase count is wrong and the
      block must be rebuilt. */
  int getOversampleFactor() const { return oversampleFactor; }

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

  /** The rate each instance runs at: chain rate for a single instance, the
      base rate for phase instances. */
  double instanceSampleRate() const {
    return kChainBaseSampleRate * oversampleFactor / static_cast<double>(instances.size());
  }

  std::vector<std::unique_ptr<nam::DSP>> instances;
  int oversampleFactor = 1;
  double modelSampleRate;

  // Per-phase double-precision I/O (NAM expects double); the deinterleave IS
  // the float→double conversion. A single instance is just the one-phase
  // case of the same path. Sized in prepare().
  std::vector<std::vector<double>> phaseInputs;
  std::vector<std::vector<double>> phaseOutputs;
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
