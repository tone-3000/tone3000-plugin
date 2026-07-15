#include "NamEngine.h"
#include "NAM/slimmable.h"
#include <stdexcept>

NamEngine::NamEngine(std::unique_ptr<nam::DSP> model) : wrappedModel(std::move(model)) {
  if (!wrappedModel) {
    throw std::invalid_argument("NAM model cannot be null");
  }
  // Informational only; the chain domain always runs the model at
  // kChainSampleRate regardless (the loader logs a warning on mismatch).
  try {
    modelSampleRate = wrappedModel->GetExpectedSampleRate();
  } catch (...) {
    modelSampleRate = kChainSampleRate;
  }
}

void NamEngine::prepare(int newMaxBlockSize) {
  maxBlockSize = newMaxBlockSize;

  inputConversionBuffer.resize(static_cast<size_t>(maxBlockSize));
  outputConversionBuffer.resize(static_cast<size_t>(maxBlockSize));

  wrappedModel->ResetAndPrewarm(kChainSampleRate, maxBlockSize);

  // SlimmableContainer / SlimmableWavenet: after max buffer size is set on the
  // root DSP, SetSlimmableSize re-selects the active tier and ResetAndPrewarms
  // it (see NAM render tools and NeuralAmpModelerCore tests). Without this,
  // container models can stay on a stale or uninitialized sub-path.
  if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()))
    slimmable->SetSlimmableSize(requestedSlimmableSize);

  isPrepared = true;
}

bool NamEngine::isSlimmableModel() const {
  return dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()) != nullptr;
}

void NamEngine::setSlimmableSize(double val) {
  requestedSlimmableSize = juce::jlimit(0.0, 1.0, val);
  if (!isPrepared)
    return;
  if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()))
    slimmable->SetSlimmableSize(requestedSlimmableSize);
}

void NamEngine::process(juce::AudioBuffer<float>& buffer) {
  if (!isPrepared) {
    throw std::runtime_error("NamEngine must be prepared before processing");
  }

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  if (numSamples > maxBlockSize) {
    throw std::runtime_error("Block size exceeds maximum prepared size");
  }

  // NAM models are mono: process channel 0 through the model...
  float* leftChannel = buffer.getWritePointer(0);
  for (int i = 0; i < numSamples; ++i) {
    inputConversionBuffer[static_cast<size_t>(i)] = static_cast<double>(leftChannel[i]);
  }

  NAM_SAMPLE* inputPtrs[] = {inputConversionBuffer.data()};
  NAM_SAMPLE* outputPtrs[] = {outputConversionBuffer.data()};
  wrappedModel->process(inputPtrs, outputPtrs, numSamples);

  for (int i = 0; i < numSamples; ++i) {
    leftChannel[i] = static_cast<float>(outputConversionBuffer[static_cast<size_t>(i)]);
  }

  // ...and fan the result out to the other channels.
  for (int ch = 1; ch < numChannels; ++ch) {
    buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
  }
}
