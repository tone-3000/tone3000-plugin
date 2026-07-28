#include "Processor.h"
#include "json.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

// #####################################
// MODEL LOADING HELPERS
// #####################################

// ── IR block sizing constants ──
// TONE3000 IR tones cover two very different species: cab IRs (tens of
// milliseconds) and convolution-reverb IRs (whole seconds). ONE length
// cutoff — kShortIrMaxSeconds — classifies every IR as short or long, and
// that classification drives everything downstream:
//   short: uniform zero-latency engine, −18 dB output pad, 100% default mix
//   long:  non-uniform engine,           no output pad,     50% default mix
namespace {

// Hard cap on loaded IR length. Bounds memory and engine-build time for
// arbitrary downloads while comfortably covering any published reverb IR
// (the longest cathedral tails run ~8 s).
constexpr double kMaxIrSeconds = 10.0;

// The short/long cutoff: cab IRs top out around 0.5 s, reverbs start well
// above 1 s — nothing meaningful lives at the boundary. IRs always convolve
// at the base rate (see ChainBlock::irBaseRateIsland), so the sample
// threshold is a constant.
constexpr double kShortIrMaxSeconds = 1.0;
constexpr int kShortIrMaxBaseSamples = static_cast<int>(kShortIrMaxSeconds * kChainBaseSampleRate);

// Long IRs use JUCE's two-stage non-uniform engine (still zero latency):
// the first kIrNonUniformHeadSamples convolve in callback-sized partitions,
// the tail in partitions of this size — per-callback CPU stops scaling
// linearly with tail length. Bigger head = more per-callback FFT work;
// smaller = chunkier tail batches. 8192 (~170 ms) is a conventional
// reverb head size.
constexpr int kIrNonUniformHeadSamples = 8192;

// ── NAM phase-interleaved oversampling eligibility (see NamEngine.h) ──
// Phase interleaving is exact only for architectures whose temporal structure
// is pure (dilated) convolution: splitting the oversampled stream into factor
// phases and running one native-rate instance per phase computes the same
// math as one dilation-scaled instance at the high rate, and the temporal
// images a dilated kernel produces above the base Nyquist are removed by the
// chain's decimation filter. Recurrent models (LSTM) update state on
// consecutive samples and can't be phase-split — they get a single instance
// running time-scaled at the full chain rate instead.
bool namConfigIsPhaseSafe(const nlohmann::json& modelJson) {
  const std::string architecture = modelJson.value("architecture", "");
  if (architecture == "WaveNet" || architecture == "ConvNet" || architecture == "Linear")
    return true;
  if (architecture == "SlimmableContainer") {
    // Safe only when every submodel is (containers switch tiers at runtime).
    if (!modelJson.contains("config") || !modelJson["config"].contains("submodels"))
      return false;
    const auto& submodels = modelJson["config"]["submodels"];
    if (!submodels.is_array() || submodels.empty())
      return false;
    for (const auto& entry : submodels) {
      if (!entry.contains("model") || !namConfigIsPhaseSafe(entry["model"]))
        return false;
    }
    return true;
  }
  return false;  // LSTM and anything unknown
}

}  // namespace

float TONE3000Processor::computeIrNormalizationGain(const juce::File& irFile,
                                                    size_t maxIrFileSamples) {
  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));

  if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0) {
    return 1.0f;
  }

  const juce::int64 totalSamples =
      std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(maxIrFileSamples));
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

  // The convolver resamples the IR to the base rate (IR convolution always
  // runs there — see ChainBlock::irBaseRateIsland) and, with Normalise::no,
  // scales it by fileRate/baseRate to preserve the filter's magnitude
  // response; the net effect on kernel *energy* is one factor of
  // fileRate/baseRate. Fold that in so the gain matches what the engine
  // actually convolves regardless of the file's sample rate (a no-op for
  // 48 kHz files).
  const double rateScale =
      reader->sampleRate > 0.0 ? reader->sampleRate / kChainBaseSampleRate : 1.0;
  const double l2norm = std::sqrt(sumSquares * rateScale);
  if (!std::isfinite(l2norm) || l2norm <= 0.0) {
    return 1.0f;
  }

  // Attenuation-only unit-energy normalization: the wet path leaves the
  // block at roughly the dry level for cabs and reverbs alike. The 1.0
  // ceiling never boosts a quiet IR; the floor is a safety net against
  // absurdly hot files — −48 dB rather than −24 because a dense
  // multi-second reverb IR peaked at 0 dBFS legitimately carries 25–35 dB
  // more energy than a cab hit.
  const double linear = 1.0 / l2norm;
  const double linearClamped =
      juce::jlimit(juce::Decibels::decibelsToGain(-48.0), 1.0, linear);

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


