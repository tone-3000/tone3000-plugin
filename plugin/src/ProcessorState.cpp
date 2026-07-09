#include "Processor.h"
#include <cstring>

// #############################
// STATE PERSISTENCE
// #############################

juce::ValueTree TONE3000Processor::serializeBlockSettings(const ChainBlock& block) {
  juce::ValueTree blockState("ChainBlock");

  blockState.setProperty("id", juce::String(block.id), nullptr);
  blockState.setProperty("type", chainBlockTypeToString(block.type), nullptr);
  blockState.setProperty("enabled", block.enabled, nullptr);
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
  // inputGain arrived after the first release; older projects default to unity.
  block.inputGainNormalized = blockState.hasProperty("inputGain")
                                  ? static_cast<float>(blockState.getProperty("inputGain"))
                                  : 0.5f;
  block.outputGainNormalized = static_cast<float>(blockState.getProperty("outputGain", 0.5f));
  block.mixNormalized = static_cast<float>(blockState.getProperty("mix", 1.0f));

  if (block.type == ChainBlockType::NAM && blockState.hasProperty("namSlimmableSize")) {
    block.namSlimmableSize =
        juce::jlimit(0.5, 1.0, static_cast<double>(blockState.getProperty("namSlimmableSize")));
    if (block.namIsSlimmable && block.namResampler != nullptr)
      block.namResampler->setSlimmableSize(block.namSlimmableSize);
  }

  if (block.type != ChainBlockType::INSERT) {
    // EQ bands (defaults to flat when the child is missing — older projects).
    block.eq.restoreFromValueTree(blockState.getChildWithName("Eq"));
    if (const double sr = getSampleRate(); sr > 0.0)
      block.eq.prepare(sr);
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

      if (block->type == ChainBlockType::IR && block->irTempFile.existsAsFile()) {
        blockState.setProperty("irTempFilePath", block->irTempFile.getFullPathName(), nullptr);
      }
    }

    chainState.appendChild(blockState, nullptr);
  }
}

void TONE3000Processor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree state("TONE3000State");

  juce::ValueTree parameterState = parameters.copyState();
  state.appendChild(parameterState, nullptr);

  state.setProperty("stereoEnabled", stereoEnabled.load(), nullptr);

  {
    juce::ScopedLock lock(chainMutex);

    juce::ValueTree chainState("ChainBlocks");
    serializeChainToTree(chainBlocks, chainState, true);
    state.appendChild(chainState, nullptr);

    juce::ValueTree rightChainState("RightChainBlocks");
    serializeChainToTree(rightChainBlocks, rightChainState, true);
    state.appendChild(rightChainState, nullptr);
  }

  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  if (xml != nullptr) {
    copyXmlToBinary(*xml, destData);
    DBG("Plugin state saved successfully");
  }
}

void TONE3000Processor::restoreChainFromTree(
    const juce::ValueTree& chainState, std::vector<std::unique_ptr<ChainBlock>>& target,
    const char* insertBlockId) {
  target.clear();

  bool hasInsertBlock = false;

  for (int i = 0; i < chainState.getNumChildren(); ++i) {
    juce::ValueTree blockState = chainState.getChild(i);
    if (!blockState.hasType("ChainBlock"))
      continue;

    std::string blockId = blockState.getProperty("id").toString().toStdString();
    ChainBlockType type = chainBlockTypeFromString(blockState.getProperty("type").toString());

    auto block = std::make_unique<ChainBlock>(blockId, type);
    applyBlockSettings(*block, blockState);

    if (type == ChainBlockType::INSERT) {
      hasInsertBlock = true;
      target.push_back(std::move(block));
      continue;
    }

    int toneId = blockState.getProperty("toneId");
    int activeModelId = blockState.getProperty("activeModelId");
    block->toneId = toneId;
    block->toneJson = blockState.getProperty("toneJson").toString();
    block->activeModelId = activeModelId;

    juce::ValueTree cacheState = blockState.getChildWithName("ModelCache");
    if (cacheState.isValid()) {
      for (int j = 0; j < cacheState.getNumChildren(); ++j) {
        juce::ValueTree cachedModel = cacheState.getChild(j);
        if (cachedModel.hasType("CachedModel")) {
          int modelId = cachedModel.getProperty("modelId");
          juce::String base64Data = cachedModel.getProperty("data");

          juce::MemoryOutputStream outputStream;
          if (juce::Base64::convertFromBase64(outputStream, base64Data)) {
            std::vector<uint8_t> modelData(outputStream.getDataSize());
            std::memcpy(modelData.data(), outputStream.getData(), outputStream.getDataSize());

            block->modelCache[modelId] = modelData;
          }
        }
      }
    }

    auto cacheIt = block->modelCache.find(activeModelId);
    if (cacheIt != block->modelCache.end()) {
      juce::String filename = "model_" + juce::String(activeModelId) +
                              (type == ChainBlockType::NAM ? ".nam" : ".wav");
      loadModelData(*block, cacheIt->second, filename);

      DBG("Restored block: " << blockId << " with tone ID: " << toneId
                             << ", active model ID: " << activeModelId);
    } else {
      DBG("Warning: Active model not found in cache for block: " << blockId);
      block->loaded = false;
    }

    if (type == ChainBlockType::IR && blockState.hasProperty("irTempFilePath")) {
      juce::String irTempFilePath = blockState.getProperty("irTempFilePath");
      block->irTempFile = juce::File(irTempFilePath);
    }

    target.push_back(std::move(block));
  }

  // Migration: old project files have no insert block; add it at the end.
  if (!hasInsertBlock) {
    target.push_back(std::make_unique<ChainBlock>(insertBlockId, ChainBlockType::INSERT));
    DBG("Added insert block for migration from older project format");
  }
}

void TONE3000Processor::setStateInformation(const void* data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState == nullptr) {
    DBG("Failed to parse plugin state XML");
    return;
  }

  juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
  if (!state.isValid()) {
    DBG("Invalid plugin state ValueTree");
    return;
  }

  juce::ValueTree parameterState = state.getChildWithName("PARAMETERS");
  if (parameterState.isValid()) {
    parameters.replaceState(parameterState);
    DBG("Parameters restored from state");
  }

  const bool restoredStereo =
      state.hasProperty("stereoEnabled") ? static_cast<bool>(state.getProperty("stereoEnabled"))
                                         : false;

  juce::ValueTree chainState = state.getChildWithName("ChainBlocks");
  juce::ValueTree rightChainState = state.getChildWithName("RightChainBlocks");

  {
    juce::ScopedLock lock(chainMutex);

    if (chainState.isValid()) {
      restoreChainFromTree(chainState, chainBlocks, INSERT_BLOCK_ID);
      DBG("Left chain blocks restored: " << chainBlocks.size() << " blocks");
    }

    if (rightChainState.isValid()) {
      restoreChainFromTree(rightChainState, rightChainBlocks, INSERT_BLOCK_ID_RIGHT);
      DBG("Right chain blocks restored: " << rightChainBlocks.size() << " blocks");
    }

    // Ensure the right chain always has its placeholder once stereo is in use.
    if (restoredStereo && rightChainBlocks.empty()) {
      rightChainBlocks.push_back(
          std::make_unique<ChainBlock>(INSERT_BLOCK_ID_RIGHT, ChainBlockType::INSERT));
    }

    stereoEnabled.store(restoredStereo);
    activeEditSide = ChainSide::Left;
    // A project/state load replaces the whole session; undoing across it
    // would resurrect chains the user never saw in this session.
    chainHistory.clear();
    bumpChainRevision();
  }

  updateLatencyCompensation();
  DBG("Plugin state restored successfully");
}
