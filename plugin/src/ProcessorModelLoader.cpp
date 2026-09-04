#include "Processor.h"
#include "json.hpp"
#include "NAM/wavenet/a2_fast.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

// #####################################
// MODEL LOADING HELPERS
// #####################################

// IR block sizing constants.
// TONE3000 IR tones cover two very different species: cab IRs (tens of
// milliseconds) and convolution-reverb IRs (whole seconds). ONE length
// cutoff (kShortIrMaxSeconds) classifies every IR as short or long, and
// that classification drives everything downstream:
//   short: uniform zero-latency engine, -18 dB output pad, 100% default mix
//   long:  non-uniform engine,           no output pad,     50% default mix
namespace {

// Hard cap on loaded IR length. Bounds memory and engine-build time for
// arbitrary downloads while comfortably covering any published reverb IR
// (the longest cathedral tails run ~8 s).
constexpr double kMaxIrSeconds = 10.0;

// The short/long cutoff: cab IRs top out around 0.5 s, reverbs start well
// above 1 s; nothing meaningful lives at the boundary. IRs always convolve
// at the base rate (see ChainBlock::irBaseRateIsland), so the sample
// threshold is a constant.
constexpr double kShortIrMaxSeconds = 1.0;
constexpr int kShortIrMaxBaseSamples = static_cast<int>(kShortIrMaxSeconds * kChainBaseSampleRate);

// Long IRs use JUCE's two-stage non-uniform engine (still zero latency):
// the first kIrNonUniformHeadSamples convolve in callback-sized partitions,
// the tail in partitions of this size, so per-callback CPU stops scaling
// linearly with tail length. Bigger head = more per-callback FFT work;
// smaller = chunkier tail batches. 8192 (~170 ms) is a conventional
// reverb head size.
constexpr int kIrNonUniformHeadSamples = 8192;

// NAM phase-interleaved oversampling eligibility (see NamEngine.h).
// Phase interleaving is exact only for architectures whose temporal structure
// is pure (dilated) convolution: splitting the oversampled stream into factor
// phases and running one native-rate instance per phase computes the same
// math as one dilation-scaled instance at the high rate, and the temporal
// images a dilated kernel produces above the base Nyquist are removed by the
// chain's decimation filter. Recurrent models (LSTM) update state on
// consecutive samples and can't be phase-split; they get a single instance
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

// Gate for drop-loaded local NAM files: the catalog only serves
// architecture-2 models (the runtime is tuned around them: 48 kHz training
// rate, the slimmable tiers, the fast path), so local files must be A2 too.
// Same container shape as namConfigIsPhaseSafe: a bare A2 WaveNet, or a
// SlimmableContainer whose every submodel is one.
bool namConfigIsA2(const nlohmann::json& modelJson) {
  const std::string architecture = modelJson.value("architecture", "");
  if (architecture == "WaveNet") {
    int channels = 0;
    return modelJson.contains("config") &&
           nam::wavenet::a2_fast::is_a2_shape(modelJson["config"], &channels);
  }
  if (architecture == "SlimmableContainer") {
    if (!modelJson.contains("config") || !modelJson["config"].contains("submodels"))
      return false;
    const auto& submodels = modelJson["config"]["submodels"];
    if (!submodels.is_array() || submodels.empty())
      return false;
    for (const auto& entry : submodels) {
      if (!entry.contains("model") || !namConfigIsA2(entry["model"]))
        return false;
    }
    return true;
  }
  return false;
}

// Stash folder for drop-loaded local models. The block's toneJson persists
// the stash path as its model_url, so a cache-lost reload (undo after
// remove, undo across a tone swap) re-reads this copy even after the user's
// original file moved. Content-addressed names dedupe re-drops of the same
// file; stale entries age out (see cleanLocalModelStash). Same app-data
// root as PresetManager. Persisted URLs are resolved back to this folder by
// resolveLocalModelFile, which is what survives the iOS container rotating.
juce::File localModelsDir() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("LocalModels");
}

// FNV-1a over the file bytes: stable across sessions and platforms without
// pulling in juce_cryptography. The size suffix in the stash name backs up
// the (already negligible) collision odds.
juce::uint64 fnv1a64(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  juce::uint64 hash = 1469598103934665603ULL;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Some catalog WAVs end in an odd-sized chunk but omit the RIFF pad byte
// that odd chunks require (their header's RIFF size counts it, the file
// doesn't ship it). JUCE 9's WAV reader rejects such a chunk outright (its
// rounded-up length overruns the stream, tripping the malformed-chunk
// guard), so the file reads as zero samples: the convolver got an empty
// kernel and short cab IRs silently degraded to a dry passthrough. Every
// audio byte is present, though; detecting exactly this shape (declared
// RIFF size == real size + 1) lets one appended zero byte make the file
// spec-compliant without touching a sample.
bool wavMissingRiffPadByte(const void* data, size_t size) {
  if (size < 44)
    return false;
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0)
    return false;
  const auto declared = static_cast<juce::uint64>(juce::ByteOrder::littleEndianInt(bytes + 4));
  return declared + 8 == static_cast<juce::uint64>(size) + 1;
}

// Caps for local file loads, mirroring the web UI's drop limits
// (useToneLoadFlow.ts): the same rules must hold whether the bytes arrive
// as base64 over the bridge (drops) or straight from disk (the tile menus'
// Load File / Load Folder pickers).
constexpr juce::int64 kMaxLocalFileBytes = 50 * 1024 * 1024;
constexpr int kMaxFolderModels = 300;

