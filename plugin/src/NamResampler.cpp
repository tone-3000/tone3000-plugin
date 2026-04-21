#include "NamResampler.h"
#include "NAM/slimmable.h"
#include <cmath>
#include <stdexcept>

NamResampler::NamResampler(std::unique_ptr<nam::DSP> model, double hostSampleRate)
    : wrappedModel(std::move(model))
    , hostSampleRate(hostSampleRate)
    , modelSampleRate(extractModelSampleRate(wrappedModel))
    , isPrepared(false)
    , maxBlockSize(0)
{
    if (!wrappedModel) {
        throw std::invalid_argument("NAM model cannot be null");
    }

    // Create the processing function for ResamplingContainer callback
    processFunc = [this](float** input, float** output, int numFrames) {
        // NAM models expect double*, so we need to convert
        // Resize conversion buffers if needed
        if (static_cast<int>(inputConversionBuffer.size()) < numFrames) {
            inputConversionBuffer.resize(numFrames);
            outputConversionBuffer.resize(numFrames);
        }

        // Convert float input to double
        for (int i = 0; i < numFrames; ++i) {
            inputConversionBuffer[i] = static_cast<double>(input[0][i]);
        }

        // Process with NAM model (mono: input[0] -> output[0])
        // sdatkinson API uses NAM_SAMPLE** (array of channel pointers)
        NAM_SAMPLE* inputPtrs[] = {inputConversionBuffer.data()};
        NAM_SAMPLE* outputPtrs[] = {outputConversionBuffer.data()};
        wrappedModel->process(inputPtrs, outputPtrs, numFrames);

        // Convert double output back to float
        for (int i = 0; i < numFrames; ++i) {
            output[0][i] = static_cast<float>(outputConversionBuffer[i]);
        }
    };

    // Create resampler if needed
    if (needsResampling()) {
        resampler = std::make_unique<dsp::ResamplingContainer<float, 1, 12>>(modelSampleRate);
    }
}

void NamResampler::prepare(double sampleRate, int maxBlockSize) {
    this->hostSampleRate = sampleRate;
    this->maxBlockSize = maxBlockSize;

    // Update resampler if sample rate changed
    if (needsResampling()) {
        if (!resampler) {
            resampler = std::make_unique<dsp::ResamplingContainer<float, 1, 12>>(modelSampleRate);
        }
        resampler->Reset(hostSampleRate, maxBlockSize);
        // ResamplingContainer may pass more frames to the model than maxBlockSize (e.g. when
        // host is 44.1k and model is 48k). Match its MaxEncapsulatedBlockSize: ceil(maxBlockSize / (host/model))
        const double ratio1 = hostSampleRate / modelSampleRate;
        const int maxEncapsulatedFrames = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / ratio1));
        wrappedModel->ResetAndPrewarm(modelSampleRate, maxEncapsulatedFrames);
    } else {
        // No resampling needed, just prepare the model directly
        const double ratio = hostSampleRate / modelSampleRate;
        const int modelBlockSize = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / ratio));
        wrappedModel->ResetAndPrewarm(modelSampleRate, modelBlockSize);
    }

    // SlimmableContainer / SlimmableWavenet: after max buffer size is set on the root DSP,
    // SetSlimmableSize re-selects the active tier and ResetAndPrewarms it (see NAM render tools
    // and NeuralAmpModelerCore tests). Without this, container models can stay on a stale or
    // uninitialized sub-path until the host would drive a CPU slider.
    if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()))
        slimmable->SetSlimmableSize(requestedSlimmableSize);

    isPrepared = true;
}

bool NamResampler::isSlimmableModel() const {
    return dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()) != nullptr;
}

void NamResampler::setSlimmableSize(double val) {
    requestedSlimmableSize = juce::jlimit(0.5, 1.0, val);
    if (!isPrepared)
        return;
    if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(wrappedModel.get()))
        slimmable->SetSlimmableSize(requestedSlimmableSize);
}

void NamResampler::process(juce::AudioBuffer<float>& buffer) {
    if (!isPrepared) {
        throw std::runtime_error("NamResampler must be prepared before processing");
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples > maxBlockSize) {
        throw std::runtime_error("Block size exceeds maximum prepared size");
    }

    // Get the left channel for processing (NAM models are mono)
    float* leftChannel = buffer.getWritePointer(0);

    if (!needsResampling()) {
        // Direct processing - no resampling needed, but still need type conversion
        // Resize conversion buffers if needed
        if (static_cast<int>(inputConversionBuffer.size()) < numSamples) {
            inputConversionBuffer.resize(numSamples);
            outputConversionBuffer.resize(numSamples);
        }

        // Convert float to double
        for (int i = 0; i < numSamples; ++i) {
            inputConversionBuffer[i] = static_cast<double>(leftChannel[i]);
        }

        // Process with NAM model (sdatkinson API uses NAM_SAMPLE**)
        NAM_SAMPLE* inputPtrs[] = {inputConversionBuffer.data()};
        NAM_SAMPLE* outputPtrs[] = {outputConversionBuffer.data()};
        wrappedModel->process(inputPtrs, outputPtrs, numSamples);

        // Convert double back to float
        for (int i = 0; i < numSamples; ++i) {
            leftChannel[i] = static_cast<float>(outputConversionBuffer[i]);
        }
    } else {
        // Use ResamplingContainer for resampling
        float* inputPtrs[] = {leftChannel};
        float* outputPtrs[] = {leftChannel};

        resampler->ProcessBlock(inputPtrs, outputPtrs, numSamples, processFunc);
    }

    // Copy processed left channel to right channel if stereo
    if (numChannels > 1) {
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    }

    // Zero any additional channels
    for (int ch = 2; ch < numChannels; ++ch) {
        buffer.clear(ch, 0, numSamples);
    }
}

int NamResampler::getLatencySamples() const {
    if (!needsResampling() || !resampler) {
        return 0;
    }
    return resampler->GetLatency();
}

bool NamResampler::needsResampling() const {
    return std::abs(hostSampleRate - modelSampleRate) > SAMPLE_RATE_TOLERANCE;
}

void NamResampler::resetAndPrewarm(double sampleRate, int maxBlockSize) {
    prepare(sampleRate, maxBlockSize);
}

double NamResampler::extractModelSampleRate(const std::unique_ptr<nam::DSP>& model) {
    if (!model) {
        throw std::invalid_argument("Cannot extract sample rate from null model");
    }

    // Try to get the expected sample rate from the model
    // This should work for most NAM models
    try {
        return model->GetExpectedSampleRate();
    } catch (...) {
        // Fallback to a reasonable default if the model doesn't specify
        // Most guitar amp models are trained at 48kHz
        return 48000.0;
    }
}
