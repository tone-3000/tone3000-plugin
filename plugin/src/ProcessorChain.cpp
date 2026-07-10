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

namespace {

// Everything loadTone/swapTone need from a raw tone JSON string.
struct ParsedTone {
  bool valid = false;
  int toneId = 0;
  int firstModelId = 0;
  juce::String modelUrl;
  juce::String modelName;
  ChainBlockType type = ChainBlockType::NAM;
};

ParsedTone parseToneForLoading(const juce::String& toneJsonString) {
  ParsedTone out;

  juce::var toneVar = juce::JSON::parse(toneJsonString);
  juce::DynamicObject* toneObj = toneVar.getDynamicObject();
  if (toneObj == nullptr) {
    DBG("Tone JSON is not a valid object");
    return out;
  }

  out.toneId = toneObj->getProperty("id");
  // The API renamed `platform` to `format`; fall back to `platform` for tone
  // JSON persisted by older builds.
  juce::String format = toneObj->getProperty("format").toString().toLowerCase();
  if (format.isEmpty())
    format = toneObj->getProperty("platform").toString().toLowerCase();
  juce::var modelsVar = toneObj->getProperty("models");

  if (!modelsVar.isArray() || modelsVar.getArray()->size() == 0) {
    DBG("Tone has no models");
    return out;
  }

  juce::DynamicObject* firstModel = modelsVar.getArray()->getReference(0).getDynamicObject();
  if (firstModel == nullptr) {
    DBG("First model is not a valid object");
    return out;
  }

  out.firstModelId = firstModel->getProperty("id");
  out.modelUrl = firstModel->getProperty("model_url").toString();
  out.modelName = firstModel->getProperty("name").toString();
  out.type = (format == "nam") ? ChainBlockType::NAM : ChainBlockType::IR;
  out.valid = true;
  return out;
}

}  // namespace

void TONE3000Processor::queueToneLoad(const std::string& blockId, const juce::String& toneJson,
                                      int modelId, const juce::String& modelUrl,
                                      const juce::String& modelName, ChainBlockType type) {
  struct LoadToneJob : public juce::ThreadPoolJob {
    TONE3000Processor& processor;
    std::string blockId;
    juce::String toneJson;
    int modelId;
    juce::String modelUrl;
    juce::String modelName;
    ChainBlockType type;

    LoadToneJob(TONE3000Processor& p, const std::string& bid, const juce::String& tj, int mid,
                const juce::String& url, const juce::String& name, ChainBlockType t)
        : ThreadPoolJob("Load Tone"), processor(p), blockId(bid), toneJson(tj), modelId(mid),
          modelUrl(url), modelName(name), type(t) {}

    JobStatus runJob() override {
      processor.loadToneInBackground(blockId, toneJson, modelId, modelUrl, modelName, type);
      return jobHasFinished;
    }
  };

  loadingThreadPool.addJob(
      new LoadToneJob(*this, blockId, toneJson, modelId, modelUrl, modelName, type), true);
}

std::string TONE3000Processor::loadTone(const juce::String& toneJsonString) {
  juce::ScopedLock lock(chainMutex);

  const ParsedTone parsed = parseToneForLoading(toneJsonString);
  if (!parsed.valid)
    return "";

  pushChainHistory();

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(1000, 9999);
  std::string blockId = std::to_string(dis(gen));

  auto block = std::make_unique<ChainBlock>(blockId, parsed.type);
  block->toneId = parsed.toneId;
  block->toneJson = toneJsonString;
  block->activeModelId = parsed.firstModelId;
  block->loaded = false;

  DBG("Created tone block: " << parsed.toneId << " (block: " << blockId << ")");
  DBG("Queueing first model for background loading: " << parsed.modelName);

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

  bumpChainRevision();
  queueToneLoad(blockId, toneJsonString, parsed.firstModelId, parsed.modelUrl, parsed.modelName,
                parsed.type);

  return blockId;
}