// One local file's bytes: validate and stash a content-addressed copy.
// Validation happens here, at load time, instead of letting a bad file
// reach the background loader: its failure surfaces as a retry badge, which
// is the wrong affordance for a file that can never load. Returns the model
// object { id, name, model_url } for the synthetic tone, or void with
// `error` set to a user-facing message.
juce::var stashLocalBytes(const juce::String& filename, juce::MemoryOutputStream& decoded,
                          juce::String& error) {
  auto fail = [&](const juce::String& message) {
    juce::Logger::writeToLog("[LocalLoad] " + filename + ": " + message);
    error = message;
    return juce::var();
  };

  const juce::String extension = filename.fromLastOccurrenceOf(".", false, false).toLowerCase();
  const bool isNam = extension == "nam";
  if (!isNam && extension != "wav")
    return fail("Only .nam and .wav files are supported");

  if (isNam) {
    try {
      const auto* bytes = static_cast<const char*>(decoded.getData());
      const nlohmann::json config = nlohmann::json::parse(bytes, bytes + decoded.getDataSize());
      if (!namConfigIsA2(config))
        return fail("Only A2 NAM files are supported");
    } catch (const std::exception&) {
      return fail("Not a valid NAM file");
    }
  } else {
    // Repair before validating: a WAV missing its final RIFF pad byte would
    // otherwise be rejected here as unreadable. The repaired bytes are what
    // get stashed (and content-hashed), so downstream loads read a
    // spec-compliant file.
    if (wavMissingRiffPadByte(decoded.getData(), decoded.getDataSize()))
      decoded.writeByte(0);

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(decoded.getData(), decoded.getDataSize(),
                                                  false)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
      return fail("Not a valid WAV file");
  }

  const juce::uint64 hash = fnv1a64(decoded.getData(), decoded.getDataSize());
  const juce::File stash = localModelsDir().getChildFile(
      juce::String::toHexString(static_cast<juce::int64>(hash)) + "-" +
      juce::String(static_cast<juce::int64>(decoded.getDataSize())) + "." + extension);
  if (!stash.existsAsFile()) {
    stash.getParentDirectory().createDirectory();
    if (!stash.replaceWithData(decoded.getData(), decoded.getDataSize()))
      return fail("Couldn't store the dropped file");
  } else {
    // Re-drop of known bytes: refresh the GC liveness stamp (see
    // cleanLocalModelStash).
    stash.setLastModificationTime(juce::Time::getCurrentTime());
  }

  // Positive, content-stable model id. It's only ever compared within its
  // own block (cache key, activeModelId), so collisions across different
  // files would be harmless anyway.
  juce::DynamicObject::Ptr model = new juce::DynamicObject();
  model->setProperty("id", static_cast<int>(hash % 0x7ffffffe) + 1);
  model->setProperty("name", filename.upToLastOccurrenceOf(".", false, false));
  model->setProperty("model_url", juce::URL(stash).toString(false));
  return juce::var(model.get());
}

// A dropped file as shipped by the webview: { name, data } with base64
// bytes (the DOM never exposes file paths, so drops ride the bridge as
// base64; see useToneLoadFlow.ts).
juce::var stashLocalFile(const juce::String& filename, const juce::String& base64Data,
                         juce::String& error) {
  juce::MemoryOutputStream decoded;
  if (!juce::Base64::convertFromBase64(decoded, base64Data) || decoded.getDataSize() == 0) {
    juce::Logger::writeToLog("[LocalLoad] " + filename + ": Couldn't read the dropped file");
    error = "Couldn't read the dropped file";
    return {};
  }
  return stashLocalBytes(filename, decoded, error);
}

// A file native already has on disk (the tile menus' file picker flow;
// no base64 round-trip).
juce::var stashLocalFileFromDisk(const juce::File& file, juce::String& error) {
  juce::MemoryOutputStream bytes;
  juce::FileInputStream in(file);
  if (!in.openedOk() || bytes.writeFromInputStream(in, -1) <= 0) {
    juce::Logger::writeToLog("[LocalLoad] " + file.getFullPathName() + ": Couldn't read the file");
    error = "Couldn't read the file";
    return {};
  }
  return stashLocalBytes(file.getFileName(), bytes, error);
}

// A file the OS document picker handed us as a security-scoped URL.
//
// iOS is the reason this exists, and iOS is the only caller (see
// pickLocalToneFile); it is compiled everywhere so the DSP suite, which does
// not build for iOS, can exercise the same code the iPad runs. Everything the
// picker returns from the Files app lives outside the app sandbox (an iCloud /
// file-provider container), and
// the app is only allowed to touch it through the security scope JUCE's
// FileChooser opened and stored as a bookmark. Reading the raw path with a
// FileInputStream, which is what stashLocalFileFromDisk does and what every
// desktop build correctly does, is refused by the sandbox and surfaces as
// "Couldn't read the file". juce::URL::createInputStream goes through the
// bookmark and the scope, so it reads the same bytes the user actually picked.
juce::var stashLocalFileFromUrl(const juce::URL& url, juce::String& error) {
  const juce::String filename = TONE3000Processor::localFileNameFromUrl(url);
  juce::MemoryOutputStream bytes;
  const auto in = url.createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
  if (in == nullptr || bytes.writeFromInputStream(*in, -1) <= 0) {
    juce::Logger::writeToLog("[LocalLoad] " + filename + ": Couldn't read the file");
    error = "Couldn't read the file";
    return {};
  }
  return stashLocalBytes(filename, bytes, error);
}

