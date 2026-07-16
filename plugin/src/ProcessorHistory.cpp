#include "Processor.h"
#include <map>

// #############################
// UNDO / REDO (chain history)
// #############################
//
// Snapshot-based: every chain mutator records the pre-mutation settings
// (ChainHistory entries are settings-only ValueTrees — tone JSON + params,
// never model bytes). Undo/redo restore a snapshot by *reconciling* it
// against the live chains: blocks whose id/tone/model still match keep their
// loaded engines and in-memory model caches, so undoing a knob tweak costs a
// few property writes, while undoing a structural edit only reloads the
// blocks that actually changed.

juce::ValueTree TONE3000Processor::captureChainSnapshot(bool includeModelData) const {
  juce::ValueTree snapshot("ChainSnapshot");
  snapshot.setProperty("stereoEnabled", stereoEnabled.load(), nullptr);

  juce::ValueTree left("ChainBlocks");
  serializeChainToTree(lane(ChainSide::Left), left, includeModelData);
  snapshot.appendChild(left, nullptr);

  juce::ValueTree right("RightChainBlocks");
  serializeChainToTree(lane(ChainSide::Right), right, includeModelData);
  snapshot.appendChild(right, nullptr);

  return snapshot;
}

void TONE3000Processor::pushChainHistory(const juce::String& coalesceKey) {
  // Mid-gesture updates skip the (comparatively pricey) serialization
  // entirely — the entry on top of the stack already holds the pre-gesture
  // state, which is exactly what undo should restore.
  if (chainHistory.shouldCoalesce(coalesceKey))
    return;
  chainHistory.push(captureChainSnapshot(), coalesceKey);
}

void TONE3000Processor::queueActiveModelLoad(const ChainBlock& block) {
  juce::DynamicObject* toneObj = block.toneVar.getDynamicObject();
  if (toneObj == nullptr)
    return;

  juce::var modelsVar = toneObj->getProperty("models");
  if (!modelsVar.isArray())
    return;

  for (const auto& modelVar : *modelsVar.getArray()) {
    juce::DynamicObject* modelObj = modelVar.getDynamicObject();
    if (modelObj == nullptr || static_cast<int>(modelObj->getProperty("id")) != block.activeModelId)
      continue;

    const juce::String modelUrl = modelObj->getProperty("model_url").toString();
    const juce::String modelName = modelObj->getProperty("name").toString();

    // switchModelInBackground prefers the block's in-memory model cache and
    // only hits the network when the bytes are gone — ideal for undo/redo.
    loadingThreadPool.addJob(std::function<void()>(
        [this, blockId = block.id, modelId = block.activeModelId, modelUrl, modelName]() {
          switchModelInBackground(blockId, modelId, modelUrl, modelName);
        }));
    return;
  }

  DBG("queueActiveModelLoad: model " << block.activeModelId
                                     << " not found in tone JSON for block " << block.id);
}