bool TONE3000Processor::swapTone(const std::string& blockId, const juce::String& toneJsonString) {
  juce::ScopedLock lock(chainMutex);

  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT) {
    DBG("swapTone: block not found or is insert block: " << blockId);
    return false;
  }

  const ParsedTone parsed = parseToneForLoading(toneJsonString);
  if (!parsed.valid)
    return false;

  pushChainHistory();

  // Replace the tone in place: same block id (chain position preserved), same
  // user params (enabled/gains/mix). Engines stay until the new model swaps in
  // via applyPreparedModelToChainBlock, but loaded=false stops processing now.
  block->type = parsed.type;
  block->toneId = parsed.toneId;
  block->toneJson = toneJsonString;
  block->activeModelId = parsed.firstModelId;
  block->loaded = false;
  block->modelCache.clear();
  block->namIsSlimmable = false;
  block->namSlimmableSize = 1.0;

  DBG("Swapped tone on block " << blockId << " -> tone " << parsed.toneId);

  bumpChainRevision();
  queueToneLoad(blockId, toneJsonString, parsed.firstModelId, parsed.modelUrl, parsed.modelName,
                parsed.type);

  return true;
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

  pushChainHistory();

  block->activeModelId = modelId;
  block->loaded = false;
  bumpChainRevision();

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
      pushChainHistory();
      chain->erase(it);
      bumpChainRevision();
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

  // Validate that newOrder is a permutation of the chain up front, so history
  // is only recorded for reorders that actually happen.
  {
    std::vector<std::string> chainIds, orderIds = newOrder;
    for (const auto& block : chain)
      chainIds.push_back(block->id);
    std::sort(chainIds.begin(), chainIds.end());
    std::sort(orderIds.begin(), orderIds.end());
    if (chainIds != orderIds) {
      DBG("Failed to reorder chain blocks: order is not a permutation of the chain");
      return false;
    }
  }

  pushChainHistory();

  std::vector<std::unique_ptr<ChainBlock>> reorderedBlocks;
  reorderedBlocks.reserve(chain.size());
  std::vector<std::unique_ptr<ChainBlock>> originalBlocks = std::move(chain);

  for (const std::string& blockId : newOrder) {
    auto it = std::find_if(
        originalBlocks.begin(), originalBlocks.end(),
        [&blockId](const std::unique_ptr<ChainBlock>& block) {
          return block && block->id == blockId;
        });
    reorderedBlocks.push_back(std::move(*it));
  }

  chain = std::move(reorderedBlocks);
  bumpChainRevision();
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

juce::var TONE3000Processor::getChainState(int knownRevision) const {
  juce::ScopedLock lock(chainMutex);

  // Read under the lock so the revision always matches the snapshot we build:
  // mutators bump the revision while holding chainMutex too.
  const juce::uint32 revision = chainRevision.load();

  // Cheap early-out for the UI poll loop: nothing changed since the caller
  // last synced, so skip building (and shipping) the full state.
  if (knownRevision >= 0 && static_cast<juce::uint32>(knownRevision) == revision) {
    juce::DynamicObject::Ptr unchanged = new juce::DynamicObject();
    unchanged->setProperty("revision", static_cast<int>(revision));
    unchanged->setProperty("unchanged", true);
    return unchanged.get();
  }

  juce::DynamicObject::Ptr state = new juce::DynamicObject();
  juce::Array<juce::var> chainArray;

  // Report the chain currently being edited. Cast away const for the helper; we only read.
  const auto& chain = const_cast<TONE3000Processor*>(this)->activeChain();

  for (const auto& block : chain) {
    juce::DynamicObject::Ptr item = new juce::DynamicObject();
    item->setProperty("blockId", juce::String(block->id));

    if (block->type == ChainBlockType::INSERT) {
      item->setProperty("kind", "insert");
      chainArray.add(juce::var(item.get()));
      continue;
    }

    item->setProperty("kind", "tone");

    // Full tone metadata, nested (not spread) so runtime fields never collide
    // with API fields and the UI has one obvious place to read tone info from.
    juce::var toneVar = juce::JSON::parse(block->toneJson);
    if (!toneVar.isObject()) {
      // Corrupt/legacy toneJson: emit a minimal stand-in so the UI's `tone`
      // field is always an object.
      juce::DynamicObject::Ptr fallback = new juce::DynamicObject();
      fallback->setProperty("id", block->toneId);
      fallback->setProperty("title", "Tone " + juce::String(block->toneId));
      fallback->setProperty("models", juce::Array<juce::var>());
      toneVar = juce::var(fallback.get());
    }
    item->setProperty("tone", toneVar);

    item->setProperty("activeModelId", block->activeModelId);
    item->setProperty("loaded", block->loaded);
    item->setProperty("namSlimmable",
                      block->type == ChainBlockType::NAM && block->namIsSlimmable && block->loaded);

    // User-editable params, grouped so a future "shareable chain preset" can
    // serialize { tone ref, activeModelId, params } per block verbatim.
    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    params->setProperty("enabled", block->enabled);
    params->setProperty("inputGain", block->inputGainNormalized);
    params->setProperty("outputGain", block->outputGainNormalized);
    params->setProperty("mix", block->mixNormalized);
    params->setProperty("namSlimmableSize", block->namSlimmableSize);
    params->setProperty("eq", block->eq.toVar());
    item->setProperty("params", juce::var(params.get()));

    chainArray.add(juce::var(item.get()));
  }

  state->setProperty("revision", static_cast<int>(revision));
  state->setProperty("chain", chainArray);
  // History flags ride along with the chain state: they only ever change
  // together with a revision bump (mutation, undo/redo or a state load).
  state->setProperty("canUndo", chainHistory.canUndo());
  state->setProperty("canRedo", chainHistory.canRedo());
  // Active preset for the top-bar pill (only changes with a revision bump).
  if (activePresetId.isNotEmpty()) {
    juce::DynamicObject::Ptr preset = new juce::DynamicObject();
    preset->setProperty("id", activePresetId);
    preset->setProperty("name", activePresetName);
    state->setProperty("preset", juce::var(preset.get()));
  }
  state->setProperty("stereoEnabled", stereoEnabled.load());
  state->setProperty("activeSide", activeEditSide == ChainSide::Right ? "right" : "left");
  // True when a real stereo source feeds the plugin (stereo host bus or a
  // stereo standalone input device) — drives the dual input meter/gain UI.
  state->setProperty("stereoInput", stereoInputDetected.load());
  // Standalone-only input channel picker (Settings). Hosts route explicitly,
  // so the UI hides the control unless `standalone` is set.
  state->setProperty("standalone", isStandalone());
  state->setProperty(
      "inputMode",
      inputModeToString(static_cast<InputMode>(standaloneInputMode.load())));
  // The EQ editor mirrors the biquad math client-side; it needs the real
  // sample rate for the drawn curve to match the audio exactly.
  const double sr = getSampleRate();
  state->setProperty("sampleRate", sr > 0.0 ? sr : 48000.0);
  return state.get();
}