// Shared user-facing error result for the local-load entry points.
juce::var localToneError(const juce::String& title, const juce::String& message) {
  juce::Logger::writeToLog("[LocalLoad] " + title + ": " + message);
  juce::DynamicObject::Ptr err = new juce::DynamicObject();
  err->setProperty("error", message);
  return juce::var(err.get());
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
  // runs there, see ChainBlock::irBaseRateIsland) and, with Normalise::no,
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
  // absurdly hot files: -48 dB rather than -24 because a dense
  // multi-second reverb IR peaked at 0 dBFS legitimately carries 25-35 dB
  // more energy than a cab hit.
  const double linear = 1.0 / l2norm;
  const double linearClamped =
      juce::jlimit(juce::Decibels::decibelsToGain(-48.0), 1.0, linear);

  return static_cast<float>(linearClamped);
}

juce::var TONE3000Processor::loadLocalTone(const juce::String& title, const juce::var& files,
                                           const std::string& targetInsertId) {
  const auto* fileArray = files.getArray();
  if (fileArray == nullptr || fileArray->isEmpty())
    return localToneError(title, "Nothing to load");

  // A folder with some bad files still loads the good ones; only when
  // nothing survives does the first file's error surface (which for a
  // single-file drop is exactly that file's error).
  juce::Array<juce::var> models;
  juce::String firstError;
  for (const auto& file : *fileArray) {
    juce::String error;
    const juce::var model = stashLocalFile(file["name"].toString(), file["data"].toString(), error);
    if (!model.isObject()) {
      if (firstError.isEmpty())
        firstError = error;
      continue;
    }
    models.add(model);
  }

  return finishLocalToneLoad(title, models, firstError, fileArray->size(), targetInsertId);
}

juce::var TONE3000Processor::loadLocalTonePath(const juce::File& source,
                                               const std::string& targetInsertId) {
  if (source.isDirectory()) {
    // Folder rules mirror the UI's folder drop (useToneLoadFlow.ts): every
    // .nam/.wav under the folder (subfolders included), the majority
    // extension decides NAM vs IR, everything else is ignored. Extension
    // matching is by lowercased name, not findChildFiles wildcards, which
    // are case-sensitive on Linux.
    const juce::String title = source.getFileName();
    juce::Array<juce::File> nams, wavs;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true)) {
      const juce::String extension = file.getFileExtension().toLowerCase();
      if (extension == ".nam")
        nams.add(file);
      else if (extension == ".wav")
        wavs.add(file);
    }
    juce::Array<juce::File>& picked = nams.size() >= wavs.size() ? nams : wavs;

    if (picked.isEmpty())
      return localToneError(title, "No .nam or .wav files in the folder");
    if (picked.size() > kMaxFolderModels)
      return localToneError(title,
                            "Folder has too many files (max " + juce::String(kMaxFolderModels) +
                                ")");
    for (const auto& file : picked)
      if (file.getSize() > kMaxLocalFileBytes)
        return localToneError(title, "A file is too large");

    // Listing order is filesystem-dependent; natural name order keeps the
    // model list stable ("amp 2" before "amp 10"), like the UI's drop path.
    std::sort(picked.begin(), picked.end(), [](const juce::File& a, const juce::File& b) {
      return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    juce::Array<juce::var> models;
    juce::String firstError;
    for (const auto& file : picked) {
      juce::String error;
      const juce::var model = stashLocalFileFromDisk(file, error);
      if (!model.isObject()) {
        if (firstError.isEmpty())
          firstError = error;
        continue;
      }
      models.add(model);
    }

    return finishLocalToneLoad(title, models, firstError, picked.size(), targetInsertId);
  }

  const juce::String title = source.getFileNameWithoutExtension();
  const juce::String extension = source.getFileExtension().toLowerCase();
  if (extension != ".nam" && extension != ".wav")
    return localToneError(title, "Only .nam and .wav files are supported");
  if (source.getSize() > kMaxLocalFileBytes)
    return localToneError(title, "File is too large");

  juce::String error;
  const juce::var model = stashLocalFileFromDisk(source, error);
  if (!model.isObject())
    return localToneError(title, error);

  return finishLocalToneLoad(title, {model}, {}, 1, targetInsertId);
}

juce::var TONE3000Processor::loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                                               const std::string& targetInsertId) {
  // Multi-select stands in for the folder route on iOS: the document picker
  // can hand back a folder URL, but a security-scoped directory cannot be
  // enumerated through juce::URL (there is no listing API behind the
  // bookmark), so "Load Folder" asks for the files themselves instead. See
  // pickLocalToneFile. Not gated on JUCE_IOS so the DSP suite can run it;
  // the editor only reaches it on iOS.
  if (sources.isEmpty())
    return localToneError("Load Files", "Nothing to load");

  const juce::String title =
      sources.size() == 1
          ? localFileNameFromUrl(sources.getReference(0)).upToLastOccurrenceOf(".", false, false)
          : juce::String(sources.size()) + " files";

  if (sources.size() > kMaxFolderModels)
    return localToneError(
        title, "Too many files (max " + juce::String(kMaxFolderModels) + ")");

  // Natural name order, matching the folder and drop paths, so the model list
  // is stable regardless of the order the picker reports.
  juce::Array<juce::URL> picked(sources);
  std::sort(picked.begin(), picked.end(), [](const juce::URL& a, const juce::URL& b) {
    return localFileNameFromUrl(a).compareNatural(localFileNameFromUrl(b)) < 0;
  });

  juce::Array<juce::var> models;
  juce::String firstError;
  // No extension check here: stashLocalBytes rejects anything but .nam and
  // .wav by name before it looks at the bytes, with the same message.
  for (const auto& url : picked) {
    juce::String error;
    const juce::var model = stashLocalFileFromUrl(url, error);
    if (!model.isObject()) {
      if (firstError.isEmpty())
        firstError = error;
      continue;
    }
    models.add(model);
  }

  if (models.isEmpty())
    return localToneError(title, firstError.isEmpty() ? "Couldn't read the file" : firstError);

  return finishLocalToneLoad(title, models, firstError, picked.size(), targetInsertId);
}

