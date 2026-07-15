#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "NAM/dsp.h"
#include "ChainDomain.h"

/**
 * A JUCE-compatible host for a nam::DSP model, running at the fixed chain
 * rate (kChainSampleRate). Sample-rate conversion is NOT this class's job —
 * the whole chain stage sits behind one resampling boundary in the processor
 * (see ChainDomain.h) — so all this does is:
 *  - float ↔ double conversion (NAM models process NAM_SAMPLE == double),
 *  - mono processing with fan-out to stereo buffers,
 *  - slimmable (A2 container) tier selection.
 */
class NamEngine {
public:
  /** Takes ownership of the model. Throws std::invalid_argument on null. */
  explicit NamEngine(std::unique_ptr<nam::DSP> model);
  ~NamEngine() = default;

  NamEngine(const NamEngine&) = delete;
  NamEngine& operator=(const NamEngine&) = delete;
  NamEngine(NamEngine&&) = default;
  NamEngine& operator=(NamEngine&&) = default;

  /** Prepare for processing at kChainSampleRate. `maxBlockSize` is the
      largest per-call frame count (the chain-domain block size). */
  void prepare(int maxBlockSize);

  /** Process a chain-domain buffer in place: channel 0 through the model,
      fanned out to channel 1 if present. Must be prepared first. */
  void process(juce::AudioBuffer<float>& buffer);

  /** The rate the model reports it was trained at. Purely informational —
      the chain always feeds it kChainSampleRate (A2 models are all 48 kHz). */
  double getModelSampleRate() const { return modelSampleRate; }

  bool hasInputLevel() const { return wrappedModel->HasInputLevel(); }
  double getInputLevel() const { return wrappedModel->GetInputLevel(); }
  bool hasOutputLevel() const { return wrappedModel->HasOutputLevel(); }
  double getOutputLevel() const { return wrappedModel->GetOutputLevel(); }
  bool hasLoudness() const { return wrappedModel->HasLoudness(); }
  double getLoudness() const { return wrappedModel->GetLoudness(); }

  /** True if the wrapped NAM model supports SlimmableModel (container / A2 slimmable). */
  bool isSlimmableModel() const;

  /**
   * Requested slimmable size (0.0 = lite, 1.0 = full). Clamped to [0.0, 1.0].
   * NAM tier mappers assign the boundary value to the tier above (a two-tier
   * container selects lite for [0, 0.5) and full for [0.5, 1.0]), so the lite
   * request must be 0.0 — 0.5 would select full.
   * Applied in prepare() and immediately if already prepared.
   */
  void setSlimmableSize(double val);

  double getSlimmableSize() const noexcept { return requestedSlimmableSize; }

private:
  std::unique_ptr<nam::DSP> wrappedModel;
  double modelSampleRate;

  // Conversion buffers for float<->double (NAM expects double), sized in prepare().
  std::vector<double> inputConversionBuffer;
  std::vector<double> outputConversionBuffer;

  bool isPrepared = false;
  int maxBlockSize = 0;

  double requestedSlimmableSize{1.0};
};
