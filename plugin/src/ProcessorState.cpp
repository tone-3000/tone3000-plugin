#include "Processor.h"

// #############################
// STATE PERSISTENCE
// #############################

juce::ValueTree TONE3000Processor::serializeBlockSettings(const ChainBlock& block) {
  juce::ValueTree blockState("ChainBlock");

  blockState.setProperty("id", juce::String(block.id), nullptr);
  blockState.setProperty("type", chainBlockTypeToString(block.type), nullptr);
  blockState.setProperty("enabled", block.enabled, nullptr);
  blockState.setProperty("normalize", block.normalizeEnabled, nullptr);
  blockState.setProperty("inputGain", block.inputGainNormalized, nullptr);
  blockState.setProperty("outputGain", block.outputGainNormalized, nullptr);
  blockState.setProperty("mix", block.mixNormalized, nullptr);

  if (block.type == ChainBlockType::NAM)
    blockState.setProperty("namSlimmableSize", block.namSlimmableSize, nullptr);

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

  if (block.type == ChainBlockType::NAM && blockState.hasProperty("namSlimmableSize")) {
    block.namSlimmableSize =
        juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("namSlimmableSize")));
    if (block.namIsSlimmable && block.namEngine != nullptr)
      block.namEngine->setSlimmableSize(block.namSlimmableSize);
  }

  if (block.type != ChainBlockType::INSERT) {
    // EQ bands (defaults to flat when the child is missing — older projects).
    // Block EQs always run in the chain domain (fixed rate).
    block.eq.restoreFromValueTree(blockState.getChildWithName("Eq"));
    block.eq.prepare(kChainSampleRate);
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
  // Standalone input channel mode (harmless no-op in hosts, but the
  // standalone app persists its state through here across launches).
  state.setProperty("standaloneInputMode", standaloneInputMode.load(), nullptr);

  {
    juce::ScopedLock lock(chainMutex);

    state.setProperty("activePresetId", activePresetId, nullptr);
    state.setProperty("activePresetName", activePresetName, nullptr);

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

  if (state.hasProperty("standaloneInputMode")) {
    standaloneInputMode.store(
        juce::jlimit(0, 2, static_cast<int>(state.getProperty("standaloneInputMode"))));
    updateStereoInputDetection();
  }

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