void TONE3000Processor::reconcileChainFromTree(const juce::ValueTree& chainState, Lane& target,
                                               const char* insertBlockId, Lane& retired) {
  // Park the live blocks by id so matching ones can be moved back with their
  // engines/model caches intact. Anything left over at the end is a removal
  // and goes into `retired` — the caller destroys those after releasing
  // chainMutex (engine teardown is heavy).
  std::map<std::string, std::unique_ptr<ChainBlock>> existing;
  for (auto& b : target)
    if (b)
      existing[b->id] = std::move(b);
  target.clear();

  bool hasInsertBlock = false;

  for (int i = 0; i < chainState.getNumChildren(); ++i) {
    juce::ValueTree blockState = chainState.getChild(i);
    if (!blockState.hasType("ChainBlock"))
      continue;

    const std::string blockId = blockState.getProperty("id").toString().toStdString();
    const ChainBlockType type =
        chainBlockTypeFromString(blockState.getProperty("type").toString());
    const int toneId = blockState.getProperty("toneId", 0);

    // Reusable when identity matches: same block, same type, same tone. A
    // swapped tone (same id, different tone) rebuilds like a fresh block.
    std::unique_ptr<ChainBlock> block;
    auto it = existing.find(blockId);
    if (it != existing.end() && it->second->type == type &&
        (type == ChainBlockType::INSERT || it->second->toneId == toneId)) {
      block = std::move(it->second);
      existing.erase(it);
    } else {
      block = std::make_unique<ChainBlock>(blockId, type);
    }

    applyBlockSettings(*block, blockState);

    if (type == ChainBlockType::INSERT) {
      hasInsertBlock = true;
      target.push_back(std::move(block));
      continue;
    }

    const int activeModelId = blockState.getProperty("activeModelId", 0);
    const bool modelChanged = block->activeModelId != activeModelId;
    const juce::String toneJson = blockState.getProperty("toneJson").toString();
    // Re-parse the cached tone var/summary only when the tone actually
    // changed (reused blocks keep theirs; fresh blocks always parse).
    if (!block->toneVar.isObject() || block->toneJson != toneJson)
      setToneOnBlock(*block, toneId, toneJson, juce::JSON::parse(toneJson));
    else
      block->toneId = toneId;
    block->activeModelId = activeModelId;

    // Engines survive only when the loaded model is still the right one.
    // Everything else (fresh block, model switch, load still in flight)
    // goes through the background loader — cache-first, network fallback.
    if (modelChanged || !block->loaded) {
      block->loaded = false;
      block->loadFailed = false;  // fresh load queued below — back to loading UI
      // Project files and presets embed model bytes; seed the in-memory cache
      // with *all* of them so offline model switching keeps working and a
      // later save doesn't silently drop the non-active models. Undo
      // snapshots are settings-only (no ModelCache child) — this is a no-op
      // there.
      const juce::ValueTree cacheState = blockState.getChildWithName("ModelCache");
      for (int j = 0; j < cacheState.getNumChildren(); ++j) {
        const juce::ValueTree cachedModel = cacheState.getChild(j);
        const int modelId = cachedModel.getProperty("modelId");
        if (block->modelCache.find(modelId) != block->modelCache.end())
          continue;
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64(decoded, cachedModel.getProperty("data").toString())) {
          const auto* bytes = static_cast<const uint8_t*>(decoded.getData());
          block->modelCache[modelId].assign(bytes, bytes + decoded.getDataSize());
        }
      }
      queueActiveModelLoad(*block);
    }

    target.push_back(std::move(block));
  }

  // Reconciled chains always keep an insert placeholder at hand.
  if (!hasInsertBlock)
    target.push_back(std::make_unique<ChainBlock>(insertBlockId, ChainBlockType::INSERT));

  // Whatever is still parked was removed by this restore.
  for (auto& [id, b] : existing)
    retired.push_back(std::move(b));
}

TONE3000Processor::Lane TONE3000Processor::restoreChainSnapshot(const juce::ValueTree& snapshot) {
  Lane retired;
  if (!snapshot.isValid())
    return retired;

  reconcileChainFromTree(snapshot.getChildWithName("ChainBlocks"), lane(ChainSide::Left),
                         INSERT_BLOCK_ID, retired);
  reconcileChainFromTree(snapshot.getChildWithName("RightChainBlocks"), lane(ChainSide::Right),
                         INSERT_BLOCK_ID_RIGHT, retired);

  const bool wasStereo = stereoEnabled.load();
  const bool snapStereo = static_cast<bool>(snapshot.getProperty("stereoEnabled", false));

  auto& right = lane(ChainSide::Right);
  if (snapStereo && right.empty())
    right.push_back(std::make_unique<ChainBlock>(INSERT_BLOCK_ID_RIGHT, ChainBlockType::INSERT));

  stereoEnabled.store(snapStereo);
  if (!snapStereo)
    pendingAddSide = ChainSide::Left;

  // Mirrors setStereoMode: the right chain's engines must be ready before the
  // audio thread starts running them.
  if (snapStereo && !wasStereo)
    prepareChain(right);

  bumpChainRevision();
  return retired;
}

bool TONE3000Processor::undoChain() {
  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canUndo())
      return false;
    retired = restoreChainSnapshot(chainHistory.undo(captureChainSnapshot()));
  }
  DBG("Chain undo applied");
  return true;
}

bool TONE3000Processor::redoChain() {
  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canRedo())
      return false;
    retired = restoreChainSnapshot(chainHistory.redo(captureChainSnapshot()));
  }
  DBG("Chain redo applied");
  return true;
}
