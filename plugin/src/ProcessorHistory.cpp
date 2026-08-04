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
  // Branch routing travels with the chains (undo, presets, DAW state all
  // share this shape). Empty branchAfterBlockId = independent chains.
  snapshot.setProperty("branchSide",
                       branchSourceSide == ChainSide::Right ? "right" : "left", nullptr);
  snapshot.setProperty("branchAfterBlockId", juce::String(branchAfterBlockId), nullptr);

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

void TONE3000Processor::queueActiveModelLoad(ChainBlock& block) {
  // Every bail below leaves the block unloadable — flag it so the UI shows
  // the retry affordance instead of a loader that can never resolve, and log
  // at release level (these paths are the needle for "stuck loading after
  // relaunch" reports).
  auto bail = [&block](const juce::String& reason) {
    juce::Logger::writeToLog("[ModelLoader] Cannot queue load for block " +
                             juce::String(block.id) + ": " + reason);
    block.loaded = false;
    block.loadFailed = true;
    block.modelLoading = false;
  };

  juce::DynamicObject* toneObj = block.toneVar.getDynamicObject();
  if (toneObj == nullptr) {
    bail("stored tone JSON did not parse");
    return;
  }

  juce::var modelsVar = toneObj->getProperty("models");
  if (!modelsVar.isArray()) {
    bail("stored tone JSON has no models array");
    return;
  }

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

  bail("active model " + juce::String(block.activeModelId) + " not in stored tone JSON");
}

void TONE3000Processor::reconcileChainFromTree(const juce::ValueTree& chainState, Lane& target,
                                               Lane& retired) {
  // Park the live blocks by id so matching ones can be moved back with their
  // engines/model caches intact. Anything left over at the end is a removal
  // and goes into `retired` — the caller destroys those after releasing
  // chainMutex (engine teardown is heavy).
  std::map<std::string, std::unique_ptr<ChainBlock>> existing;
  for (auto& b : target)
    if (b)
      existing[b->id] = std::move(b);
  target.clear();

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
      block->modelLoading = true;
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
        const juce::var dataVar = cachedModel.getProperty("data");
        if (const auto* raw = dataVar.getBinaryData()) {
          // Current format: raw bytes straight out of the binary stream.
          const auto* bytes = static_cast<const uint8_t*>(raw->getData());
          block->modelCache[modelId].assign(bytes, bytes + raw->getSize());
          continue;
        }
        // Legacy XML states/presets carried the bytes as Base64 text.
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64(decoded, dataVar.toString())) {
          const auto* bytes = static_cast<const uint8_t*>(decoded.getData());
          block->modelCache[modelId].assign(bytes, bytes + decoded.getDataSize());
        } else {
          juce::Logger::writeToLog("[Restore] Embedded model bytes for model " +
                                   juce::String(modelId) + " failed to decode (block " +
                                   juce::String(block->id) + ") — will refetch");
        }
      }
      // One release-level line per reloading block: enough to diagnose
      // "stuck loading after relaunch" reports from a user's log file.
      juce::Logger::writeToLog(
          "[Restore] Block " + juce::String(block->id) + " tone " + juce::String(toneId) +
          " model " + juce::String(activeModelId) +
          (block->modelCache.count(activeModelId) != 0 ? " (cached)" : " (needs fetch)") +
          " queued for load");
      queueActiveModelLoad(*block);
    }

    target.push_back(std::move(block));
  }

  // Reconciled chains always come back up to the minimum slot layout —
  // snapshots from this build already satisfy the invariant (no-op); legacy
  // states/presets that carried a single insert get padded here.
  normalizeLaneInserts(target);

  // Whatever is still parked was removed by this restore.
  for (auto& [id, b] : existing)
    retired.push_back(std::move(b));
}

TONE3000Processor::Lane TONE3000Processor::restoreChainSnapshot(const juce::ValueTree& snapshot) {
  Lane retired;
  if (!snapshot.isValid())
    return retired;

  reconcileChainFromTree(snapshot.getChildWithName("ChainBlocks"), lane(ChainSide::Left), retired);
  reconcileChainFromTree(snapshot.getChildWithName("RightChainBlocks"), lane(ChainSide::Right),
                         retired);

  const bool wasStereo = stereoEnabled.load();
  const bool snapStereo = static_cast<bool>(snapshot.getProperty("stereoEnabled", false));

  auto& right = lane(ChainSide::Right);
  stereoEnabled.store(snapStereo);
  if (!snapStereo)
    pendingAddSide = ChainSide::Left;

  // Branch routing rides the snapshot. refreshBranchTapIndex validates it
  // against the freshly reconciled trunk lane — a stale id (snapshot from a
  // chain that no longer holds the block) degrades to independent chains.
  // Older snapshots without the properties restore as unbranched for free.
  branchSourceSide = snapshot.getProperty("branchSide").toString() == "right"
                         ? ChainSide::Right
                         : ChainSide::Left;
  branchAfterBlockId =
      snapshot.getProperty("branchAfterBlockId").toString().toStdString();
  refreshBranchTapIndex();

  // A restored *active* branch + a stereo input fold would silently drop a
  // channel — enforce the same invariant setChainBranch does (presets don't
  // carry inputMode; DAW states restore it just before this runs). A dormant
  // branch (mono snapshot) doesn't constrain the fold.
  if (rtBranchTapIndex >= 0 && getInputMode() == InputMode::Stereo)
    inputMode.store(static_cast<int>(InputMode::Left));

  // Mirrors setStereoMode: the right chain's engines must be ready before the
  // audio thread starts running them.
  if (snapStereo && !wasStereo)
    prepareChain(right);

  // Restores can add/remove/retire IR blocks wholesale (undo/redo, presets,
  // project load) — resync the host-facing tail length.
  refreshIrTailLength();

  bumpChainRevision();
  return retired;
}

bool TONE3000Processor::undoChain() {
  // No-op undos (hotkey spam at the stack end) must not dip the audio, so
  // check the stack before arming the fade.
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canUndo())
      return false;
  }

  // A restore can restructure the chain arbitrarily — mute-splice it like
  // any structural edit. Constructed before the lock: the audio thread
  // needs chainMutex to run the fade down.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canUndo())
      return false;
    retired = restoreChainSnapshot(chainHistory.undo(captureChainSnapshot()));
  }
  DBG("Chain undo applied");

  // The restore may have queued background reloads (undone model/tone
  // changes) — hold the mute until they land (bounded), not just through
  // the splice, or the dry input plays while the engines rebuild. A
  // settings-only undo has nothing loading and releases immediately.
  editFade.releaseWhenChainLoadsSettle();
  return true;
}

bool TONE3000Processor::redoChain() {
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canRedo())
      return false;
  }

  // See undoChain: mute-splice the restore.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    if (!chainHistory.canRedo())
      return false;
    retired = restoreChainSnapshot(chainHistory.redo(captureChainSnapshot()));
  }
  DBG("Chain redo applied");

  // See undoChain: hold the mute through any queued reloads.
  editFade.releaseWhenChainLoadsSettle();
  return true;
}