juce::var TONE3000Processor::finishLocalToneLoad(const juce::String& title,
                                                 const juce::Array<juce::var>& stashedModels,
                                                 const juce::String& firstError, int fileCount,
                                                 const std::string& targetInsertId) {
  // Identical bytes under two names would collide on the content-derived
  // id (cache key, picker selection); the first name wins.
  juce::Array<juce::var> models;
  for (const auto& model : stashedModels) {
    const int modelId = model["id"];
    const bool duplicate =
        std::any_of(models.begin(), models.end(),
                    [modelId](const juce::var& m) { return static_cast<int>(m["id"]) == modelId; });
    if (!duplicate)
      models.add(model);
  }

  if (models.isEmpty())
    return localToneError(title, firstError.isNotEmpty() ? firstError : "No loadable files");

  // Every model in a local tone shares one format (folder loads keep only
  // the majority extension); the first stash URL names it.
  const bool isNam = models.getReference(0)["model_url"].toString().endsWithIgnoreCase(".nam");

  juce::DynamicObject::Ptr tone = new juce::DynamicObject();
  tone->setProperty("id", 0);
  tone->setProperty("local", true);
  tone->setProperty("title", title);
  tone->setProperty("format", isNam ? "nam" : "ir");
  tone->setProperty("models", models);

  const juce::String toneJson = juce::JSON::toString(juce::var(tone.get()));

  // A load targeting an existing tone tile replaces in place (same block id
  // + params). Insert-slot ids (and missing/stale ids) still go through
  // loadTone and consume/create a slot.
  if (!targetInsertId.empty() && swapTone(targetInsertId, toneJson)) {
    juce::Logger::writeToLog("[LocalLoad] Swapped '" + title + "' into block " +
                             juce::String(targetInsertId) + " (" + juce::String(models.size()) +
                             " of " + juce::String(fileCount) + " file(s))");
    juce::DynamicObject::Ptr ok = new juce::DynamicObject();
    ok->setProperty("blockId", juce::String(targetInsertId));
    return juce::var(ok.get());
  }

  const std::string blockId = loadTone(toneJson, targetInsertId);
  if (blockId.empty())
    return localToneError(title, "Couldn't add the block");

  juce::Logger::writeToLog("[LocalLoad] Loaded '" + title + "' into block " +
                           juce::String(blockId) + " (" + juce::String(models.size()) + " of " +
                           juce::String(fileCount) + " file(s))");
  juce::DynamicObject::Ptr ok = new juce::DynamicObject();
  ok->setProperty("blockId", juce::String(blockId));
  return juce::var(ok.get());
}

void TONE3000Processor::cleanLocalModelStash() {
  // The stash only has to outlive whatever references it *by path*: running
  // sessions' undo histories and not-yet-reloaded chains. Presets and DAW
  // state embed the bytes and re-heal the stash on load (see
  // refreshLocalStashCopy), and every use re-stamps the file's mtime, so a
  // week of no use means nothing alive points at it. Runs once per process,
  // off the constructor's thread (it does directory IO).
  static std::once_flag once;
  std::call_once(once, [] {
    juce::Thread::launch([] {
      const juce::Time cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days(7);
      for (const auto& file : localModelsDir().findChildFiles(juce::File::findFiles, false)) {
        if (file.getLastModificationTime() < cutoff && file.deleteFile())
          juce::Logger::writeToLog("[LocalLoad] Stash GC removed " + file.getFileName());
      }
    });
  });
}

void TONE3000Processor::cleanLeakedIrTempFiles() {
  // Runs once per process, off the constructor's thread (it does directory
  // IO), same pattern as cleanLocalModelStash.
  static std::once_flag once;
  std::call_once(once, [] {
    juce::Thread::launch([] {
      const int removed =
          sweepLeakedIrTempFiles(juce::File::getSpecialLocation(juce::File::tempDirectory));
      if (removed > 0)
        juce::Logger::writeToLog("[ModelLoader] Swept " + juce::String(removed) +
                                 " leaked IR temp file(s)");
    });
  });
}

int TONE3000Processor::sweepLeakedIrTempFiles(const juce::File& tempDir) {
  // Builds through v0.0.2 wrote one "<uuid>_ir.wav" per IR engine build into
  // the OS temp dir and never deleted it (github issue #25: hundreds of MB
  // after a session of preset browsing). The loader now scopes its temp file
  // to the engine build (see prepareBlockModelOffThread), so anything
  // matching the pattern is that legacy bloat or a crash leftover. Nothing
  // ever reads these files after the build, so deleting them is safe even
  // under a running older build; the hour of grace covers the only live
  // window, a concurrent instance mid-build, whose file is seconds old.
  const juce::Time cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours(1);
  int removed = 0;
  for (const auto& file : tempDir.findChildFiles(juce::File::findFiles, false, "*_ir.wav"))
    if (file.getLastModificationTime() < cutoff && file.deleteFile())
      ++removed;
  return removed;
}

juce::String TONE3000Processor::localFileNameFromUrl(const juce::URL& url) {
  // A file:// URL knows its own local file, which carries the decoded name;
  // anything else only has the escaped path component to unescape.
  if (url.isLocalFile())
    return url.getLocalFile().getFileName();
  return juce::URL::removeEscapeChars(url.getFileName());
}

