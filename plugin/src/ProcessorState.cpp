#include "Processor.h"

#include <cstring>

// #############################
// STATE PERSISTENCE
// #############################

// ── Machine-wide user settings ──
// Shared PropertiesFile for preferences that belong to the machine, not the
// session/preset (the NAM A2 size and multi-core stereo). Same app-data root
// as PresetManager: ~/Library/Application Support/TONE3000 on macOS,
// %APPDATA%/TONE3000 on Windows.
namespace {

constexpr auto kNamFullSizeKey = "namFullSize";
constexpr auto kMultiCoreKey = "multiCoreStereo";

// Magic prefix for the binary ValueTree state format (see getStateInformation).
constexpr char kStateMagic[] = {'T', '3', 'K', 'B'};

juce::PropertiesFile::Options userSettingsOptions() {
  juce::PropertiesFile::Options options;
  options.applicationName = "TONE3000";
  options.filenameSuffix = ".settings";
  options.folderName = "TONE3000";
  options.osxLibrarySubFolder = "Application Support";
  return options;
}

}  // namespace

juce::File TONE3000Processor::getSettingsFile() {
  return userSettingsOptions().getDefaultFile();
}

bool TONE3000Processor::readPersistedNamFullSize() {
  return juce::PropertiesFile(userSettingsOptions()).getBoolValue(kNamFullSizeKey, false);
}

bool TONE3000Processor::readPersistedMultiCoreEnabled() {
  return juce::PropertiesFile(userSettingsOptions()).getBoolValue(kMultiCoreKey, true);
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

  juce::Logger::writeToLog(juce::String("[Processor] Multi-core stereo ") +
                           (enabled ? "enabled" : "disabled"));
  bumpChainRevision();
}
void TONE3000Processor::setNamFullSize(bool full) {
  if (namFullSize.load() == full)
    return;

  namFullSize.store(full);
  {
    juce::PropertiesFile settings(userSettingsOptions());
    settings.setValue(kNamFullSizeKey, full);
    settings.saveIfNeeded();
  }

  // Retier every loaded NAM engine in place. Swapping weights inside playing
  // engines is discontinuous, and the change spans blocks in both lanes — so
  // mute-splice the whole chain like a structural edit. setSlimmableSize is
  // a no-op for non-slimmable models, and in-flight loads re-apply the
  // preference when they land (see applyPreparedModelToChainBlock).
  ChainEditFade editFade(*this);
  juce::ScopedLock lock(chainMutex);
  const double size = namSlimmableSizeValue();
  for (auto& l : lanes)
    for (auto& block : l)
      if (block->type == ChainBlockType::NAM && block->namEngine != nullptr)
        block->namEngine->setSlimmableSize(size);

  juce::Logger::writeToLog(juce::String("[Processor] NAM A2 size set to ") +
                           (full ? "full" : "lite"));
  bumpChainRevision();
}