int TONE3000Processor::chainBaseBlockSize() const noexcept {
  // Before prepareToPlay has reported the real host config (state-restore
  // loads race it at launch), assume a conservatively large host block so
  // engines prepared early can absorb whatever the device settles on.
  // prepareChain / applyPreparedModelToChainBlock re-prepare on drift.
  const int knownHostBlock = juce::jmax(maxBlockSize, getBlockSize());
  const int hostBlockSize = knownHostBlock > 0 ? knownHostBlock : 4096;
  const double sr = hostSampleRate > 0.0 ? hostSampleRate : kChainBaseSampleRate;
  // Upsampling hosts below 48k (e.g. 44.1k) yield MORE frames per boundary
  // callback than the host block size; never go below the host size either,
  // since the direct path (48k host) hands host-sized blocks straight through.
  const int baseFrames =
      static_cast<int>(std::ceil(static_cast<double>(hostBlockSize) * kChainBaseSampleRate / sr));
  return juce::jmax(hostBlockSize, baseFrames);
}

int TONE3000Processor::chainDomainBlockSize() const noexcept {
  // The oversampler hands the chain stage exactly factor× the base frames.
  return chainBaseBlockSize() * chainOversampleFactor.load(std::memory_order_relaxed);
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

      // Oversampling: phase-safe architectures get one native-rate instance
      // per phase (see NamEngine.h); anything else runs a single instance
      // time-scaled at the full chain rate.
      const int oversampleFactor = chainOversampleFactor.load();
      const bool phaseSafe = oversampleFactor > 1 && namConfigIsPhaseSafe(config);
      const int instanceCount = phaseSafe ? oversampleFactor : 1;
      if (oversampleFactor > 1 && !phaseSafe) {
        juce::Logger::writeToLog(
            "[ModelLoader] NAM architecture '" +
            juce::String(config.value("architecture", std::string("?"))) +
            "' can't be phase-interleaved — running time-scaled at ×" +
            juce::String(oversampleFactor));
      }

      std::vector<std::unique_ptr<nam::DSP>> instances;
      instances.reserve(static_cast<size_t>(instanceCount));
      for (int i = 0; i < instanceCount; ++i) {
        std::unique_ptr<nam::DSP> rawDsp = nam::get_dsp(config);
        if (!rawDsp) {
          juce::Logger::writeToLog("[ModelLoader] Failed to load NAM model — null DSP returned");
          return out;
        }
        if (rawDsp->NumInputChannels() != 1) {
          throw std::runtime_error("NAM model must have 1 input channel, but has " +
                                   std::to_string(rawDsp->NumInputChannels()));
        }
        if (rawDsp->NumOutputChannels() != 1) {
          throw std::runtime_error("NAM model must have 1 output channel, but has " +
                                   std::to_string(rawDsp->NumOutputChannels()));
        }
        instances.push_back(std::move(rawDsp));
      }

      auto engine = std::make_unique<NamEngine>(std::move(instances), oversampleFactor);
      out.namIsSlimmable = engine->isSlimmableModel();

      // The chain domain runs everything at the chain rate. A2 models are
      // all trained at 48k; anything else is rare enough that we just run it
      // anyway and note it in the log (a slight pitch/tone shift beats
      // per-block resampling machinery for a case that ~never happens).
      if (std::abs(engine->getModelSampleRate() - kChainBaseSampleRate) > 0.1) {
        juce::Logger::writeToLog(
            "[ModelLoader] NAM model reports " + juce::String(engine->getModelSampleRate()) +
            " Hz; the chain runs at " + juce::String(chainSampleRate()) + " Hz regardless");
      }

      const double clampedSlim =
          out.namIsSlimmable ? juce::jlimit(0.0, 1.0, namPersistedSlimmableSize) : 1.0;
      engine->setSlimmableSize(clampedSlim);
      engine->prepare(domainBlockSize);

      out.namEngine = std::move(engine);
      juce::Logger::writeToLog(
          "[ModelLoader] NAM model prepared — model sample rate: " +
          juce::String(out.namEngine->getModelSampleRate()) +
          (phaseSafe ? " (" + juce::String(instanceCount) + " phase instances)" : ""));

      out.success = true;
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

      // The load cap is a time bound, not a fixed sample count — truncating
      // a multi-second reverb IR audibly chops its decay. It is passed to
      // JUCE in *file-rate* samples (truncation happens before the convolver
      // resamples to the base rate).
      const double fileSampleRate =
          reader->sampleRate > 0.0 ? reader->sampleRate : kChainBaseSampleRate;
      const auto maxIrFileSamples = static_cast<size_t>(kMaxIrSeconds * fileSampleRate);
      const juce::int64 fileSamplesToLoad = std::min<juce::int64>(
          reader->lengthInSamples, static_cast<juce::int64>(maxIrFileSamples));
      // Upper bound on the engine's kernel length at the base rate; the
      // authoritative (trimmed) length is read back off the built engine
      // below — a cab IR padded with trailing silence must still classify
      // as short for the level logic.
      const int irLengthUpperBound = static_cast<int>(std::llround(
          static_cast<double>(fileSamplesToLoad) * kChainBaseSampleRate / fileSampleRate));
      const int irNumChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));

      // Engine by the short/long cutoff. The engine must be constructed
      // before the trimmed size is known, so this one decision uses the
      // pre-trim upper bound; a silence-padded cab merely lands on the
      // non-uniform engine — a CPU choice, not an audible one. All audible
      // logic (output pad, default mix) uses the trimmed length below.
      const bool engineLongIr = irLengthUpperBound > kShortIrMaxBaseSamples;
      auto makeConvolver = [engineLongIr] {
        return engineLongIr ? std::make_unique<juce::dsp::Convolution>(
                                  juce::dsp::Convolution::NonUniform{kIrNonUniformHeadSamples})
                            : std::make_unique<juce::dsp::Convolution>();
      };

      // IR convolution always runs at the base rate — when the chain is
      // oversampled, the block's island (ChainBlock::irBaseRateIsland) hands
      // the convolver base-rate frames. So the spec is factor-independent:
      // base rate, base block size.
      juce::dsp::ProcessSpec spec{kChainBaseSampleRate,
                                  static_cast<juce::uint32>(chainBaseBlockSize()), 2};

      // Load *before* prepare: prepare() drains the convolver's background
      // message queue synchronously, so the engine (FFT segmentation and
      // all) is fully built right here on the loader thread — the block can
      // never go live with its IR still initialising in the background.

      // Mono fallback convolver: IR channel 0 applied to every audio channel.
      auto convolverMono = makeConvolver();
      convolverMono->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes, maxIrFileSamples,
                                         juce::dsp::Convolution::Normalise::no);
      convolverMono->prepare(spec);
      out.convolverMono = std::move(convolverMono);

      // True-stereo convolver: only meaningful when the file actually has 2 channels.
      if (irNumChannels > 1) {
        auto convolverStereo = makeConvolver();
        convolverStereo->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::yes, maxIrFileSamples,
                                             juce::dsp::Convolution::Normalise::no);
        convolverStereo->prepare(spec);
        out.convolverStereo = std::move(convolverStereo);
      }

      // The engine was built synchronously above, so it can report the real
      // (trimmed + resampled) kernel length — the basis for the short/long
      // classification and the host tail report. Fall back to the pre-trim
      // bound defensively.
      const int engineIrSamples = out.convolverMono->getCurrentIRSize();
      const int irLengthBaseSamples = engineIrSamples > 0 ? engineIrSamples : irLengthUpperBound;

      out.irNumChannels = irNumChannels;
      out.irLengthBaseSamples = irLengthBaseSamples;
      out.irIsLong = irLengthBaseSamples > kShortIrMaxBaseSamples;
      out.irTempFile = tempFile;
      out.irNormalizationGainLinear = computeIrNormalizationGain(tempFile, maxIrFileSamples);

      juce::Logger::writeToLog(
          "[ModelLoader] IR prepared: " + juce::String(irNumChannels) + " ch, " +
          juce::String(irLengthBaseSamples / kChainBaseSampleRate, 2) + " s (" +
          (out.irIsLong ? "long" : "short") + ", norm " +
          juce::String(juce::Decibels::gainToDecibels(out.irNormalizationGainLinear), 1) + " dB)");
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

  // An oversampling change can race an in-flight load: the engine was built
  // with the old factor's phase count and can't be re-prepared into the new
  // one. Drop it and re-queue — the rebuild reads the settled factor and
  // reuses the block's in-memory model cache (no network).
  if (newType == ChainBlockType::NAM && prepared.namEngine != nullptr &&
      prepared.namEngine->getOversampleFactor() != chainOversampleFactor.load()) {
    juce::Logger::writeToLog("[ModelLoader] Oversampling factor changed during prepare (×" +
                             juce::String(prepared.namEngine->getOversampleFactor()) + " -> ×" +
                             juce::String(chainOversampleFactor.load()) + ") — re-queuing block " +
                             juce::String(block.id));
    block.modelLoading = true;
    queueActiveModelLoad(block);
    return;
  }

  // A restore-time prepare can race prepareToPlay: the engine may have been
  // sized off a stale (or defaulted) host config, and prepareChain can't have
  // covered it — the engine wasn't on the block yet. Re-prepare before it
  // goes live; feeding an engine more frames than it was prepared for is what
  // used to silently kill blocks on relaunch ("Processing failed").
  // (Convolver re-prepare rebuilds the FFT engine under chainMutex — heavier
  // for reverb-length IRs, but this path only fires on that startup race,
  // when audio has barely started.)
  const int requiredBlockSize = chainDomainBlockSize();
  if (prepared.preparedBlockSize < requiredBlockSize) {
    juce::Logger::writeToLog("[ModelLoader] Chain domain drifted during prepare (block " +
                             juce::String(prepared.preparedBlockSize) + " -> " +
                             juce::String(requiredBlockSize) + ") — re-preparing");
    if (prepared.namEngine != nullptr)
      prepared.namEngine->prepare(requiredBlockSize);
    // Convolvers always run at the base rate behind the block's island, so
    // only the base block size can have drifted.
    const juce::dsp::ProcessSpec spec{kChainBaseSampleRate,
                                      static_cast<juce::uint32>(chainBaseBlockSize()), 2};
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
    block.irLengthBaseSamples = 0;
    block.irIsLong = false;
    block.irTempFile = juce::File();

    // The block's size only means anything while a slimmable model is active,
    // so it survives non-slimmable loads untouched — toggling through a tone's
    // models keeps the block's lite/full tier for the next A2 model.
    block.namIsSlimmable = prepared.namIsSlimmable;
    block.namEngine->setSlimmableSize(
        block.namIsSlimmable ? block.namSlimmableSize : 1.0);

    block.namNormalizationSmoother.reset(chainSampleRate(), 0.05f);
    block.namNormalizationSmoother.setCurrentAndTargetValue(1.0f);
    block.loaded = true;
    block.loadFailed = false;

  } else if (newType == ChainBlockType::IR && prepared.convolverMono != nullptr) {
    block.type = newType;
    std::swap(block.namEngine, prepared.namEngine);            // null in, any old NAM out
    std::swap(block.convolverMono, prepared.convolverMono);
    std::swap(block.convolverStereo, prepared.convolverStereo);
    block.irNumChannels = prepared.irNumChannels;
    block.irLengthBaseSamples = prepared.irLengthBaseSamples;
    block.irIsLong = prepared.irIsLong;
    block.irTempFile = prepared.irTempFile;
    prepared.irTempFile = juce::File();

    // The base-rate island around the convolvers: blocks added mid-session
    // were never seen by prepareChain, so (re)prepare it here — same
    // capacity/factor arguments as the chain-wide oversampler. Cheap (three
    // small work buffers), and the swap-fade already has the wet path silent.
    block.irBaseRateIsland.prepare(chainOversampleFactor.load(),
                                   juce::jmax(1, chainBaseBlockSize()));

    // Fresh blocks (Select-flow loads) default their mix by IR length: long
    // IRs are reverbs/effects meant to be blended (half wet), short cab IRs
    // replace the signal (fully wet). Length is only known here — after the
    // download — so loadTone arms this one-shot flag instead of guessing
    // from tone metadata. Swaps/restores keep the user's mix.
    if (block.applyDefaultMixOnLoad)
      block.mixNormalized = block.irIsLong ? 0.5f : 1.0f;

    block.irNormalizationGainLinear = prepared.irNormalizationGainLinear;
    block.irNormalizationSmoother.reset(chainSampleRate(), 0.05f);
    block.irNormalizationSmoother.setCurrentAndTargetValue(block.irNormalizationGainLinear);

    block.loaded = true;
    block.loadFailed = false;
  } else {
    DBG("Prepared model type/engine mismatch — dropping block from processing");
    block.loaded = false;
    block.loadFailed = true;
    return;
  }

  // The set of live IR engines changed — keep the host-facing tail length in
  // sync (caller holds chainMutex through this whole apply).
  refreshIrTailLength();

  // First successful load done — later loads on this block (model switches,
  // tone swaps) must keep the user's mix.
  block.applyDefaultMixOnLoad = false;

  // Per-block smoothers: prepareChain only covers blocks that existed at
  // prepareToPlay, so (re)arm them here for blocks added mid-session — a
  // never-reset LinearSmoothedValue jumps instantly (zipper noise on the
  // first knob drag). Snapping to target is inaudible: the wet path is at
  // bypass right now (fade below / fresh block).
  block.inputGainSmoother.reset(chainSampleRate(), 0.05f);
  block.outputGainSmoother.reset(chainSampleRate(), 0.05f);
  block.mixSmoother.reset(chainSampleRate(), 0.05f);
  block.mixSmoother.setCurrentAndTargetValue(block.mixNormalized);

  // Splice-in fade: the new engine enters from bypass instead of jumping in
  // mid-waveform (the outgoing one faded to bypass before the swap — see
  // requestSwapFadeAndWait).
  block.wetFadeGain.reset(chainSampleRate(), kWetFadeSeconds);
  block.wetFadeGain.setCurrentAndTargetValue(0.0f);
}

