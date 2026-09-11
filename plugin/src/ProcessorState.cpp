#include "Processor.h"

#include <cstring>

// #############################
// STATE PERSISTENCE
// #############################

// Machine-wide user settings.
// Shared PropertiesFile for preferences that belong to the machine, not the
// session/preset (multi-core processing, the default A2 size for new
// blocks). Same app-data root as PresetManager: ~/Library/Application
// Support/TONE3000 on macOS, %APPDATA%/TONE3000 on Windows,
// $XDG_CONFIG_HOME/TONE3000 (default ~/.config/TONE3000) on Linux.
namespace {

constexpr auto kMultiCoreKey = "multiCore";
constexpr auto kNamSlimSizeDefaultKey = "namSlimSizeDefault";
constexpr auto kWebInspectorKey = "webInspector";

// Magic prefix for the binary ValueTree state format (see getStateInformation).
constexpr char kStateMagic[] = {'T', '3', 'K', 'B'};

// Bump when the TONE3000State tree changes shape. Readers ignore state from a
// newer schema rather than guessing at it.
constexpr int kStateSchemaVersion = 1;

juce::PropertiesFile::Options userSettingsOptions() {
  juce::PropertiesFile::Options options;
  // getDefaultFile() uses applicationName as the filename stem. "preferences"
  // keeps this store distinct from the standalone holder's TONE3000.settings
  // in the same folder: two PropertiesFile instances on one file clobber each
  // other, since each save rewrites the whole file from its in-memory copy.
  options.applicationName = "preferences";
  options.filenameSuffix = ".settings";
  options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
  // PropertiesFile puts a bare folderName directly under ~ on Linux, so pass
  // the XDG config location as an absolute path instead (same root as
  // PresetManager, the logs and the WebKit storage).
  options.folderName =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("TONE3000")
          .getFullPathName();
#else
  options.folderName = "TONE3000";
#endif
  return options;
}

}  // namespace

juce::File TONE3000Processor::getSettingsFile() {
  return userSettingsOptions().getDefaultFile();
}

bool TONE3000Processor::readPersistedMultiCoreEnabled() {
  return juce::PropertiesFile(userSettingsOptions()).getBoolValue(kMultiCoreKey, true);
}

double TONE3000Processor::readPersistedNamSlimSizeDefault() {
  // 0.0 = lite, the shipped default (see ChainBlock::namSlimSize).
  return juce::jlimit(
      0.0, 1.0,
      juce::PropertiesFile(userSettingsOptions()).getDoubleValue(kNamSlimSizeDefaultKey, 0.0));
}

bool TONE3000Processor::readPersistedWebInspectorEnabled() {
  // Debug builds already get the inspector from stock JUCE; default on so a
  // fresh debug install still has Inspect Element / Reload. Release stays off
  // until Settings -> Diagnostics flips it.
  return juce::PropertiesFile(userSettingsOptions())
      .getBoolValue(kWebInspectorKey,
#if JUCE_DEBUG
                    true
#else
                    false
#endif
      );
}

void TONE3000Processor::persistWebInspectorEnabled(bool enabled) {
  juce::PropertiesFile settings(userSettingsOptions());
  settings.setValue(kWebInspectorKey, enabled);
  settings.saveIfNeeded();
  juce::Logger::writeToLog(juce::String("[Processor] Web Inspector ") +
                           (enabled ? "enabled" : "disabled"));
}

void TONE3000Processor::setMultiCoreEnabled(bool enabled, bool persist) {
  if (multiCoreEnabled.load() == enabled)
    return;

  // No fade, no lock: the flag only picks serial vs. parallel scheduling for
  // the next callback, and both schedules produce bit-identical audio.
  multiCoreEnabled.store(enabled);
  if (persist) {
    juce::PropertiesFile settings(userSettingsOptions());
    settings.setValue(kMultiCoreKey, enabled);
    settings.saveIfNeeded();
  }

  juce::Logger::writeToLog(juce::String("[Processor] Multi-core processing ") +
                           (enabled ? "enabled" : "disabled"));
  bumpChainRevision();
}
void TONE3000Processor::setNamSlimSizeDefault(double slimSize) {
  slimSize = juce::jlimit(0.0, 1.0, slimSize);
  if (namSlimSizeDefault.load() == slimSize)
    return;

  // No fade, no lock: loaded engines are untouched (each block owns its
  // size); this only decides what loadTone stamps on the next new block.
  namSlimSizeDefault.store(slimSize);
  juce::PropertiesFile settings(userSettingsOptions());
  settings.setValue(kNamSlimSizeDefaultKey, slimSize);
  settings.saveIfNeeded();

  juce::Logger::writeToLog("[Processor] Default NAM A2 size set to " + juce::String(slimSize));
  bumpChainRevision();
}

