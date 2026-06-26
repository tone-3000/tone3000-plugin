#include "Processor.h"
#include <algorithm>
#include <random>

// ####################
// CHAIN MANAGEMENT
// ####################

// The chain the UI edits/adds to right now: Left in mono mode, or the active side in stereo.
std::vector<std::unique_ptr<ChainBlock>>& TONE3000Processor::activeChain() {
  if (stereoEnabled.load() && activeEditSide == ChainSide::Right)
    return rightChainBlocks;
  return chainBlocks;
}

// Find a block by id across both chains (ids are globally unique).
ChainBlock* TONE3000Processor::findBlockById(const std::string& blockId) {
  for (auto& b : chainBlocks)
    if (b && b->id == blockId)
      return b.get();
  for (auto& b : rightChainBlocks)
    if (b && b->id == blockId)
      return b.get();
  return nullptr;
}

std::string TONE3000Processor::loadTone(const juce::String& toneJsonString) {
  juce::ScopedLock lock(chainMutex);

  juce::var toneVar = juce::JSON::parse(toneJsonString);
  if (!toneVar.isObject()) {
    DBG("Failed to parse tone JSON");
    return "";
  }

  juce::DynamicObject* toneObj = toneVar.getDynamicObject();
  if (!toneObj) {
    DBG("Tone JSON is not a valid object");
    return "";
  }

  int toneId = toneObj->getProperty("id");
  // The API renamed `platform` to `format`; fall back to `platform` for tone
  // JSON persisted by older builds.
  juce::String format = toneObj->getProperty("format").toString().toLowerCase();
  if (format.isEmpty())
    format = toneObj->getProperty("platform").toString().toLowerCase();
  juce::var modelsVar = toneObj->getProperty("models");

  if (!modelsVar.isArray() || modelsVar.getArray()->size() == 0) {
    DBG("Tone has no models");
    return "";
  }

  juce::var firstModelVar = modelsVar.getArray()->getReference(0);
  if (!firstModelVar.isObject()) {
    DBG("First model is not a valid object");
    return "";
  }

  juce::DynamicObject* firstModel = firstModelVar.getDynamicObject();
  int firstModelId = firstModel->getProperty("id");
  juce::String modelUrl = firstModel->getProperty("model_url").toString();
  juce::String modelName = firstModel->getProperty("name").toString();

  ChainBlockType type = (format == "nam") ? ChainBlockType::NAM : ChainBlockType::IR;

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(1000, 9999);
  std::string blockId = std::to_string(dis(gen));

  auto block = std::make_unique<ChainBlock>(blockId, type);
  block->toneId = toneId;
  block->toneJson = toneJsonString;
  block->activeModelId = firstModelId;
  block->loaded = false;

  DBG("Created tone block: " << toneId << " (block: " << blockId << ")");
  DBG("Queueing first model for background loading: " << modelName);

  // Add to whichever chain is currently being edited (Left in mono mode).
  auto& chain = activeChain();

  // Insert new block at the insert block position, then move insert block to end
  auto insertIt = std::find_if(
      chain.begin(), chain.end(),
      [](const std::unique_ptr<ChainBlock>& b) {
        return b && b->type == ChainBlockType::INSERT;
      });
  if (insertIt != chain.end()) {
    chain.insert(insertIt, std::move(block));
    // Move insert block to end so it's always last after adding a tone
    insertIt = std::find_if(
        chain.begin(), chain.end(),
        [](const std::unique_ptr<ChainBlock>& b) {
          return b && b->type == ChainBlockType::INSERT;
        });
    if (insertIt != chain.end()) {
      auto insertBlock = std::move(*insertIt);
      chain.erase(insertIt);
      chain.push_back(std::move(insertBlock));
    }
  } else {
    chain.push_back(std::move(block));
  }

  struct LoadToneJob : public juce::ThreadPoolJob {
    TONE3000Processor& processor;
    std::string blockId;
    juce::String toneJson;
    int firstModelId;
    juce::String modelUrl;
    juce::String modelName;
    ChainBlockType type;

    LoadToneJob(TONE3000Processor& p, const std::string& bid, const juce::String& tj,
                int fmid, const juce::String& url, const juce::String& name, ChainBlockType t)
        : ThreadPoolJob("Load Tone"), processor(p), blockId(bid), toneJson(tj),
          firstModelId(fmid), modelUrl(url), modelName(name), type(t) {}

    JobStatus runJob() override {
      processor.loadToneInBackground(blockId, toneJson, firstModelId, modelUrl, modelName, type);
      return jobHasFinished;
    }
  };

  loadingThreadPool.addJob(
      new LoadToneJob(*this, blockId, toneJsonString, firstModelId, modelUrl, modelName, type),
      true);

  return blockId;
}