juce::File TONE3000Processor::resolveLocalModelFile(const juce::File& stashRoot,
                                                    const juce::String& modelUrl) {
  const juce::URL url(modelUrl);
  if (!url.isLocalFile())
    return {};

  const juce::File stored = url.getLocalFile();
  if (stored.existsAsFile())
    return stored;

  // The stored path is gone. On iOS that is the normal case after a
  // reinstall or an app update: the data container's UUID rotates, so every
  // absolute path a preset, an undo snapshot or the saved app state baked in
  // points at a container that no longer exists, and the block came back as
  // "Download failed / Retry". Stash names are content hashes in one flat
  // folder, so re-rooting the name is exact, not a guess. On desktop the
  // root never moves, so a path this machine wrote still exists and comes
  // back untouched above. The one desktop case this does change is a preset
  // or saved state carrying a stash URL from another machine or account:
  // that path is gone here too, and the block now re-roots it into the local
  // stash (and refreshLocalStashCopy writes the embedded bytes there) instead
  // of reading from the embedded cache alone. Same bytes, one more file on
  // disk. Not gated on JUCE_IOS because that case is a repair, not a
  // regression.
  const juce::String name = stored.getFileName();
  if (name.isEmpty() || name == "." || name == "..")
    return stored;
  return stashRoot.getChildFile(name);
}

void TONE3000Processor::refreshLocalStashCopy(const juce::String& modelUrl,
                                              const std::vector<uint8_t>& bytes) {
  const juce::File stash = resolveLocalModelFile(localModelsDir(), modelUrl);
  if (stash == juce::File() || !stash.isAChildOf(localModelsDir()))
    return;

  if (stash.existsAsFile()) {
    // In use, so keep the GC away (mtime is its liveness signal).
    stash.setLastModificationTime(juce::Time::getCurrentTime());
    return;
  }

  // The bytes came from an embedded cache but the stash copy is gone (GC'd,
  // or a preset from another machine): put it back so paths that need the
  // file (undo of a remove, retry) keep working.
  stash.getParentDirectory().createDirectory();
  if (stash.replaceWithData(bytes.data(), bytes.size()))
    juce::Logger::writeToLog("[LocalLoad] Restored stash copy " + stash.getFileName());
}

