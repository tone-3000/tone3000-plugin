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
  if (token.isEmpty()) {
    // Happens when a restore-time load misses the embedded cache before the
    // UI has pushed the auth token — the API rejects anonymous model fetches.
    juce::Logger::writeToLog("[ModelLoader] Fetching model without auth token (may be rejected)");
  }

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

  // The connection timeout above only covers the connect; a stalled response
  // body would otherwise pin a loader thread forever (an eternal "loading"
  // block in the UI). Bound the whole download instead.
  const juce::uint32 readDeadline = juce::Time::getMillisecondCounter() + 120000;

  while (!stream->isExhausted()) {
    if (juce::Time::getMillisecondCounter() > readDeadline) {
      juce::Logger::writeToLog("[ModelLoader] Model download timed out: " + modelUrl);
      return {};
    }
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


int TONE3000Processor::chainDomainBlockSize() const noexcept {
  // Before prepareToPlay has reported the real host config (state-restore
  // loads race it at launch), assume a conservatively large host block so
  // engines prepared early can absorb whatever the device settles on.
  // prepareChain / applyPreparedModelToChainBlock re-prepare on drift.
  const int knownHostBlock = juce::jmax(maxBlockSize, getBlockSize());
  const int hostBlockSize = knownHostBlock > 0 ? knownHostBlock : 4096;
  const double sr = hostSampleRate > 0.0 ? hostSampleRate : kChainSampleRate;
  // Upsampling hosts below 48k (e.g. 44.1k) yield MORE frames per boundary
  // callback than the host block size; never go below the host size either,
  // since the direct path (48k host) hands host-sized blocks straight through.
  const int domainFrames =
      static_cast<int>(std::ceil(static_cast<double>(hostBlockSize) * kChainSampleRate / sr));
  return juce::jmax(hostBlockSize, domainFrames);
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

  const int domainBlockSize = chainDomainBlockSize();
  out.preparedBlockSize = domainBlockSize;

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

        auto engine = std::make_unique<NamEngine>(std::move(rawDsp));
        out.namIsSlimmable = engine->isSlimmableModel();

        // The chain domain runs everything at kChainSampleRate. A2 models are
        // all trained at 48k; anything else is rare enough that we just run it
        // at 48k anyway and note it in the log (a slight pitch/tone shift beats
        // per-block resampling machinery for a case that ~never happens).
        if (std::abs(engine->getModelSampleRate() - kChainSampleRate) > 0.1) {
          juce::Logger::writeToLog(
              "[ModelLoader] NAM model reports " + juce::String(engine->getModelSampleRate()) +
              " Hz; the chain runs at " + juce::String(kChainSampleRate) + " Hz regardless");
        }

        const double clampedSlim =
            out.namIsSlimmable ? juce::jlimit(0.0, 1.0, namPersistedSlimmableSize) : 1.0;
        engine->setSlimmableSize(clampedSlim);
        engine->prepare(domainBlockSize);

        out.namEngine = std::move(engine);
        juce::Logger::writeToLog("[ModelLoader] NAM model prepared — model sample rate: " +
                                 juce::String(out.namEngine->getModelSampleRate()));

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

      // IRs live in the chain domain too: the convolver resamples the IR file
      // to kChainSampleRate at load, and processing always runs at that rate.
      juce::dsp::ProcessSpec spec{kChainSampleRate, static_cast<juce::uint32>(domainBlockSize), 2};

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
    out.namEngine.reset();
    out.convolverMono.reset();
    out.convolverStereo.reset();
  }

  return out;
}

void TONE3000Processor::requestSwapFadeAndWait(const std::string& blockId) {
  // No callbacks running → nothing audible; the change can apply directly
  // (and a click gesture on the message thread shouldn't stall on the
  // timeout below).
  if (!isAudioActive())
    return;

  {
    juce::ScopedLock lock(chainMutex);
    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr)
      return;
    // Nothing audible to fade: fresh block, failed/unloaded block, powered
    // off, or no engine yet. The change can splice in directly.
    const bool audible = block->loaded && block->enabled &&
                         (block->namEngine != nullptr || block->convolverMono != nullptr);
    if (!audible)
      return;
    block->swapFadeDone.store(false);
    block->swapFadePending.store(true);
  }

  // The audio thread completes the fade within a few callbacks; the deadline
  // is a backstop for audio stopping mid-wait. Every path past this point
  // clears swapFadePending, so a timed-out fade can't leave the block
  // bypassed.
  const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 250;
  while (juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(2);
    juce::ScopedLock lock(chainMutex);
    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr || block->swapFadeDone.load())
      return;
  }
}

void TONE3000Processor::requestChainEditFadeAndWait() {
  if (!isAudioActive())
    return;  // no callbacks → the edit is inaudible; apply directly

  chainEditFadeDone.store(false);
  chainEditFadePending.store(true);

  // The caller (ChainEditFade) clears chainEditFadePending after its edit,
  // so a timed-out fade can't leave the chain muted.
  const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 250;
  while (juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(2);
    if (chainEditFadeDone.load())
      return;
  }
}

