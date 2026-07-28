#include "NamEngine.h"
#include "NAM/slimmable.h"
#include <stdexcept>

NamEngine::NamEngine(std::vector<std::unique_ptr<nam::DSP>> phaseInstances, int factor)
    : instances(std::move(phaseInstances)), oversampleFactor(factor) {
  if (instances.empty())
    throw std::invalid_argument("NamEngine needs at least one model instance");
  for (const auto& instance : instances) {
    if (instance == nullptr)
      throw std::invalid_argument("NAM model cannot be null");
  }
  const auto count = static_cast<int>(instances.size());
  if (factor < 1 || (count != 1 && count != factor))
    throw std::invalid_argument("NamEngine instance count must be 1 or the oversampling factor");

  // Informational only; the chain domain runs the model at the chain rate
  // regardless (the loader logs a warning on mismatch).
  try {
    modelSampleRate = primary().GetExpectedSampleRate();
  } catch (...) {
    modelSampleRate = kChainBaseSampleRate;
  }
}

void NamEngine::prepare(int newMaxBlockSize) {
  maxBlockSize = newMaxBlockSize;

  const auto count = instances.size();
  // +1: a carried phase offset can hand one phase an extra frame when a
  // defensive slice isn't divisible by the phase count.
  const size_t perPhaseCapacity = static_cast<size_t>(maxBlockSize) / count + 1;
  phaseInputs.assign(count, std::vector<double>(perPhaseCapacity));
  phaseOutputs.assign(count, std::vector<double>(perPhaseCapacity));
  phaseFrames.assign(count, 0);
  phaseOffset = 0;

  for (auto& instance : instances) {
    instance->ResetAndPrewarm(instanceSampleRate(), static_cast<int>(perPhaseCapacity));

    // SlimmableContainer / SlimmableWavenet: after max buffer size is set on the
    // root DSP, SetSlimmableSize re-selects the active tier and ResetAndPrewarms
    // it (see NAM render tools and NeuralAmpModelerCore tests). Without this,
    // container models can stay on a stale or uninitialized sub-path.
    if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(instance.get()))
      slimmable->SetSlimmableSize(requestedSlimmableSize);
  }

  isPrepared = true;
}

bool NamEngine::isSlimmableModel() const {
  return dynamic_cast<nam::SlimmableModel*>(&primary()) != nullptr;
}

void NamEngine::setSlimmableSize(double val) {
  requestedSlimmableSize = juce::jlimit(0.0, 1.0, val);
  if (!isPrepared)
    return;
  for (auto& instance : instances) {
    if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(instance.get()))
      slimmable->SetSlimmableSize(requestedSlimmableSize);
  }
}

void NamEngine::process(juce::AudioBuffer<float>& buffer) {
  if (!isPrepared || maxBlockSize < 1) {
    throw std::runtime_error("NamEngine must be prepared before processing");
  }

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();
  const int count = static_cast<int>(instances.size());

  // NAM models are mono: process channel 0 through the model... Buffers
  // larger than the prepared size are run in prepared-size slices (models
  // stream statefully, so slicing is exact) — throwing here used to
  // permanently disable the block when a startup prepare raced the host's
  // actual block size.
  float* leftChannel = buffer.getWritePointer(0);
  for (int offset = 0; offset < numSamples; offset += maxBlockSize) {
    const int chunk = juce::jmin(maxBlockSize, numSamples - offset);

    // Deinterleave into the per-phase streams (single instance = one phase =
    // a straight copy). This doubles as the float → double conversion.
    std::fill(phaseFrames.begin(), phaseFrames.end(), 0);
    int phase = phaseOffset;
    for (int i = 0; i < chunk; ++i) {
      auto& phaseInput = phaseInputs[static_cast<size_t>(phase)];
      phaseInput[static_cast<size_t>(phaseFrames[static_cast<size_t>(phase)]++)] =
          static_cast<double>(leftChannel[offset + i]);
      if (++phase == count)
        phase = 0;
    }

    for (int p = 0; p < count; ++p) {
      const int frames = phaseFrames[static_cast<size_t>(p)];
      if (frames <= 0)
        continue;
      NAM_SAMPLE* inputPtrs[] = {phaseInputs[static_cast<size_t>(p)].data()};
      NAM_SAMPLE* outputPtrs[] = {phaseOutputs[static_cast<size_t>(p)].data()};
      instances[static_cast<size_t>(p)]->process(inputPtrs, outputPtrs, frames);
    }

    // Reinterleave (and convert back to float). Reuse phaseFrames as read
    // cursors by counting back up from zero.
    std::fill(phaseFrames.begin(), phaseFrames.end(), 0);
    phase = phaseOffset;
    for (int i = 0; i < chunk; ++i) {
      const auto& phaseOutput = phaseOutputs[static_cast<size_t>(phase)];
      leftChannel[offset + i] = static_cast<float>(
          phaseOutput[static_cast<size_t>(phaseFrames[static_cast<size_t>(phase)]++)]);
      if (++phase == count)
        phase = 0;
    }

    phaseOffset = (phaseOffset + chunk) % count;
  }

  // ...and fan the result out to the other channels.
  for (int ch = 1; ch < numChannels; ++ch) {
    buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
  }
}