bool TONE3000Processor::switchModel(const std::string& blockId, int modelId) {
  juce::ScopedLock lock(chainMutex);

  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr) {
    DBG("Block not found: " << blockId);
    return false;
  }

  juce::var toneVar = juce::JSON::parse(block->toneJson);
  if (!toneVar.isObject()) {
    DBG("Failed to parse stored tone JSON");
    return false;
  }

  juce::DynamicObject* toneObj = toneVar.getDynamicObject();
  juce::var modelsVar = toneObj->getProperty("models");

  if (!modelsVar.isArray()) {
    DBG("Models array not found in tone JSON");
    return false;
  }

  juce::DynamicObject* targetModel = nullptr;
  for (int i = 0; i < modelsVar.getArray()->size(); i++) {
    juce::var modelVar = modelsVar.getArray()->getReference(i);
    if (modelVar.isObject()) {
      juce::DynamicObject* modelObj = modelVar.getDynamicObject();
      if ((int)modelObj->getProperty("id") == modelId) {
        targetModel = modelObj;
        break;
      }
    }
  }

  if (!targetModel) {
    DBG("Model not found with ID: " << modelId);
    return false;
  }

  juce::String modelUrl = targetModel->getProperty("model_url").toString();
  juce::String modelName = targetModel->getProperty("name").toString();

  DBG("Queueing model switch: " << modelName << " (ID: " << modelId << ")");

  block->activeModelId = modelId;
  block->loaded = false;

  struct SwitchModelJob : public juce::ThreadPoolJob {
    TONE3000Processor& processor;
    std::string blockId;
    int modelId;
    juce::String modelUrl;
    juce::String modelName;

    SwitchModelJob(TONE3000Processor& p, const std::string& bid, int mid,
                  const juce::String& url, const juce::String& name)
        : ThreadPoolJob("Switch Model"), processor(p), blockId(bid), modelId(mid), modelUrl(url),
          modelName(name) {}

    JobStatus runJob() override {
      processor.switchModelInBackground(blockId, modelId, modelUrl, modelName);
      return jobHasFinished;
    }
  };

  loadingThreadPool.addJob(new SwitchModelJob(*this, blockId, modelId, modelUrl, modelName), true);

  return true;
}

bool TONE3000Processor::removeChainBlock(const std::string& blockId) {
  juce::ScopedLock lock(chainMutex);

  if (blockId == INSERT_BLOCK_ID || blockId == INSERT_BLOCK_ID_RIGHT) {
    DBG("Cannot remove insert block");
    return false;
  }

  for (auto* chain : {&chainBlocks, &rightChainBlocks}) {
    auto it = std::find_if(
        chain->begin(), chain->end(),
        [&blockId](const std::unique_ptr<ChainBlock>& block) { return block->id == blockId; });
    if (it != chain->end()) {
      if ((*it)->type == ChainBlockType::INSERT) {
        DBG("Cannot remove insert block");
        return false;
      }
      chain->erase(it);
      DBG("Removed chain block: " << blockId);
      return true;
    }
  }

  DBG("Failed to remove chain block: " << blockId << " (not found)");
  return false;
}

bool TONE3000Processor::reorderChainBlocks(const std::vector<std::string>& newOrder) {
  juce::ScopedLock lock(chainMutex);

  // Reorder the chain currently being edited (Left in mono mode, active side in stereo).
  auto& chain = activeChain();

  if (newOrder.size() != chain.size()) {
    DBG("Failed to reorder chain blocks: size mismatch (got "
        << newOrder.size() << ", expected " << chain.size() << ")");
    return false;
  }

  std::vector<std::unique_ptr<ChainBlock>> reorderedBlocks;
  reorderedBlocks.reserve(chain.size());
  std::vector<std::unique_ptr<ChainBlock>> originalBlocks = std::move(chain);

  for (const std::string& blockId : newOrder) {
    auto it = std::find_if(
        originalBlocks.begin(), originalBlocks.end(),
        [&blockId](const std::unique_ptr<ChainBlock>& block) {
          return block && block->id == blockId;
        });

    if (it == originalBlocks.end()) {
      DBG("Failed to reorder chain blocks: block not found: " << blockId);
      chain = std::move(originalBlocks);
      return false;
    }

    reorderedBlocks.push_back(std::move(*it));
  }

  chain = std::move(reorderedBlocks);
  DBG("Successfully reordered chain blocks (including insert block)");
  return true;
}

