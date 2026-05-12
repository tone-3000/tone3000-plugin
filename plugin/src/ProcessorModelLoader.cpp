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

  // The new TONE3000 API requires a Bearer token on `model_url` requests —
  // attach the latest token the UI handed us, if any. Anonymous fetches still
  // work for legacy public URLs, so we degrade gracefully when no token is set.
  // Chain the option builders since `InputStreamOptions` has no copy-assign.
  const juce::String token = getAccessToken();
  const juce::String extraHeaders =
      token.isNotEmpty() ? juce::String("Authorization: Bearer ") + token
                         : juce::String();

  auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(30000)
          .withExtraHeaders(extraHeaders);

  std::unique_ptr<juce::InputStream> stream(url.createInputStream(options));

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


int TONE3000Processor::computeEffectiveNamPrepareBlockSize() const noexcept {
  return juce::jmax(1, juce::jmax(maxBlockSize, getBlockSize()));
}

TONE3000Processor::PreparedBlockModel TONE3000Processor::prepareBlockModelOffThread(
    ChainBlockType type,
    const std::vector<uint8_t>& modelData,
    const juce::String& filename,
    double namPersistedSlimmableSize) {
  PreparedBlockModel out;

  if (modelData.empty()) {
    DBG("Cannot prepare empty model data");
    return out;
  }

  DBG("Preparing model off-thread: " << filename << " (" << modelData.size() << " bytes)");

  const double srForNam = hostSampleRate;
  const int effectiveBlockSize = computeEffectiveNamPrepareBlockSize();

  try {
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File tempFile = tempDir.getChildFile(uniqueSafeTempLeafName(filename));

    if (!tempFile.replaceWithData(modelData.data(), modelData.size())) {
      DBG("Failed to create temporary file");
      return out;
    }

    if (type == ChainBlockType::NAM) {
      std::unique_ptr<nam::DSP> rawDsp =
          nam::get_dsp(std::filesystem::path(tempFile.getFullPathName().toStdString()));

      if (rawDsp) {
        if (rawDsp->NumInputChannels() != 1) {
          throw std::runtime_error("NAM model must have 1 input channel, but has " +
                                   std::to_string(rawDsp->NumInputChannels()));
        }
        if (rawDsp->NumOutputChannels() != 1) {
          throw std::runtime_error("NAM model must have 1 output channel, but has " +
                                   std::to_string(rawDsp->NumOutputChannels()));
        }

        auto resampler =
            std::make_unique<NamResampler>(std::move(rawDsp), srForNam);
        out.namIsSlimmable = resampler->isSlimmableModel();

        double slimPersistForPrepare = namPersistedSlimmableSize;
        if (!out.namIsSlimmable)
          slimPersistForPrepare = 1.0;
        const double clampedSlim =
            out.namIsSlimmable ? juce::jlimit(0.5, 1.0, slimPersistForPrepare) : 1.0;
        resampler->setSlimmableSize(clampedSlim);

        if (srForNam > 0.0 && effectiveBlockSize > 0) {
          resampler->prepare(srForNam, effectiveBlockSize);
          out.namLatencySamples = resampler->getLatencySamples();
        } else {
          DBG("Deferring NamResampler::prepare until host sample rate / block size are valid");
          tempFile.deleteFile();
          return out;
        }

        out.namResampler = std::move(resampler);
        DBG("NAM model prepared - Sample rate: " << out.namResampler->getModelSampleRate()
                                               << ", Latency: " << out.namLatencySamples);

        tempFile.deleteFile();
        out.success = true;
      } else {
        DBG("Failed to load NAM model - null DSP returned");
        tempFile.deleteFile();
      }
    } else {
      juce::AudioFormatManager formatManager;
      formatManager.registerBasicFormats();
      std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(tempFile));

      if (!reader) {
        DBG("Failed to read IR file");
        tempFile.deleteFile();
        return out;
      }

      const size_t maxIrLength = 32768;
      auto convolverL = std::make_unique<juce::dsp::Convolution>();
      auto convolverR = std::make_unique<juce::dsp::Convolution>();

      const double sampleRate = getSampleRate();
      const int convolutionBlockSize = effectiveBlockSize;
      juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(convolutionBlockSize), 2};

      convolverL->prepare(spec);
      convolverR->prepare(spec);

      convolverL->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::yes, maxIrLength,
                                      juce::dsp::Convolution::Normalise::no);

      convolverR->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::yes, maxIrLength,
                                      juce::dsp::Convolution::Normalise::no);

      out.irTempFile = tempFile;
      out.irNormalizationGainLinear = computeIrNormalizationGain(tempFile, maxIrLength);
      out.convolverLeft = std::move(convolverL);
      out.convolverRight = std::move(convolverR);

      DBG("IR prepared successfully");
      out.success = true;
    }
  } catch (const std::exception& e) {
    DBG("Error preparing model data off-thread: " << e.what());
    out.success = false;
    out.namResampler.reset();
    out.convolverLeft.reset();
    out.convolverRight.reset();
  }

  return out;
}

void TONE3000Processor::applyPreparedModelToChainBlock(ChainBlock& block,
                                                       PreparedBlockModel& prepared) {
  if (!prepared.success) {
    block.loaded = false;
    return;
  }

  if (block.type == ChainBlockType::NAM && prepared.namResampler != nullptr) {
    block.convolverLeft.reset();
    block.convolverRight.reset();
    block.irTempFile = juce::File();

    block.namResampler = std::move(prepared.namResampler);
    block.namIsSlimmable = prepared.namIsSlimmable;
    if (!block.namIsSlimmable)
      block.namSlimmableSize = 1.0;
    block.namResampler->setSlimmableSize(
        block.namIsSlimmable ? block.namSlimmableSize : 1.0);
    block.latencySamples = prepared.namLatencySamples;
    block.loaded = true;

  } else if (block.type == ChainBlockType::IR && prepared.convolverLeft != nullptr &&
             prepared.convolverRight != nullptr) {
    block.namResampler.reset();
    block.latencySamples = 0;

    block.convolverLeft = std::move(prepared.convolverLeft);
    block.convolverRight = std::move(prepared.convolverRight);
    block.irTempFile = prepared.irTempFile;

    prepared.irTempFile = juce::File();

    block.irNormalizationGainLinear = prepared.irNormalizationGainLinear;
    const double sampleRate = getSampleRate();
    block.irNormalizationSmoother.reset(sampleRate, 0.05f);
    block.irNormalizationSmoother.setCurrentAndTargetValue(block.irNormalizationGainLinear);

    block.loaded = true;
  } else {
    DBG("Prepared model type/engine mismatch vs chain block — leaving block unloaded");
    block.loaded = false;
  }

  prepared = PreparedBlockModel();
}

void TONE3000Processor::loadModelData(ChainBlock& block,
                                      const std::vector<uint8_t>& modelData,
                                      const juce::String& filename) {
  PreparedBlockModel prepared =
      prepareBlockModelOffThread(block.type, modelData, filename, block.namSlimmableSize);
  applyPreparedModelToChainBlock(block, prepared);
}