juce::ValueTree TONE3000Processor::serializeBlockSettings(const ChainBlock& block) {
  juce::ValueTree blockState("ChainBlock");

  blockState.setProperty("id", juce::String(block.id), nullptr);
  blockState.setProperty("type", chainBlockTypeToString(block.type), nullptr);
  blockState.setProperty("enabled", block.enabled, nullptr);
  blockState.setProperty("normalize", block.normalizeEnabled, nullptr);
  blockState.setProperty("slimSize", block.namSlimSize, nullptr);
  blockState.setProperty("inputGain", block.inputGainNormalized, nullptr);
  blockState.setProperty("outputGain", block.outputGainNormalized, nullptr);
  blockState.setProperty("mix", block.mixNormalized, nullptr);
  blockState.setProperty("irCategory", irCategoryToString(block.irCategory), nullptr);

  if (block.type != ChainBlockType::INSERT) {
    blockState.setProperty("toneId", block.toneId, nullptr);
    blockState.setProperty("toneJson", block.toneJson, nullptr);
    blockState.setProperty("activeModelId", block.activeModelId, nullptr);
    blockState.appendChild(block.eq.toValueTree(), nullptr);
  }

  return blockState;
}

void TONE3000Processor::applyBlockSettings(ChainBlock& block, const juce::ValueTree& blockState) {
  block.enabled = static_cast<bool>(blockState.getProperty("enabled", true));
  block.normalizeEnabled = static_cast<bool>(blockState.getProperty("normalize", true));
  block.inputGainNormalized = static_cast<float>(blockState.getProperty("inputGain", 0.5f));
  block.outputGainNormalized = static_cast<float>(blockState.getProperty("outputGain", 0.5f));
  block.mixNormalized = static_cast<float>(blockState.getProperty("mix", 1.0f));

  // Empty ("" sentinel, via getProperty's default) means state saved before
  // this field existed: seed it once the model (re)loads and its real
  // content can be scanned, exactly like a fresh local-file load - never
  // just default to IrPlayer, which would silently change an old cab
  // block's pad/mix on next restore (see
  // ChainBlock::irCategoryNeedsDurationGuess). A present value is a real,
  // already-resolved setting; mixNormalized above is left exactly as saved
  // either way.
  const juce::String irCategoryStr = blockState.getProperty("irCategory", "").toString();
  if (irCategoryStr.isNotEmpty()) {
    block.irCategory = irCategoryFromString(irCategoryStr);
    block.irCategoryNeedsDurationGuess = false;
  } else if (block.type == ChainBlockType::IR) {
    block.irCategoryNeedsDurationGuess = true;
  }

  // States from before per-block sizes restore as lite (0.0). An engine the
  // restore keeps loaded (see reconcileChainFromTree) retiers in place: the
  // NAM container fast-paths an unchanged tier, and every restore path runs
  // under the chain-edit fade, so a real retier splices in silently.
  block.namSlimSize =
      juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("slimSize", 0.0)));
  if (block.namEngine != nullptr)
    block.namEngine->setSlimmableSize(block.namSlimSize);

  if (block.type != ChainBlockType::INSERT) {
    // A missing Eq child restores as flat. Block EQs always run in the chain
    // domain (fixed rate).
    block.eq.restoreFromValueTree(blockState.getChildWithName("Eq"));
    block.eq.prepare(chainSampleRate());
  }
}

void TONE3000Processor::serializeChainToTree(
    const std::vector<std::unique_ptr<ChainBlock>>& blocks, juce::ValueTree& chainState,
    bool includeModelData) {
  for (const auto& block : blocks) {
    juce::ValueTree blockState = serializeBlockSettings(*block);

    if (includeModelData && block->type != ChainBlockType::INSERT) {
      juce::ValueTree cacheState("ModelCache");
      for (const auto& [modelId, modelData] : block->modelCache) {
        juce::ValueTree cachedModel("CachedModel");
        cachedModel.setProperty("modelId", modelId, nullptr);

        // Raw bytes in a binary var. The ValueTree binary stream writes these
        // verbatim, which matters because this can run with chainMutex held
        // (~8 MB per heavy rig).
        cachedModel.setProperty(
            "data", juce::var(juce::MemoryBlock(modelData.data(), modelData.size())), nullptr);

        cacheState.appendChild(cachedModel, nullptr);
      }
      blockState.appendChild(cacheState, nullptr);
    }

    chainState.appendChild(blockState, nullptr);
  }
}