void TONE3000Processor::loadToneInBackground(const std::string& blockId, const juce::String& toneJson,
                                             int firstModelId, const juce::String& modelUrl,
                                             const juce::String& modelName, ChainBlockType type) {
  DBG("[Background] Loading tone for block: " << blockId);

  std::vector<uint8_t> modelData = fetchModelFromUrl(modelUrl);
  if (modelData.empty()) {
    DBG("[Background] Failed to fetch model from URL");
    return;
  }

  const juce::String filename =
      modelName + (type == ChainBlockType::NAM ? ".nam" : ".wav");

  double namPersistedSlimmable = 1.0;
  {
    juce::ScopedLock lock(chainMutex);
    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr) {
      DBG("[Background] Block not found: " << blockId);
      return;
    }

    namPersistedSlimmable = block->namSlimmableSize;
    block->modelCache[firstModelId] = modelData;
  }

  PreparedBlockModel prepared =
      prepareBlockModelOffThread(type, modelData, filename, namPersistedSlimmable);

  {
    juce::ScopedLock lock(chainMutex);

    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr) {
      DBG("[Background] Block not found after prepare: " << blockId);
      return;
    }

    applyPreparedModelToChainBlock(*block, prepared);

    if (prepared.success)
      DBG("[Background] Successfully loaded tone for block: " << blockId);
  }
}

void TONE3000Processor::switchModelInBackground(const std::string& blockId, int modelId,
                                                const juce::String& modelUrl,
                                                const juce::String& modelName) {
  DBG("[Background] Switching model for block: " << blockId << " to model ID: " << modelId);

  std::vector<uint8_t> modelData;
  bool needsFetch = false;
  ChainBlockType blockTypeForPrepare = ChainBlockType::NAM;
  double namPersistedSlimmable = 1.0;

  {
    juce::ScopedLock lock(chainMutex);

    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr) {
      DBG("[Background] Block not found: " << blockId);
      return;
    }

    blockTypeForPrepare = block->type;
    namPersistedSlimmable = block->namSlimmableSize;
    auto cacheIt = block->modelCache.find(modelId);

    if (cacheIt != block->modelCache.end()) {
      DBG("[Background] Using cached model data");
      modelData = cacheIt->second;
    } else {
      needsFetch = true;
    }
  }

  if (needsFetch) {
    DBG("[Background] Fetching model from URL: " << modelUrl);
    modelData = fetchModelFromUrl(modelUrl);

    if (modelData.empty()) {
      DBG("[Background] Failed to fetch model from URL");
      return;
    }
  }

  const juce::String filename =
      modelName + (blockTypeForPrepare == ChainBlockType::NAM ? ".nam" : ".wav");

  PreparedBlockModel prepared = prepareBlockModelOffThread(blockTypeForPrepare, modelData, filename,
                                                           namPersistedSlimmable);

  {
    juce::ScopedLock lock(chainMutex);

    ChainBlock* block = findBlockById(blockId);
    if (block == nullptr) {
      DBG("[Background] Block was removed during fetch/install");
      return;
    }

    if (needsFetch) {
      block->modelCache[modelId] = modelData;
    }
    applyPreparedModelToChainBlock(*block, prepared);
  }

  if (prepared.success)
    DBG("[Background] Successfully switched to model ID: " << modelId);
}

juce::var TONE3000Processor::getChainStatus() const {
  juce::ScopedLock lock(chainMutex);

  juce::DynamicObject::Ptr status = new juce::DynamicObject();
  juce::Array<juce::var> chainArray;

  // Report the chain currently being edited. Cast away const for the helper; we only read.
  const auto& chain =
      const_cast<TONE3000Processor*>(this)->activeChain();

  for (const auto& block : chain) {
    if (block->type == ChainBlockType::INSERT) {
      juce::DynamicObject::Ptr blockStatus = new juce::DynamicObject();
      blockStatus->setProperty("blockId", juce::String(block->id));
      blockStatus->setProperty("isInsertBlock", true);
      chainArray.add(juce::var(blockStatus.get()));
      continue;
    }

    juce::var toneVar = juce::JSON::parse(block->toneJson);

    if (toneVar.isObject()) {
      juce::DynamicObject::Ptr sourceTone = toneVar.getDynamicObject();
      juce::DynamicObject::Ptr blockStatus = new juce::DynamicObject();

      for (const auto& prop : sourceTone->getProperties()) {
        blockStatus->setProperty(prop.name, prop.value);
      }

      blockStatus->setProperty("blockId", juce::String(block->id));
      blockStatus->setProperty("loaded", block->loaded);
      blockStatus->setProperty("enabled", block->enabled);
      blockStatus->setProperty("outputGain", block->outputGainNormalized);
      blockStatus->setProperty("mix", block->mixNormalized);
      blockStatus->setProperty("activeModelId", block->activeModelId);

      if (block->type == ChainBlockType::NAM) {
        blockStatus->setProperty("namSlimmable", block->namIsSlimmable && block->loaded);
        blockStatus->setProperty("namSlimmableSize", block->namSlimmableSize);
      }

      chainArray.add(juce::var(blockStatus.get()));
    } else {
      juce::DynamicObject::Ptr blockStatus = new juce::DynamicObject();
      blockStatus->setProperty("blockId", juce::String(block->id));
      blockStatus->setProperty("toneId", block->toneId);
      blockStatus->setProperty("activeModelId", block->activeModelId);
      blockStatus->setProperty("loaded", block->loaded);
      blockStatus->setProperty("enabled", block->enabled);
      blockStatus->setProperty("outputGain", block->outputGainNormalized);
      blockStatus->setProperty("mix", block->mixNormalized);

      if (block->type == ChainBlockType::NAM) {
        blockStatus->setProperty("namSlimmable", block->namIsSlimmable && block->loaded);
        blockStatus->setProperty("namSlimmableSize", block->namSlimmableSize);
      }

      chainArray.add(juce::var(blockStatus.get()));
    }
  }

  status->setProperty("chain", chainArray);
  status->setProperty("stereoEnabled", stereoEnabled.load());
  status->setProperty("activeSide",
                      activeEditSide == ChainSide::Right ? "right" : "left");
  return status.get();
}