std::vector<uint8_t> TONE3000Processor::fetchModelFromUrl(const juce::String& modelUrl) {
  juce::URL url(modelUrl);

  // Local-file models (drag-and-drop loads) resolve to their stash copy:
  // no network, no auth. See loadLocalTone.
  if (url.isLocalFile()) {
    // Through resolveLocalModelFile, not url.getLocalFile(): a path persisted
    // before an iOS reinstall names a container that is gone.
    const juce::File stash = resolveLocalModelFile(localModelsDir(), modelUrl);
    juce::MemoryBlock data;
    if (!stash.loadFileAsData(data) || data.getSize() == 0) {
      juce::Logger::writeToLog("[ModelLoader] Local model file missing or unreadable: " +
                               modelUrl);
      return {};
    }
    // In use, so keep the GC away (mtime is its liveness signal, see
    // cleanLocalModelStash).
    stash.setLastModificationTime(juce::Time::getCurrentTime());
    const auto* bytes = static_cast<const uint8_t*>(data.getData());
    return std::vector<uint8_t>(bytes, bytes + data.getSize());
  }

  // The TONE3000 API requires a Bearer token on `model_url` requests;
  // attach the latest token the UI handed us, if any. Anonymous fetches still
  // work for legacy public URLs, so we degrade gracefully when no token is set.
  // Chain the option builders since `InputStreamOptions` has no copy-assign.
  const juce::String token = getAccessToken();
  const juce::String extraHeaders =
      token.isNotEmpty() ? juce::String("Authorization: Bearer ") + token
                         : juce::String();
  if (token.isEmpty()) {
    // Happens when a restore-time load misses the embedded cache before the
    // UI has pushed the auth token; the API rejects anonymous model fetches.
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
    double namSlimSize) {
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
            "' can't be phase-interleaved; running time-scaled at ×" +
            juce::String(oversampleFactor));
      }

      std::vector<std::unique_ptr<nam::DSP>> instances;
      instances.reserve(static_cast<size_t>(instanceCount));
      for (int i = 0; i < instanceCount; ++i) {
        std::unique_ptr<nam::DSP> rawDsp = nam::get_dsp(config);
        if (!rawDsp) {
          juce::Logger::writeToLog("[ModelLoader] Failed to load NAM model: null DSP returned");
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

      // The chain domain runs everything at the chain rate. A2 models are
      // all trained at 48k; anything else is rare enough that we just run it
      // anyway and note it in the log (a slight pitch/tone shift beats
      // per-block resampling machinery for a case that ~never happens).
      if (std::abs(engine->getModelSampleRate() - kChainBaseSampleRate) > 0.1) {
        juce::Logger::writeToLog(
            "[ModelLoader] NAM model reports " + juce::String(engine->getModelSampleRate()) +
            " Hz; the chain runs at " + juce::String(chainSampleRate()) + " Hz regardless");
      }

      // The block's A2 size (no-op for non-slimmable models); prepare()
      // applies it.
      engine->setSlimmableSize(namSlimSize);
      engine->prepare(domainBlockSize);

      out.namEngine = std::move(engine);
      juce::Logger::writeToLog(
          "[ModelLoader] NAM model prepared, model sample rate: " +
          juce::String(out.namEngine->getModelSampleRate()) +
          (phaseSafe ? " (" + juce::String(instanceCount) + " phase instances)" : ""));

      out.success = true;
    } else {
      // IRs go through the JUCE convolution/format-reader API, which wants a
      // file. The TemporaryFile's random leaf name (plus the right extension)
      // keeps the model's display name (which may contain characters that are
      // illegal in file names) out of the path, and its destructor deletes
      // the file on every exit path (success, early return, exception).
      // Nothing needs the file after this scope: loadImpulseResponse copies
      // the bytes into memory when prepare() drains the convolver's queue,
      // and later re-prepares rebuild from that copy, never from the file.
      // (Files leaked here by older builds are swept at startup, see
      // cleanLeakedIrTempFiles.)
      const juce::TemporaryFile scopedIrFile("_ir.wav");
      const juce::File& tempFile = scopedIrFile.getFile();

      if (!tempFile.replaceWithData(modelData.data(), modelData.size())) {
        juce::Logger::writeToLog("[ModelLoader] Failed to create temporary IR file: " +
                                 tempFile.getFullPathName());
        return out;
      }

      // The bytes can come from any source (fresh download, model cache,
      // embedded DAW/preset state), so the pad-byte repair lives here, at
      // the last common point before JUCE reads the file.
      if (wavMissingRiffPadByte(modelData.data(), modelData.size())) {
        const char pad = 0;
        tempFile.appendData(&pad, 1);
        juce::Logger::writeToLog("[ModelLoader] Repaired missing RIFF pad byte: " + filename);
      }

      juce::AudioFormatManager formatManager;
      formatManager.registerBasicFormats();
      std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(tempFile));

      if (!reader) {
        juce::Logger::writeToLog("[ModelLoader] Failed to read IR file: " + filename);
        return out;
      }

      // The load cap is a time bound, not a fixed sample count; truncating
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
      // below; a cab IR padded with trailing silence must still classify
      // as short for the level logic.
      const int irLengthUpperBound = static_cast<int>(std::llround(
          static_cast<double>(fileSamplesToLoad) * kChainBaseSampleRate / fileSampleRate));
      const int irNumChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));

      // Engine by the short/long cutoff. The engine must be constructed
      // before the trimmed size is known, so this one decision uses the
      // pre-trim upper bound; a silence-padded cab merely lands on the
      // non-uniform engine, a CPU choice, not an audible one. All audible
      // logic (output pad, default mix) uses the trimmed length below.
      const bool engineLongIr = irLengthUpperBound > kShortIrMaxBaseSamples;
      auto makeConvolver = [engineLongIr] {
        return engineLongIr ? std::make_unique<juce::dsp::Convolution>(
                                  juce::dsp::Convolution::NonUniform{kIrNonUniformHeadSamples})
                            : std::make_unique<juce::dsp::Convolution>();
      };

      // IR convolution always runs at the base rate: when the chain is
      // oversampled, the block's island (ChainBlock::irBaseRateIsland) hands
      // the convolver base-rate frames. So the spec is factor-independent:
      // base rate, base block size.
      juce::dsp::ProcessSpec spec{kChainBaseSampleRate,
                                  static_cast<juce::uint32>(chainBaseBlockSize()), 2};

      // Load *before* prepare: prepare() drains the convolver's background
      // message queue synchronously, so the engine (FFT segmentation and
      // all) is fully built right here on the loader thread; the block can
      // never go live with its IR still initialising in the background.
      //
      // "Fully built" is not "fully installed", though: juce::dsp::Convolution
      // runs its own loader thread, and when that thread wins the race for
      // the pending-engine slot, prepare() finds it empty and the engine is
      // instead installed lazily on the *first process() call*, behind a
      // ~50 ms internal crossfade from dry (CrossoverMixer). Live, that dry
      // half-window is the un-cabbed signal bleeding through right as the
      // block's own fade-in ends. So after prepare(), run silence through
      // the convolver until that window has provably elapsed: the install
      // and its crossfade happen here on the loader thread, and the engine
      // goes live deterministically full-wet with silent state.
      auto elapseConvolverInstallFade = [&spec](juce::dsp::Convolution& convolver) {
        const int chunk = static_cast<int>(spec.maximumBlockSize);
        juce::AudioBuffer<float> silence(static_cast<int>(spec.numChannels), chunk);
        silence.clear();
        // 3× JUCE's 0.05 s install fade: margin over exactness, it's cheap.
        const int warmupSamples = static_cast<int>(kChainBaseSampleRate * 0.15);
        for (int done = 0; done < warmupSamples; done += chunk) {
          juce::dsp::AudioBlock<float> blockRef(silence);
          convolver.process(juce::dsp::ProcessContextReplacing<float>(blockRef));
        }
      };

      // Mono fallback convolver: IR channel 0 applied to every audio channel.
      auto convolverMono = makeConvolver();
      convolverMono->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes, maxIrFileSamples,
                                         juce::dsp::Convolution::Normalise::no);
      convolverMono->prepare(spec);
      elapseConvolverInstallFade(*convolverMono);
      out.convolverMono = std::move(convolverMono);

      // True-stereo convolver: only meaningful when the file actually has 2 channels.
      if (irNumChannels > 1) {
        auto convolverStereo = makeConvolver();
        convolverStereo->loadImpulseResponse(tempFile, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::yes, maxIrFileSamples,
                                             juce::dsp::Convolution::Normalise::no);
        convolverStereo->prepare(spec);
        elapseConvolverInstallFade(*convolverStereo);
        out.convolverStereo = std::move(convolverStereo);
      }

      // The engine was built synchronously above, so it can report the real
      // (trimmed + resampled) kernel length, the basis for the short/long
      // classification and the host tail report. Fall back to the pre-trim
      // bound defensively.
      const int engineIrSamples = out.convolverMono->getCurrentIRSize();
      const int irLengthBaseSamples = engineIrSamples > 0 ? engineIrSamples : irLengthUpperBound;

      out.irNumChannels = irNumChannels;
      out.irLengthBaseSamples = irLengthBaseSamples;
      out.irIsLong = irLengthBaseSamples > kShortIrMaxBaseSamples;
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

