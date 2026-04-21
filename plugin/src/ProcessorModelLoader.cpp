#include "Processor.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

/** Single path segment for tempDirectory.getChildFile: API names may contain '/' etc. */
juce::String uniqueSafeTempLeafName(const juce::String& filename) {
  juce::String leaf(filename);
  leaf = leaf.replaceCharacters("/\\:", "___");
  leaf = leaf.trim();
  if (leaf.isEmpty())
    leaf = "model.bin";
  return juce::Uuid().toString() + "_" + leaf;
}

} // namespace

// #####################################
// MODEL LOADING HELPERS
// #####################################

float TONE3000Processor::computeIrNormalizationGain(const juce::File& irFile, size_t maxIrLength) {
  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));

  if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0) {
    return 1.0f;
  }

  const juce::int64 totalSamples =
      std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(maxIrLength));
  juce::AudioBuffer<float> tmp(static_cast<int>(reader->numChannels),
                               static_cast<int>(totalSamples));
  reader->read(&tmp, 0, static_cast<int>(totalSamples), 0, true, true);

  double sumSquares = 0.0;
  juce::int64 count = 0;
  for (int ch = 0; ch < tmp.getNumChannels(); ++ch) {
    const float* data = tmp.getReadPointer(ch);
    for (int i = 0; i < tmp.getNumSamples(); ++i) {
      const float sample = data[i];
      sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
      ++count;
    }
  }

  if (count <= 0) {
    return 1.0f;
  }

  const double l2norm = std::sqrt(sumSquares);
  if (!std::isfinite(l2norm) || l2norm <= 0.0) {
    return 1.0f;
  }

  const double linear = 1.0 / l2norm;
  const double linearClamped =
      juce::jlimit(juce::Decibels::decibelsToGain(-24.0), 1.0, linear);

  return static_cast<float>(linearClamped);
}

std::vector<uint8_t> TONE3000Processor::fetchModelFromUrl(const juce::String& modelUrl) {
  juce::URL url(modelUrl);
  juce::URL::InputStreamOptions options(juce::URL::ParameterHandling::inAddress);

  std::unique_ptr<juce::InputStream> stream(
      url.createInputStream(options.withConnectionTimeoutMs(30000)));

  if (!stream) {
    DBG("Failed to create input stream for URL: " << modelUrl);
    return {};
  }

  juce::MemoryBlock memoryBlock;
  const size_t blockSize = 8192;
  char buffer[blockSize];

  while (!stream->isExhausted()) {
    int bytesRead = stream->read(buffer, blockSize);
    if (bytesRead > 0) {
      memoryBlock.append(buffer, bytesRead);
    } else {
      break;
    }
  }

  if (memoryBlock.getSize() == 0) {
    DBG("Downloaded 0 bytes from URL: " << modelUrl);
    return {};
  }

  DBG("Successfully downloaded " << memoryBlock.getSize() << " bytes");

  std::vector<uint8_t> result(memoryBlock.getSize());
  std::memcpy(result.data(), memoryBlock.getData(), memoryBlock.getSize());

  return result;
}

void TONE3000Processor::loadModelData(ChainBlock& block,
                                      const std::vector<uint8_t>& modelData,
                                      const juce::String& filename) {
  if (modelData.empty()) {
    DBG("Cannot load empty model data");
    block.loaded = false;
    return;
  }

  DBG("Loading model data: " << filename << " (" << modelData.size() << " bytes)");

  try {
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File tempFile = tempDir.getChildFile(uniqueSafeTempLeafName(filename));

    if (!tempFile.replaceWithData(modelData.data(), modelData.size())) {
      DBG("Failed to create temporary file");
      block.loaded = false;
      return;
    }

    if (block.type == ChainBlockType::NAM) {
      std::unique_ptr<nam::DSP> rawDsp =
          nam::get_dsp(std::filesystem::path(tempFile.getFullPathName().toStdString()));

      if (rawDsp) {
        if (rawDsp->NumInputChannels() != 1) {
          throw std::runtime_error(
              "NAM model must have 1 input channel, but has "
              + std::to_string(rawDsp->NumInputChannels()));
        }
        if (rawDsp->NumOutputChannels() != 1) {
          throw std::runtime_error(
              "NAM model must have 1 output channel, but has "
              + std::to_string(rawDsp->NumOutputChannels()));
        }

        block.namResampler = std::make_unique<NamResampler>(std::move(rawDsp), hostSampleRate);
        block.namIsSlimmable = block.namResampler->isSlimmableModel();
        if (!block.namIsSlimmable)
          block.namSlimmableSize = 1.0;
        block.namResampler->setSlimmableSize(block.namSlimmableSize);

        if (hostSampleRate > 0) {
          block.namResampler->prepare(hostSampleRate, getBlockSize());
          block.latencySamples = block.namResampler->getLatencySamples();
        }

        block.loaded = true;

        DBG("NAM model loaded - Sample rate: " << block.namResampler->getModelSampleRate()
            << ", Latency: " << block.latencySamples << " samples");

        tempFile.deleteFile();
      } else {
        DBG("Failed to load NAM model - null DSP returned");
        block.loaded = false;
        tempFile.deleteFile();
      }
    } else {
      juce::AudioFormatManager formatManager;
      formatManager.registerBasicFormats();
      std::unique_ptr<juce::AudioFormatReader> reader(
          formatManager.createReaderFor(tempFile));

      if (!reader) {
        DBG("Failed to read IR file");
        tempFile.deleteFile();
        block.loaded = false;
        return;
      }

      const size_t maxIrLength = 32768;

      block.convolverLeft = std::make_unique<juce::dsp::Convolution>();
      block.convolverRight = std::make_unique<juce::dsp::Convolution>();

      double sampleRate = getSampleRate();
      int blockSize = maxBlockSize;
      juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(blockSize), 2};

      block.convolverLeft->prepare(spec);
      block.convolverRight->prepare(spec);

      block.convolverLeft->loadImpulseResponse(
          tempFile, juce::dsp::Convolution::Stereo::no,
          juce::dsp::Convolution::Trim::yes, maxIrLength,
          juce::dsp::Convolution::Normalise::no);

      block.convolverRight->loadImpulseResponse(
          tempFile, juce::dsp::Convolution::Stereo::no,
          juce::dsp::Convolution::Trim::yes, maxIrLength,
          juce::dsp::Convolution::Normalise::no);

      block.irTempFile = tempFile;

      block.irNormalizationGainLinear = computeIrNormalizationGain(tempFile, maxIrLength);
      block.irNormalizationSmoother.reset(sampleRate, 0.05f);
      block.irNormalizationSmoother.setCurrentAndTargetValue(block.irNormalizationGainLinear);

      block.loaded = true;
      DBG("IR loaded successfully");
    }
  } catch (const std::exception& e) {
    DBG("Error loading model data: " << e.what());
    block.loaded = false;
  }
}