juce::ValueTree TONE3000Processor::serializeBlockSettings(const ChainBlock& block) {
  juce::ValueTree blockState("ChainBlock");

  blockState.setProperty("id", juce::String(block.id), nullptr);
  blockState.setProperty("type", chainBlockTypeToString(block.type), nullptr);
  blockState.setProperty("enabled", block.enabled, nullptr);
  blockState.setProperty("normalize", block.normalizeEnabled, nullptr);
  blockState.setProperty("inputGain", block.inputGainNormalized, nullptr);
  blockState.setProperty("outputGain", block.outputGainNormalized, nullptr);
  blockState.setProperty("mix", block.mixNormalized, nullptr);

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
  // inputGain arrived after the first release; older projects default to unity.
  block.inputGainNormalized = blockState.hasProperty("inputGain")
                                  ? static_cast<float>(blockState.getProperty("inputGain"))
                                  : 0.5f;
  block.outputGainNormalized = static_cast<float>(blockState.getProperty("outputGain", 0.5f));
  block.mixNormalized = static_cast<float>(blockState.getProperty("mix", 1.0f));

  if (block.type != ChainBlockType::INSERT) {
    // EQ bands (defaults to flat when the child is missing — older projects).
    // Block EQs always run in the chain domain (fixed rate).
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

        // Raw bytes in a binary var: the ValueTree binary stream (state and
        // preset files) writes these verbatim — a plain memcpy instead of the
        // Base64 encode the old XML format forced, which mattered because
        // this can run with chainMutex held (~8 MB per heavy rig). The read
        // side still accepts legacy Base64 strings.
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

  juce::ValueTree parameterState = parameters.copyState();
  state.appendChild(parameterState, nullptr);

  state.setProperty("stereoEnabled", stereoEnabled.load(), nullptr);
  // Input channel mode: session/plugin state only — presets deliberately
  // don't carry it (I/O routing, not tone).
  state.setProperty("inputMode", inputModeToString(getInputMode()), nullptr);

  // Editor window scale: session/plugin state like inputMode (a workstation
  // preference, not tone) — presets never carry it.
  state.setProperty("editorScale", editorScale.load(), nullptr);

  // MIDI map: session/plugin state like inputMode (it describes the user's
  // rig, not the tone) — presets never carry it.
  state.appendChild(midiMapper.toValueTree(), nullptr);

  {
    juce::ScopedLock lock(chainMutex);

    state.setProperty("activePresetId", activePresetId, nullptr);
    state.setProperty("activePresetName", activePresetName, nullptr);

    // Branch routing, same properties a chain snapshot carries (the state
    // root restores through restoreChainSnapshot).
    state.setProperty("branchSide",
                      branchSourceSide == ChainSide::Right ? "right" : "left", nullptr);
    state.setProperty("branchAfterBlockId", juce::String(branchAfterBlockId), nullptr);

    juce::ValueTree chainState("ChainBlocks");
    serializeChainToTree(lane(ChainSide::Left), chainState, true);
    state.appendChild(chainState, nullptr);

    juce::ValueTree rightChainState("RightChainBlocks");
    serializeChainToTree(lane(ChainSide::Right), rightChainState, true);
    state.appendChild(rightChainState, nullptr);
  }

  // Magic-prefixed binary ValueTree stream. The old path (Base64 model bytes
  // inside XML) turned the ~8 MB embed into an 11 MB text blob and burned
  // 100+ ms encoding on every host save; the binary stream writes the model
  // bytes verbatim. setStateInformation still reads the legacy XML format.
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
  } else {
    // Legacy sessions: Base64-in-XML wrapped by copyXmlToBinary.
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
      state = juce::ValueTree::fromXml(*xmlState);
  }

  if (!state.isValid()) {
    juce::Logger::writeToLog("[Restore] Failed to parse plugin state (" +
                             juce::String(sizeInBytes) + " bytes)");
    return;
  }

  juce::Logger::writeToLog(
      "[Restore] Restoring state (" + juce::String(sizeInBytes) + " bytes, " +
      juce::String(state.getChildWithName("ChainBlocks").getNumChildren()) + " left / " +
      juce::String(state.getChildWithName("RightChainBlocks").getNumChildren()) +
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

  // A missing child clears the map — a project without mappings must not
  // inherit the previous session's.
  midiMapper.restoreFromValueTree(state.getChildWithName("MidiMappings"));

  // The state root carries the same shape a chain snapshot does (ChainBlocks /
  // RightChainBlocks children + stereoEnabled property), so a project load is
  // just a reconciling restore: matching blocks keep their loaded engines,
  // everything else decodes its embedded model bytes and loads in the
  // background — no synchronous model prepare under the chain lock.
  //
  // Hosts can re-set state mid-playback (DAW preset browsers) — mute-splice
  // the restore like any structural edit. On a fresh launch no callbacks run
  // yet, but the audio device typically starts *during* the load window that
  // follows, so the mute is held until the restored chain's models settle
  // (deferred release below) — the first audible buffers are the finished
  // rig gliding in, never the raw dry input of still-loading blocks.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);

    retired = restoreChainSnapshot(state);  // updates latency, bumps revision

    pendingAddSide = ChainSide::Left;
    activePresetId = state.getProperty("activePresetId").toString();
    activePresetName = state.getProperty("activePresetName").toString();
    // A project/state load replaces the whole session; undoing across it
    // would resurrect chains the user never saw in this session.
    chainHistory.clear();
  }

  editFade.releaseWhenChainLoadsSettle();

  DBG("Plugin state restored successfully");
}