void TONE3000Processor::applyPreparedModelToChainBlock(ChainBlock& block, ChainBlockType newType,
                                                       PreparedBlockModel& prepared) {
  // Loaded state is part of what getChainState reports, so any outcome here
  // must wake up the UI's revision-gated poll.
  bumpChainRevision();

  // Whatever the outcome, this load attempt is over. Clearing the fade flag
  // lets the audio thread ramp the wet mix back up when there is something
  // to hear.
  block.modelLoading = false;
  block.swapFadePending.store(false);
  block.swapFadeDone.store(false);

  if (!prepared.success) {
    // Corrupt/unreadable model — surface the retry UI. The UI already shows
    // the new tone/model, so the block drops out of processing to match
    // (the caller's pre-apply fade already glided it to bypass); its old
    // engines are swapped out by the next successful load.
    block.loaded = false;
    block.loadFailed = true;
    return;
  }

  // A restore-time prepare can race prepareToPlay: the engine may have been
  // sized off a stale (or defaulted) host config, and prepareChain can't have
  // covered it — the engine wasn't on the block yet. Re-prepare before it
  // goes live; feeding an engine more frames than it was prepared for is what
  // used to silently kill blocks on relaunch ("Processing failed").
  const int requiredBlockSize = chainDomainBlockSize();
  if (prepared.preparedBlockSize < requiredBlockSize) {
    juce::Logger::writeToLog("[ModelLoader] Domain block size grew " +
                             juce::String(prepared.preparedBlockSize) + " -> " +
                             juce::String(requiredBlockSize) + " during prepare — re-preparing");
    if (prepared.namEngine != nullptr)
      prepared.namEngine->prepare(requiredBlockSize);
    const juce::dsp::ProcessSpec spec{kChainSampleRate,
                                      static_cast<juce::uint32>(requiredBlockSize), 2};
    if (prepared.convolverMono != nullptr)
      prepared.convolverMono->prepare(spec);  // keeps the loaded impulse
    if (prepared.convolverStereo != nullptr)
      prepared.convolverStereo->prepare(spec);
  }

  // Engines are *swapped*, not reset: the block's previous engines end up in
  // `prepared`, and the caller destroys them after releasing chainMutex —
  // NAM graph / convolution teardown must never run while the audio thread
  // can be blocked on the lock.
  if (newType == ChainBlockType::NAM && prepared.namEngine != nullptr) {
    block.type = newType;  // may differ while a tone swap was in flight
    std::swap(block.namEngine, prepared.namEngine);            // new engine in, old out
    std::swap(block.convolverMono, prepared.convolverMono);    // null in, any old IR out
    std::swap(block.convolverStereo, prepared.convolverStereo);
    block.irNumChannels = 1;
    block.irTempFile = juce::File();

    // The block's size only means anything while a slimmable model is active,
    // so it survives non-slimmable loads untouched — toggling through a tone's
    // models keeps the block's lite/full tier for the next A2 model.
    block.namIsSlimmable = prepared.namIsSlimmable;
    block.namEngine->setSlimmableSize(
        block.namIsSlimmable ? block.namSlimmableSize : 1.0);

    block.namNormalizationSmoother.reset(kChainSampleRate, 0.05f);
    block.namNormalizationSmoother.setCurrentAndTargetValue(1.0f);
    block.loaded = true;
    block.loadFailed = false;

  } else if (newType == ChainBlockType::IR && prepared.convolverMono != nullptr) {
    block.type = newType;
    std::swap(block.namEngine, prepared.namEngine);            // null in, any old NAM out
    std::swap(block.convolverMono, prepared.convolverMono);
    std::swap(block.convolverStereo, prepared.convolverStereo);
    block.irNumChannels = prepared.irNumChannels;
    block.irTempFile = prepared.irTempFile;
    prepared.irTempFile = juce::File();

    block.irNormalizationGainLinear = prepared.irNormalizationGainLinear;
    block.irNormalizationSmoother.reset(kChainSampleRate, 0.05f);
    block.irNormalizationSmoother.setCurrentAndTargetValue(block.irNormalizationGainLinear);

    block.loaded = true;
    block.loadFailed = false;
  } else {
    DBG("Prepared model type/engine mismatch — dropping block from processing");
    block.loaded = false;
    block.loadFailed = true;
    return;
  }

  // Per-block smoothers: prepareChain only covers blocks that existed at
  // prepareToPlay, so (re)arm them here for blocks added mid-session — a
  // never-reset LinearSmoothedValue jumps instantly (zipper noise on the
  // first knob drag). Snapping to target is inaudible: the wet path is at
  // bypass right now (fade below / fresh block).
  block.inputGainSmoother.reset(kChainSampleRate, 0.05f);
  block.outputGainSmoother.reset(kChainSampleRate, 0.05f);
  block.mixSmoother.reset(kChainSampleRate, 0.05f);
  block.mixSmoother.setCurrentAndTargetValue(block.mixNormalized);

  // Splice-in fade: the new engine enters from bypass instead of jumping in
  // mid-waveform (the outgoing one faded to bypass before the swap — see
  // requestSwapFadeAndWait).
  block.wetFadeGain.reset(kChainSampleRate, kWetFadeSeconds);
  block.wetFadeGain.setCurrentAndTargetValue(0.0f);
}

