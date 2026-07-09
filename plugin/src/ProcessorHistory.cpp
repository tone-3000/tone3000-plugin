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

juce::ValueTree TONE3000Processor::captureChainSnapshot() const {
  juce::ValueTree snapshot("ChainSnapshot");
  snapshot.setProperty("stereoEnabled", stereoEnabled.load(), nullptr);

  juce::ValueTree left("ChainBlocks");
  serializeChainToTree(chainBlocks, left, false);
  snapshot.appendChild(left, nullptr);

  juce::ValueTree right("RightChainBlocks");
  serializeChainToTree(rightChainBlocks, right, false);
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
  juce::var toneVar = juce::JSON::parse(block.toneJson);
  juce::DynamicObject* toneObj = toneVar.getDynamicObject();
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

void TONE3000Processor::reconcileChainFromTree(const juce::ValueTree& chainState,
                                               std::vector<std::unique_ptr<ChainBlock>>& target,
                                               const char* insertBlockId) {
  // Park the live blocks by id so matching ones can be moved back with their
  // engines/model caches intact. Anything left over at the end is a removal.
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
    block->toneId = toneId;
    block->toneJson = blockState.getProperty("toneJson").toString();
    block->activeModelId = activeModelId;

    // Engines survive only when the loaded model is still the right one.
    // Everything else (fresh block, model switch, load still in flight)
    // goes through the background loader — cache-first, network fallback.
    if (modelChanged || !block->loaded) {
      block->loaded = false;
      queueActiveModelLoad(*block);
    }

    target.push_back(std::move(block));
  }

  // Snapshots always contain the insert placeholder, but keep the same
  // guarantee restoreChainFromTree provides.
  if (!hasInsertBlock)
    target.push_back(std::make_unique<ChainBlock>(insertBlockId, ChainBlockType::INSERT));
}

void TONE3000Processor::restoreChainSnapshot(const juce::ValueTree& snapshot) {
  if (!snapshot.isValid())
    return;

  reconcileChainFromTree(snapshot.getChildWithName("ChainBlocks"), chainBlocks, INSERT_BLOCK_ID);
  reconcileChainFromTree(snapshot.getChildWithName("RightChainBlocks"), rightChainBlocks,
                         INSERT_BLOCK_ID_RIGHT);

  const bool wasStereo = stereoEnabled.load();
  const bool snapStereo = static_cast<bool>(snapshot.getProperty("stereoEnabled", false));

  if (snapStereo && rightChainBlocks.empty())
    rightChainBlocks.push_back(
        std::make_unique<ChainBlock>(INSERT_BLOCK_ID_RIGHT, ChainBlockType::INSERT));

  stereoEnabled.store(snapStereo);
  if (!snapStereo)
    activeEditSide = ChainSide::Left;

  // Mirrors setStereoMode: the right chain's engines must be ready before the
  // audio thread starts running them.
  const double sr = getSampleRate();
  if (snapStereo && !wasStereo && sr > 0.0 && maxBlockSize > 0)
    prepareChain(rightChainBlocks, sr, maxBlockSize);

  updateLatencyCompensation();
  bumpChainRevision();
}

bool TONE3000Processor::undoChain() {
  juce::ScopedLock lock(chainMutex);
  if (!chainHistory.canUndo())
    return false;
  restoreChainSnapshot(chainHistory.undo(captureChainSnapshot()));
  DBG("Chain undo applied");
  return true;
}

bool TONE3000Processor::redoChain() {
  juce::ScopedLock lock(chainMutex);
  if (!chainHistory.canRedo())
    return false;
  restoreChainSnapshot(chainHistory.redo(captureChainSnapshot()));
  DBG("Chain redo applied");
  return true;
}