void TONE3000Processor::requestSwapFadeAndWait(const std::string& blockId, bool muteWetOnly) {
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
    block->swapMuteWet.store(muteWetOnly);
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
  if (!isAudioActive()) {
    // No callbacks → nothing to fade; the edit applies directly. But still
    // arm the mute with the gain snapped to its landed value: a device can
    // start *mid-edit* (launch-time state restore racing the audio device
    // open), and callbacks arriving then must come up silent and take the
    // wait-free skip in processBlock, not blast the half-restored chain or
    // block behind the splice's lock hold. Touching the smoother here is
    // safe: no callbacks are running.
    chainEditFadeGain.setCurrentAndTargetValue(0.0f);
    chainEditFadeDone.store(true);
    chainEditFadePending.store(true);
    return;
  }

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

void TONE3000Processor::releaseChainEditFadeWhenLoadsSettle() {
  // The deadline is a *stall* detector, not a total budget: every completed
  // load pushes it out again (see the job below), so a heavy preset that
  // rebuilds many engines holds the mute as long as loads keep landing. Only
  // when nothing has finished for this long (wedged network fetch, starved
  // pool) does the chain glide back in early; any block still loading then
  // passes through until it lands, its own splice-in fade covering the entry.
  constexpr juce::uint32 kStallMs = 2000;
  chainEditFadeHoldDeadlineMs.store(juce::Time::getMillisecondCounter() + kStallMs);

  // Arm the mute even when requestChainEditFadeAndWait skipped it (audio
  // idle): if callbacks resume mid-reload the chain comes up muted instead
  // of blasting dry input, and the waiter below bounds the hold either way.
  chainEditFadeDone.store(false);
  chainEditFadePending.store(true);

  // Generation captured *after* the restore queued its loads (and after the
  // ChainEditFade ctor bumped it): a newer fade session invalidates this
  // waiter, whoever owns that session then owns the release.
  const int generation = chainEditFadeHoldGeneration.load();

  // Whether this restore queued any loads is decided here, synchronously:
  // a fast cached load could land before the job's first scan, and its
  // splice-in entry still needs the grace period below.
  bool loadsQueued = false;
  {
    juce::ScopedLock lock(chainMutex);
    for (const auto& l : lanes)
      for (const auto& b : l)
        if (b->modelLoading)
          loadsQueued = true;
  }

  struct ReleaseJob : public juce::ThreadPoolJob {
    TONE3000Processor& processor;
    const int generation;
    const bool loadsQueued;

    ReleaseJob(TONE3000Processor& p, int gen, bool loads)
        : ThreadPoolJob("Chain Edit Fade Release"), processor(p), generation(gen),
          loadsQueued(loads) {}

    JobStatus runJob() override {
      constexpr juce::uint32 kStallMs = 2000;
      int lastLoadingCount = std::numeric_limits<int>::max();
      for (;;) {
        if (shouldExit())
          return jobHasFinished;  // pool teardown; audio is gone anyway
        {
          // Decision under chainMutex: restores queue loads and flip
          // modelLoading under this lock, so the scan can't interleave a
          // half-applied restore.
          juce::ScopedLock lock(processor.chainMutex);
          if (processor.chainEditFadeHoldGeneration.load() != generation)
            return jobHasFinished;  // a newer fade session owns the release

          int loadingCount = 0;
          for (const auto& l : processor.lanes)
            for (const auto& b : l)
              if (b->modelLoading)
                ++loadingCount;

          // Progress restarts the stall clock: each landed load buys the
          // remaining ones another window, so the hold scales with the
          // preset instead of cutting off mid-rebuild.
          if (loadingCount < lastLoadingCount && loadingCount > 0)
            processor.chainEditFadeHoldDeadlineMs.store(juce::Time::getMillisecondCounter() +
                                                        kStallMs);
          lastLoadingCount = loadingCount;

          if (loadingCount == 0 || juce::Time::getMillisecondCounter() >=
                                       processor.chainEditFadeHoldDeadlineMs.load())
            break;
        }
        juce::Thread::sleep(20);
      }

      // A freshly applied block enters through its own ~25 ms splice-in fade
      // *from dry* (wetFadeGain, the continuity-preserving entry for live
      // loads). Let that finish under the mute, or a partial-dry sliver of
      // the last block's entry overlaps the glide-in. Skipped when this
      // restore queued nothing; a settings-only restore releases
      // immediately.
      if (loadsQueued)
        juce::Thread::sleep(60);

      juce::ScopedLock lock(processor.chainMutex);
      if (processor.chainEditFadeHoldGeneration.load() == generation)
        processor.chainEditFadePending.store(false);
      return jobHasFinished;
    }
  };

  loadingThreadPool.addJob(new ReleaseJob(*this, generation, loadsQueued), true);
}

void TONE3000Processor::applyPreparedModelToChainBlock(ChainBlock& block, ChainBlockType newType,
                                                       PreparedBlockModel& prepared) {
  // Loaded state is part of what getChainState reports, so any outcome here
  // must wake up the UI's revision-gated poll.
  bumpChainRevision();

  // Whatever the outcome, this load attempt is over. Clearing the fade flag
  // lets the audio thread ramp the wet mix back up when there is something
  // to hear. Remember the fade's shape first; it decides how the new
  // engine fades in below.
  const bool muteWetSwap = block.swapFadePending.load() && block.swapMuteWet.load();
  block.modelLoading = false;
  block.swapFadePending.store(false);
  block.swapFadeDone.store(false);
  block.swapMuteWet.store(false);

  if (!prepared.success) {
    // Corrupt/unreadable model: surface the retry UI. The UI already shows
    // the new tone/model, so the block drops out of processing to match
    // (the caller's pre-apply fade already glided it to bypass); its old
    // engines are swapped out by the next successful load.
    block.loaded = false;
    block.loadFailed = true;
    return;
  }

  // An oversampling change can race an in-flight load: the engine was built
  // with the old factor's phase count and can't be re-prepared into the new
  // one. Drop it and re-queue; the rebuild reads the settled factor and
  // reuses the block's in-memory model cache (no network).
  if (newType == ChainBlockType::NAM && prepared.namEngine != nullptr &&
      prepared.namEngine->getOversampleFactor() != chainOversampleFactor.load()) {
    juce::Logger::writeToLog("[ModelLoader] Oversampling factor changed during prepare (×" +
                             juce::String(prepared.namEngine->getOversampleFactor()) + " -> ×" +
                             juce::String(chainOversampleFactor.load()) + "); re-queuing block " +
                             juce::String(block.id));
    block.modelLoading = true;
    queueActiveModelLoad(block);
    return;
  }

  // A restore-time prepare can race prepareToPlay: the engine may have been
  // sized off a stale (or defaulted) host config, and prepareChain can't have
  // covered it (the engine wasn't on the block yet). Re-prepare before it
  // goes live; feeding an engine more frames than it was prepared for is what
  // used to silently kill blocks on relaunch ("Processing failed").
  // (Convolver re-prepare rebuilds the FFT engine under chainMutex, heavier
  // for reverb-length IRs, but this path only fires on that startup race,
  // when audio has barely started.)
  const int requiredBlockSize = chainDomainBlockSize();
  if (prepared.preparedBlockSize < requiredBlockSize) {
    juce::Logger::writeToLog("[ModelLoader] Chain domain drifted during prepare (block " +
                             juce::String(prepared.preparedBlockSize) + " -> " +
                             juce::String(requiredBlockSize) + "); re-preparing");
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
  // `prepared`, and the caller destroys them after releasing chainMutex;
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

    // Re-assert the block's A2 size in case it changed while this engine
    // was downloading/preparing (a no-op retier when it didn't).
    block.namEngine->setSlimmableSize(block.namSlimSize);

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

    // The base-rate island around the convolvers: blocks added mid-session
    // were never seen by prepareChain, so (re)prepare it here with the same
    // capacity/factor arguments as the chain-wide oversampler. Cheap (three
    // small work buffers), and the swap-fade already has the wet path silent.
    block.irBaseRateIsland.prepare(chainOversampleFactor.load(),
                                   juce::jmax(1, chainBaseBlockSize()));

    // Fresh blocks (Select-flow loads) default their mix by IR length: long
    // IRs are reverbs/effects meant to be blended (half wet), short cab IRs
    // replace the signal (fully wet). Length is only known here, after the
    // download, so loadTone arms this one-shot flag instead of guessing
    // from tone metadata. Swaps/restores keep the user's mix.
    if (block.applyDefaultMixOnLoad)
      block.mixNormalized = block.irIsLong ? 0.5f : 1.0f;

    block.irNormalizationGainLinear = prepared.irNormalizationGainLinear;
    block.irNormalizationSmoother.reset(chainSampleRate(), 0.05f);
    block.irNormalizationSmoother.setCurrentAndTargetValue(block.irNormalizationGainLinear);

    block.loaded = true;
    block.loadFailed = false;
  } else {
    DBG("Prepared model type/engine mismatch; dropping block from processing");
    block.loaded = false;
    block.loadFailed = true;
    return;
  }

  // The set of live IR engines changed; keep the host-facing tail length in
  // sync (caller holds chainMutex through this whole apply).
  refreshIrTailLength();

  // First successful load done; later loads on this block (model switches,
  // tone swaps) must keep the user's mix.
  block.applyDefaultMixOnLoad = false;

  // Per-block smoothers: prepareChain only covers blocks that existed at
  // prepareToPlay, so (re)arm them here for blocks added mid-session; a
  // never-reset LinearSmoothedValue jumps instantly (zipper noise on the
  // first knob drag). Snapping to target is inaudible: the wet path is at
  // bypass right now (fade below / fresh block).
  block.inputGainSmoother.reset(chainSampleRate(), 0.05f);
  block.outputGainSmoother.reset(chainSampleRate(), 0.05f);
  block.mixSmoother.reset(chainSampleRate(), 0.05f);
  block.mixSmoother.setCurrentAndTargetValue(block.mixNormalized);

  // Splice-in fade: the new engine enters from silence instead of jumping
  // in mid-waveform, mirroring how the outgoing one left (see
  // requestSwapFadeAndWait / ChainBlock.h). A wet-mute swap fades back
  // through the wet-only mute gain; the dry share of the user's mix held
  // steady the whole time, so the un-processed input is never exposed. A
  // bypass-shaped entry (fresh block, or the fade never engaged) crossfades
  // in from dry via wetFadeGain, exactly as before.
  block.swapWetMuteGain.reset(chainSampleRate(), kWetFadeSeconds);
  if (muteWetSwap) {
    // wetFadeGain stayed at 1 through the whole swap (the audio thread owns
    // it and it wasn't part of this fade); leave it alone so a concurrent
    // power-off glide keeps its course.
    block.swapWetMuteGain.setCurrentAndTargetValue(0.0f);
  } else {
    block.swapWetMuteGain.setCurrentAndTargetValue(1.0f);
    block.wetFadeGain.reset(chainSampleRate(), kWetFadeSeconds);
    block.wetFadeGain.setCurrentAndTargetValue(0.0f);
  }
}

