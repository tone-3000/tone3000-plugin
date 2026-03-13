#include "Processor.h"
#include <cstring>

// #############################
// STATE PERSISTENCE
// #############################

void TONE3000Processor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree state("TONE3000State");

  juce::ValueTree parameterState = parameters.copyState();
  state.appendChild(parameterState, nullptr);

  juce::ValueTree chainState("ChainBlocks");
  {
    juce::ScopedLock lock(chainMutex);
    for (const auto& block : chainBlocks) {
      juce::ValueTree blockState("ChainBlock");

      juce::String typeStr = "ir";
      if (block->type == ChainBlockType::NAM)
        typeStr = "nam";
      else if (block->type == ChainBlockType::INSERT)
        typeStr = "insert";

      blockState.setProperty("id", juce::String(block->id), nullptr);
      blockState.setProperty("type", typeStr, nullptr);
      blockState.setProperty("enabled", block->enabled, nullptr);
      blockState.setProperty("outputGain", block->outputGainNormalized, nullptr);
      blockState.setProperty("mix", block->mixNormalized, nullptr);

      if (block->type != ChainBlockType::INSERT) {
        blockState.setProperty("toneId", block->toneId, nullptr);
        blockState.setProperty("toneJson", block->toneJson, nullptr);
        blockState.setProperty("activeModelId", block->activeModelId, nullptr);

        juce::ValueTree cacheState("ModelCache");
        for (const auto& [modelId, modelData] : block->modelCache) {
          juce::ValueTree cachedModel("CachedModel");
          cachedModel.setProperty("modelId", modelId, nullptr);

          juce::String base64Data =
              juce::Base64::toBase64(modelData.data(), modelData.size());
          cachedModel.setProperty("data", base64Data, nullptr);

          cacheState.appendChild(cachedModel, nullptr);
        }
        blockState.appendChild(cacheState, nullptr);

        if (block->type == ChainBlockType::IR && block->irTempFile.existsAsFile()) {
          blockState.setProperty("irTempFilePath",
                                block->irTempFile.getFullPathName(), nullptr);
        }
      }

      chainState.appendChild(blockState, nullptr);
    }
  }
  state.appendChild(chainState, nullptr);

  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  if (xml != nullptr) {
    copyXmlToBinary(*xml, destData);
    DBG("Plugin state saved successfully");
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

  juce::ValueTree chainState = state.getChildWithName("ChainBlocks");
  if (chainState.isValid()) {
    juce::ScopedLock lock(chainMutex);
    chainBlocks.clear();

    bool hasInsertBlock = false;

    for (int i = 0; i < chainState.getNumChildren(); ++i) {
      juce::ValueTree blockState = chainState.getChild(i);
      if (blockState.hasType("ChainBlock")) {
        std::string blockId = blockState.getProperty("id").toString().toStdString();
        std::string typeStr = blockState.getProperty("type").toString().toStdString();
        bool enabled = blockState.getProperty("enabled");
        float outputGain = blockState.getProperty("outputGain");
        float mix = blockState.getProperty("mix");

        ChainBlockType type = ChainBlockType::IR;
        if (typeStr == "nam")
          type = ChainBlockType::NAM;
        else if (typeStr == "insert")
          type = ChainBlockType::INSERT;

        if (type == ChainBlockType::INSERT) {
          hasInsertBlock = true;
        }

        auto block = std::make_unique<ChainBlock>(blockId, type);
        block->enabled = enabled;
        block->outputGainNormalized = outputGain;
        block->mixNormalized = mix;

        if (type == ChainBlockType::INSERT) {
          chainBlocks.push_back(std::move(block));
          continue;
        }

        int toneId = blockState.getProperty("toneId");
        juce::String toneJson = blockState.getProperty("toneJson");
        int activeModelId = blockState.getProperty("activeModelId");
        block->toneId = toneId;
        block->toneJson = toneJson;
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
                std::memcpy(modelData.data(), outputStream.getData(),
                            outputStream.getDataSize());

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

        chainBlocks.push_back(std::move(block));
      }
    }

    // Migration: old project files have no insert block; add it at the end
    if (!hasInsertBlock) {
      chainBlocks.push_back(
          std::make_unique<ChainBlock>(INSERT_BLOCK_ID, ChainBlockType::INSERT));
      DBG("Added insert block for migration from older project format");
    }

    DBG("Chain blocks restored from state: " << chainBlocks.size() << " blocks");
  }

  DBG("Plugin state restored successfully");
}
