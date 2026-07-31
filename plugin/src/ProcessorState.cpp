#include "Processor.h"

// #############################
// STATE PERSISTENCE
// #############################

// ── Machine-wide user settings ──
// Shared PropertiesFile for preferences that belong to the machine, not the
// session/preset (currently just the NAM A2 size). Same app-data root as
// PresetManager: ~/Library/Application Support/TONE3000 on macOS,
// %APPDATA%/TONE3000 on Windows.
namespace {

constexpr auto kNamFullSizeKey = "namFullSize";

juce::PropertiesFile::Options userSettingsOptions() {
  juce::PropertiesFile::Options options;
  options.applicationName = "TONE3000";
  options.filenameSuffix = ".settings";
  options.folderName = "TONE3000";
  options.osxLibrarySubFolder = "Application Support";
  return options;
}

}  // namespace

bool TONE3000Processor::readPersistedNamFullSize() {
  return juce::PropertiesFile(userSettingsOptions()).getBoolValue(kNamFullSizeKey, false);
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

        juce::String base64Data = juce::Base64::toBase64(modelData.data(), modelData.size());
        cachedModel.setProperty("data", base64Data, nullptr);

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

  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  if (xml != nullptr) {
    copyXmlToBinary(*xml, destData);
    DBG("Plugin state saved successfully");
  }
}

void TONE3000Processor::setStateInformation(const void* data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState == nullptr) {
    juce::Logger::writeToLog("[Restore] Failed to parse plugin state XML (" +
                             juce::String(sizeInBytes) + " bytes)");
    return;
  }

  juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
  if (!state.isValid()) {
    juce::Logger::writeToLog("[Restore] Invalid plugin state ValueTree");
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
  // the restore like any structural edit. Free on project load: no audio
  // callbacks are running yet, so the fade is skipped entirely.
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

  DBG("Plugin state restored successfully");
}