void TONE3000Processor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree state("TONE3000State");
  state.setProperty("schemaVersion", kStateSchemaVersion, nullptr);

  state.appendChild(parameters.copyState(), nullptr);

  // Session-only settings. Presets deliberately don't carry these: input
  // mode is I/O routing, editor scale is a workstation preference, and the
  // MIDI map describes the user's rig, not the tone. (Per-block NAM A2
  // sizes ride the chain snapshot below, with presets and undo.)
  state.setProperty("inputMode", inputModeToString(getInputMode()), nullptr);
  state.setProperty("editorScale", editorScale.load(), nullptr);
  state.setProperty("editorExtraHeight", editorExtraHeight.load(), nullptr);
  state.appendChild(midiMapper.toValueTree(), nullptr);

  {
    juce::ScopedLock lock(chainMutex);
    state.setProperty("activePresetId", activePresetId, nullptr);
    state.setProperty("activePresetName", activePresetName, nullptr);
    // The same ChainSnapshot tree that undo and presets use, with model bytes
    // embedded so the project reopens offline.
    state.appendChild(captureChainSnapshot(true), nullptr);
  }

  // Magic-prefixed binary ValueTree stream. I picked binary over XML so the
  // embedded model bytes go out verbatim; the old Base64-in-XML path burned
  // 100+ ms per host save on a heavy rig.
  juce::MemoryOutputStream out(destData, false);
  out.write(kStateMagic, sizeof(kStateMagic));
  state.writeToStream(out);
  DBG("Plugin state saved successfully");
}

void TONE3000Processor::setStateInformation(const void* data, int sizeInBytes) {
  juce::ValueTree state;
  if (sizeInBytes > static_cast<int>(sizeof(kStateMagic)) &&
      std::memcmp(data, kStateMagic, sizeof(kStateMagic)) == 0) {
    state = juce::ValueTree::readFromData(
        static_cast<const char*>(data) + sizeof(kStateMagic),
        static_cast<size_t>(sizeInBytes) - sizeof(kStateMagic));
  }

  if (!state.isValid()) {
    juce::Logger::writeToLog("[Restore] Failed to parse plugin state (" +
                             juce::String(sizeInBytes) + " bytes)");
    return;
  }

  if (static_cast<int>(state.getProperty("schemaVersion", 1)) > kStateSchemaVersion) {
    juce::Logger::writeToLog("[Restore] State schema is newer than this build; ignoring");
    return;
  }

  const juce::ValueTree snapshot = state.getChildWithName("ChainSnapshot");
  juce::Logger::writeToLog(
      "[Restore] Restoring state (" + juce::String(sizeInBytes) + " bytes, " +
      juce::String(snapshot.getChildWithName("ChainBlocks").getNumChildren()) + " left / " +
      juce::String(snapshot.getChildWithName("RightChainBlocks").getNumChildren()) +
      " right blocks)");

  juce::ValueTree parameterState = state.getChildWithName("PARAMETERS");
  if (parameterState.isValid()) {
    parameters.replaceState(parameterState);
    DBG("Parameters restored from state");
  }

  inputMode.store(static_cast<int>(
      inputModeFromString(state.getProperty("inputMode").toString())));

  // Older projects have no editorScale; keep the 1x default. The editor
  // clamps to its supported range when it reads this.
  editorScale.store(static_cast<double>(state.getProperty("editorScale", 1.0)));
  // Default matches the UI's default-visible hint bar (see Processor.h).
  editorExtraHeight.store(static_cast<int>(state.getProperty("editorExtraHeight", 36)));

  // A missing child clears the map; a project without mappings must not
  // inherit the previous session's.
  midiMapper.restoreFromValueTree(state.getChildWithName("MidiMappings"));

  // A project load is a reconciling restore: matching blocks keep their
  // loaded engines, everything else decodes its embedded model bytes and
  // loads in the background. No synchronous model prepare under the chain
  // lock.
  //
  // Hosts can re-set state mid-playback (DAW preset browsers), so mute-splice
  // the restore like any structural edit. The mute is held until the restored
  // chain's models settle (deferred release below); the first audible buffers
  // are the finished rig gliding in, never the raw dry input of still-loading
  // blocks.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock; see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);

    retired = restoreChainSnapshot(snapshot);  // updates latency, bumps revision

    pendingAddSide = ChainSide::Left;
    activePresetId = state.getProperty("activePresetId").toString();
    activePresetName = state.getProperty("activePresetName").toString();
    // A project/state load replaces the whole session; undoing across it
    // would resurrect chains the user never saw in this session.
    chainHistory.clear();
  }

  editFade.releaseWhenChainLoadsSettle();

  // Tell the host the whole parameter set may have moved. CLAP requires an
  // explicit CLAP_PARAM_RESCAN_VALUES after a state load (clap-juce-extensions
  // maps programChanged to exactly that); VST3/AU hosts drive their own state
  // restores and treat this as a harmless values refresh.
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));

  DBG("Plugin state restored successfully");
}