juce::var TONE3000Processor::getMeterLevels() const {
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  // Main meters ship as [L, R] pairs (mono sources report L == R). The UI
  // store derives the combined mono value as max(L, R).
  auto channelPair = [](float l, float r) {
    juce::Array<juce::var> pair;
    pair.add(l);
    pair.add(r);
    return juce::var(pair);
  };
  root->setProperty("input", channelPair(inputMeterLevelL.load(), inputMeterLevelR.load()));
  root->setProperty("output", channelPair(outputMeterLevelL.load(), outputMeterLevelR.load()));

  juce::DynamicObject::Ptr blocks = new juce::DynamicObject();
  {
    // Meter values are atomics; the lock only guards chain iteration. Hold
    // time is a few property writes, so contention with the audio thread is
    // negligible even at per-frame polling rates.
    juce::ScopedLock lock(chainMutex);
    for (const auto* chain : {&chainBlocks, &rightChainBlocks}) {
      for (const auto& block : *chain) {
        if (block->type == ChainBlockType::INSERT)
          continue;
        juce::DynamicObject::Ptr levels = new juce::DynamicObject();
        levels->setProperty("in", block->inputMeterDb.load());
        levels->setProperty("out", block->outputMeterDb.load());
        blocks->setProperty(juce::String(block->id), juce::var(levels.get()));
      }
    }
  }
  root->setProperty("blocks", juce::var(blocks.get()));
  return root.get();
}

// ####################
// STEREO MODE
// ####################
void TONE3000Processor::setStereoMode(bool enabled) {
  juce::ScopedLock lock(chainMutex);

  if (stereoEnabled.load() == enabled)
    return;

  pushChainHistory();

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
  bumpChainRevision();
  DBG("Stereo mode " << (enabled ? "enabled" : "disabled"));
}

void TONE3000Processor::setActiveEditChain(const juce::String& side) {
  juce::ScopedLock lock(chainMutex);
  if (side == "right")
    activeEditSide = ChainSide::Right;
  else if (side == "left")
    activeEditSide = ChainSide::Left;
  bumpChainRevision();
}