// ####################
// STEREO MODE
// ####################
void TONE3000Processor::setStereoMode(bool enabled) {
  juce::ScopedLock lock(chainMutex);

  // Seed the right chain's insert placeholder the first time stereo is enabled.
  if (enabled && rightChainBlocks.empty()) {
    rightChainBlocks.push_back(
        std::make_unique<ChainBlock>(INSERT_BLOCK_ID_RIGHT, ChainBlockType::INSERT));
  }

  stereoEnabled.store(enabled);

  if (!enabled)
    activeEditSide = ChainSide::Left;

  // Make sure the right chain's engines are ready to run at the current rate/size.
  const double sr = getSampleRate();
  if (enabled && sr > 0.0 && maxBlockSize > 0)
    prepareChain(rightChainBlocks, sr, maxBlockSize);

  updateLatencyCompensation();
  DBG("Stereo mode " << (enabled ? "enabled" : "disabled"));
}

void TONE3000Processor::setActiveEditChain(const juce::String& side) {
  juce::ScopedLock lock(chainMutex);
  if (side == "right")
    activeEditSide = ChainSide::Right;
  else if (side == "left")
    activeEditSide = ChainSide::Left;
}

bool TONE3000Processor::isChainValid() const {
  juce::ScopedLock lock(chainMutex);

  for (const auto& block : chainBlocks) {
    if (block->loaded && block->enabled) {
      return true;
    }
  }
  return false;
}

void TONE3000Processor::setBlockOutputGain(const std::string& blockId, float normalizedGain) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr) return;
  block->outputGainNormalized = juce::jlimit(0.0f, 1.0f, normalizedGain);
}

void TONE3000Processor::setBlockMix(const std::string& blockId, float normalizedMix) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr) return;
  block->mixNormalized = juce::jlimit(0.0f, 1.0f, normalizedMix);
}

void TONE3000Processor::setBlockNamSlimmableSize(const std::string& blockId, double size) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr) return;
  if (block->type != ChainBlockType::NAM || !block->namIsSlimmable || block->namResampler == nullptr)
    return;

  const double clamped = juce::jlimit(0.5, 1.0, size);
  block->namSlimmableSize = clamped;
  block->namResampler->setSlimmableSize(clamped);
}

int TONE3000Processor::calculateTotalLatency() const {
  juce::ScopedLock lock(chainMutex);

  auto chainLatency = [](const std::vector<std::unique_ptr<ChainBlock>>& blocks) {
    int latency = 0;
    for (const auto& block : blocks) {
      if (block->enabled && block->type == ChainBlockType::NAM && block->namResampler) {
        latency += block->latencySamples;
      }
    }
    return latency;
  };

  // In stereo mode the two chains run in parallel, so the plugin latency is the larger of them.
  int totalLatency = chainLatency(chainBlocks);
  if (stereoEnabled.load())
    totalLatency = juce::jmax(totalLatency, chainLatency(rightChainBlocks));

  if (!bypassResampling) {
    totalLatency += static_cast<int>(oversampler->getLatencyInSamples());
  }

  return totalLatency;
}

void TONE3000Processor::updateLatencyCompensation() {
  int newTotalLatency = calculateTotalLatency();
  setLatencySamples(newTotalLatency);
  DBG("Total plugin latency updated to: " << newTotalLatency << " samples");
}

void TONE3000Processor::setAccessToken(const juce::String& token) {
  juce::ScopedLock lock(accessTokenMutex);
  accessToken = token;
  DBG("TONE3000 access token updated (" << token.length() << " chars)");
}

juce::String TONE3000Processor::getAccessToken() const {
  juce::ScopedLock lock(accessTokenMutex);
  return accessToken;
}
