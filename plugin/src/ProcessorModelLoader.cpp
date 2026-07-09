#include "Processor.h"
#include "json.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

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
    juce::Logger::writeToLog("[ModelLoader] Failed to open stream for model URL (network down or "
                             "unreachable): " + modelUrl);
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
    juce::Logger::writeToLog("[ModelLoader] Downloaded 0 bytes from model URL: " + modelUrl);
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

  juce::Logger::writeToLog("[ModelLoader] Preparing " +
                           juce::String(type == ChainBlockType::NAM ? "NAM" : "IR") + " model: " +
                           filename + " (" + juce::String((juce::int64)modelData.size()) + " bytes)");

  const double srForNam = hostSampleRate;
  const int effectiveBlockSize = computeEffectiveNamPrepareBlockSize();

  try {
    if (type == ChainBlockType::NAM) {
      // Parse the .nam JSON directly from the downloaded bytes. Never round-trip
      // through a temp file here: nam::get_dsp(std::filesystem::path) built from
      // a JUCE UTF-8 string mis-decodes non-ASCII characters (model names, user
      // temp dirs) on Windows and the load fails silently.
      const nlohmann::json config =
          nlohmann::json::parse(modelData.begin(), modelData.end());
      std::unique_ptr<nam::DSP> rawDsp = nam::get_dsp(config);

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
          juce::Logger::writeToLog(
              "[ModelLoader] Deferring NamResampler::prepare — invalid sample rate/block size");
          return out;
        }

        out.namResampler = std::move(resampler);
        juce::Logger::writeToLog("[ModelLoader] NAM model prepared — sample rate: " +
                                 juce::String(out.namResampler->getModelSampleRate()) +
                                 ", latency: " + juce::String(out.namLatencySamples));

        out.success = true;
      } else {
        juce::Logger::writeToLog("[ModelLoader] Failed to load NAM model — null DSP returned");
      }
    } else {
      // IRs go through the JUCE convolution/format-reader API, which wants a
      // file. Use a UUID-only leaf name (plus the right extension) so the
      // model's display name — which may contain characters that are illegal
      // in file names — never ends up in the path.
      juce::File tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile(juce::Uuid().toString() + "_ir.wav");

      if (!tempFile.replaceWithData(modelData.data(), modelData.size())) {
        juce::Logger::writeToLog("[ModelLoader] Failed to create temporary IR file: " +
                                 tempFile.getFullPathName());
        return out;
      }

      juce::AudioFormatManager formatManager;
      formatManager.registerBasicFormats();
      std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(tempFile));

      if (!reader) {
        juce::Logger::writeToLog("[ModelLoader] Failed to read IR file: " + filename);
        tempFile.deleteFile();
        return out;
      }

      const size_t maxIrLength = 32768;
      const int irNumChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));

      const double sampleRate = getSampleRate();
      const int convolutionBlockSize = effectiveBlockSize;
      juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(convolutionBlockSize), 2};

      // Mono fallback convolver: IR channel 0 applied to every audio channel.
      auto convolverMono = std::make_unique<juce::dsp::Convolution>();
      convolverMono->prepare(spec);
      convolverMono->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes, maxIrLength,
                                         juce::dsp::Convolution::Normalise::no);
      out.convolverMono = std::move(convolverMono);

      // True-stereo convolver: only meaningful when the file actually has 2 channels.
      if (irNumChannels > 1) {
        auto convolverStereo = std::make_unique<juce::dsp::Convolution>();
        convolverStereo->prepare(spec);
        convolverStereo->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::yes, maxIrLength,
                                             juce::dsp::Convolution::Normalise::no);
        out.convolverStereo = std::move(convolverStereo);
      }

      out.irNumChannels = irNumChannels;
      out.irTempFile = tempFile;
      out.irNormalizationGainLinear = computeIrNormalizationGain(tempFile, maxIrLength);

      DBG("IR prepared successfully (" << irNumChannels << " channel"
                                       << (irNumChannels > 1 ? "s" : "") << ")");
      out.success = true;
    }
  } catch (const std::exception& e) {
    juce::Logger::writeToLog("[ModelLoader] Error preparing model '" + filename +
                             "': " + e.what());
    out.success = false;
    out.namResampler.reset();
    out.convolverMono.reset();
    out.convolverStereo.reset();
  }

  return out;
}

void TONE3000Processor::applyPreparedModelToChainBlock(ChainBlock& block,
                                                       PreparedBlockModel& prepared) {
  // Loaded state is part of what getChainState reports, so any outcome here
  // must wake up the UI's revision-gated poll.
  bumpChainRevision();

  if (!prepared.success) {
    block.loaded = false;
    return;
  }

  if (block.type == ChainBlockType::NAM && prepared.namResampler != nullptr) {
    block.convolverMono.reset();
    block.convolverStereo.reset();
    block.irNumChannels = 1;
    block.irTempFile = juce::File();

    block.namResampler = std::move(prepared.namResampler);
    block.namIsSlimmable = prepared.namIsSlimmable;
    if (!block.namIsSlimmable)
      block.namSlimmableSize = 1.0;
    block.namResampler->setSlimmableSize(
        block.namIsSlimmable ? block.namSlimmableSize : 1.0);
    block.latencySamples = prepared.namLatencySamples;
    block.loaded = true;

  } else if (block.type == ChainBlockType::IR && prepared.convolverMono != nullptr) {
    block.namResampler.reset();
    block.latencySamples = 0;

    block.convolverMono = std::move(prepared.convolverMono);
    block.convolverStereo = std::move(prepared.convolverStereo);
    block.irNumChannels = prepared.irNumChannels;
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