bool TONE3000Processor::swapChains() {
  juce::ScopedLock lock(chainMutex);

  if (!stereoEnabled.load())
    return false;

  pushChainHistory();
  std::swap(chainBlocks, rightChainBlocks);

  // Keep the insert placeholder ids side-invariant: they are fixed per-side
  // keys used by the UI and by snapshot reconciliation, so they must not
  // travel with the swap.
  auto claimInsert = [](std::vector<std::unique_ptr<ChainBlock>>& chain, const char* id) {
    for (auto& block : chain)
      if (block->type == ChainBlockType::INSERT)
        block->id = id;
  };
  claimInsert(chainBlocks, INSERT_BLOCK_ID);
  claimInsert(rightChainBlocks, INSERT_BLOCK_ID_RIGHT);

  updateLatencyCompensation();
  bumpChainRevision();
  DBG("Swapped Left/Right chains");
  return true;
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

bool TONE3000Processor::setBlockParam(const std::string& blockId, const juce::String& param,
                                      double value) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return false;

  // Validate before recording history, so failed calls never leave an entry.
  const bool isContinuous = param == "inputGain" || param == "outputGain" || param == "mix";
  const bool isKnown = isContinuous || param == "enabled" || param == "namSlimmableSize";
  if (!isKnown) {
    DBG("setBlockParam: unknown param: " << param);
    return false;
  }
  if (param == "namSlimmableSize" &&
      (block->type != ChainBlockType::NAM || !block->namIsSlimmable ||
       block->namResampler == nullptr))
    return false;

  // Continuous params coalesce a whole knob drag into one undo step.
  pushChainHistory(isContinuous ? "param:" + juce::String(blockId) + ":" + param
                                : juce::String());

  if (param == "enabled") {
    const bool enabled = value > 0.5;
    if (block->enabled != enabled) {
      block->enabled = enabled;
      // NAM latency only counts for enabled blocks (chainMutex is re-entrant).
      updateLatencyCompensation();
    }
  } else if (param == "inputGain") {
    block->inputGainNormalized = juce::jlimit(0.0f, 1.0f, static_cast<float>(value));
  } else if (param == "outputGain") {
    block->outputGainNormalized = juce::jlimit(0.0f, 1.0f, static_cast<float>(value));
  } else if (param == "mix") {
    block->mixNormalized = juce::jlimit(0.0f, 1.0f, static_cast<float>(value));
  } else if (param == "namSlimmableSize") {
    const double clamped = juce::jlimit(0.5, 1.0, value);
    block->namSlimmableSize = clamped;
    block->namResampler->setSlimmableSize(clamped);
  }

  bumpChainRevision();
  return true;
}

// ####################
// PER-BLOCK EQ
// ####################
bool TONE3000Processor::setBlockEqBand(const std::string& blockId, int bandIndex,
                                       const juce::var& bandVar) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return false;
  if (!bandVar.isObject() || bandIndex < 0 || bandIndex >= BlockEq::kNumBands)
    return false;

  // A whole dot/slider drag coalesces into one undo step.
  pushChainHistory("eq:" + juce::String(blockId) + ":" + juce::String(bandIndex));

  if (!block->eq.setBandFromVar(bandIndex, bandVar))
    return false;

  bumpChainRevision();
  return true;
}

bool TONE3000Processor::setBlockEqEnabled(const std::string& blockId, bool enabled) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return false;
  if (block->eq.isEnabled() == enabled)
    return true;

  pushChainHistory();
  block->eq.setEnabled(enabled);
  bumpChainRevision();
  return true;
}

bool TONE3000Processor::resetBlockEq(const std::string& blockId) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return false;

  pushChainHistory();
  block->eq.resetToDefault();
  bumpChainRevision();
  return true;
}

bool TONE3000Processor::setBlockSpectrumEnabled(const std::string& blockId, bool enabled) {
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return false;

  block->spectrum.setEnabled(enabled);
  return true;
}

juce::var TONE3000Processor::getBlockSpectrum(const std::string& blockId) {
  // getSpectrum does the FFT on this (message) thread over a lock-free ring,
  // so the chain lock is only held for the block lookup + analysis.
  juce::ScopedLock lock(chainMutex);
  ChainBlock* block = findBlockById(blockId);
  if (block == nullptr || block->type == ChainBlockType::INSERT)
    return {};

  return block->spectrum.getSpectrum();
}

void TONE3000Processor::disableAllBlockSpectrums() {
  juce::ScopedLock lock(chainMutex);
  for (auto* chain : {&chainBlocks, &rightChainBlocks})
    for (auto& block : *chain)
      block->spectrum.setEnabled(false);
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
