#include "ChainStore.h"
#include "core/Help.h"

namespace t3k::ui {

ChainStore::ChainStore(Backend& backend, UiClock& clock) : backend_(backend), clock_(clock) {
  state_.revision = -1;
  refresh(true);
  clock_.addListener(this);
}

ChainStore::~ChainStore() { clock_.removeListener(this); }

void ChainStore::refresh(bool force) {
  const auto res = backend_.getChainState(force ? -1 : state_.revision);
  if (!res.isObject() || ChainState::isUnchanged(res)) return;
  auto next = ChainState::parse(res);
  const auto feedback = state_.revision < 0 ? juce::String() : loadingFeedback(state_, next);
  state_ = std::move(next);
  listeners.call([this](Listener& l) { l.chainChanged(state_); });
  help::announce(feedback);
}

juce::String ChainStore::loadingFeedback(const ChainState& before, const ChainState& after) {
  // Failures each get their own message (each needs its own Retry); loads
  // and finished loads name the tone when there is one, and count them when
  // several land at once (a preset), so the reader is not tied up for long.
  juce::StringArray failures, loading, loaded;
  const auto status = [](const ChainItem& item) {
    return item.loadFailed ? 2 : (item.modelLoading || !item.loaded ? 0 : 1);
  };
  const auto inspect = [&](const std::vector<ChainItem>& items, const juce::String& side) {
    for (size_t i = 0; i < items.size(); ++i) {
      const auto& item = items[i];
      if (!item.isTone()) continue;
      const auto* old = before.findBlock(item.blockId);
      const bool changedModel = old != nullptr &&
          (old->activeModelId != item.activeModelId || old->tone.id != item.tone.id || old->tone.title != item.tone.title);
      if (old != nullptr && old->isTone() && status(*old) == status(item) && !changedModel) continue;
      const auto label = item.tone.title + ", " + side + "position " + juce::String(static_cast<int>(i) + 1);
      if (status(item) == 2) failures.add("Could not load " + label + ". Use Retry load to try again.");
      else if (status(item) == 0) loading.add(label);
      else loaded.add(label);
    }
  };
  inspect(after.chain, after.stereoEnabled ? "left chain, " : "");
  if (after.stereoEnabled && after.chainRight) inspect(*after.chainRight, "right chain, ");
  const auto summary = [](const juce::String& verb, const juce::StringArray& labels) -> juce::String {
    if (labels.isEmpty()) return {};
    if (labels.size() == 1) return verb + " " + labels[0];
    return verb + " " + juce::String(labels.size()) + " tones";
  };
  juce::StringArray messages;
  messages.addArray(failures);
  for (const auto& m : {summary("Loading", loading), summary("Loaded", loaded)})
    if (m.isNotEmpty()) messages.add(m);
  return messages.joinIntoString(". ");
}

void ChainStore::tick() {
  if (static_cast<int>(backend_.chainRevision()) != state_.revision) refresh();
}

template <typename Fn>
auto ChainStore::run(Fn&& fn) -> decltype(fn()) {
  auto result = fn();
  refresh();
  return result;
}

juce::String ChainStore::localLoadResult(const juce::var& res) {
  if (res.isObject() && res.getProperty("blockId", "").toString().isNotEmpty()) return {};
  const auto error = res.isObject() ? res.getProperty("error", "").toString() : juce::String();
  return error.isNotEmpty() ? error : "Couldn't load the file";
}

std::string ChainStore::loadTone(const juce::String& toneJson, const std::string& targetInsertId) {
  return run([&] { return backend_.loadTone(toneJson, targetInsertId); });
}

juce::String ChainStore::loadLocalTonePath(const juce::File& source, const std::string& targetId) {
  return localLoadResult(run([&] { return backend_.loadLocalTonePath(source, targetId); }));
}

juce::String ChainStore::loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                                           const std::string& targetId) {
  return localLoadResult(run([&] { return backend_.loadLocalToneUrls(sources, targetId); }));
}

bool ChainStore::swapTone(const std::string& blockId, const juce::String& toneJson) {
  return run([&] { return backend_.swapTone(blockId, toneJson); });
}

bool ChainStore::refreshToneMetadata(const juce::String& toneJson) {
  return run([&] { return backend_.refreshToneMetadata(toneJson); });
}

bool ChainStore::switchModel(const std::string& blockId, int modelId, const juce::var& model) {
  return run([&] { return backend_.switchModel(blockId, modelId, model); });
}

bool ChainStore::retryModelLoad(const std::string& blockId) {
  return run([&] { return backend_.retryModelLoad(blockId); });
}

void ChainStore::removeBlock(const std::string& blockId) {
  run([&] { return backend_.removeChainBlock(blockId); });
}

void ChainStore::reorderBlocks(const std::vector<std::string>& orderedIds) {
  run([&] { return backend_.reorderChainBlocks(orderedIds); });
}

bool ChainStore::moveBlockToChain(const std::string& blockId, ChainSide side, int index) {
  return run([&] { return backend_.moveBlockToChain(blockId, toString(side), index); });
}

std::string ChainStore::duplicateBlock(const std::string& sourceBlockId, ChainSide side, int index) {
  return run([&] { return backend_.duplicateChainBlock(sourceBlockId, toString(side), index); });
}

bool ChainStore::copyBlock(const std::string& blockId) {
  return run([&] { return backend_.copyChainBlock(blockId); });
}

std::string ChainStore::pasteBlock(ChainSide side, int index) {
  return run([&] { return backend_.pasteChainBlock(toString(side), index); });
}

void ChainStore::setStereoMode(bool enabled) {
  backend_.setStereoMode(enabled);
  refresh();
}

void ChainStore::setInputMode(InputMode mode) {
  backend_.setInputMode(toString(mode));
  refresh();
}

bool ChainStore::setBlockSlimSize(const std::string& blockId, double slimSize) {
  return run([&] { return backend_.setBlockSlimSize(blockId, slimSize); });
}

bool ChainStore::setBlockIrStereo(const std::string& blockId, bool stereo) {
  return run([&] { return backend_.setBlockIrStereo(blockId, stereo); });
}

void ChainStore::setNamSlimSizeDefault(double slimSize) {
  backend_.setNamSlimSizeDefault(slimSize);
  refresh();
}

void ChainStore::setMultiCore(bool enabled) {
  backend_.setMultiCore(enabled);
  refresh();
}

void ChainStore::setActiveSide(ChainSide side) {
  backend_.setActiveEditChain(toString(side));
  refresh();
}

bool ChainStore::swapChains() {
  return run([&] { return backend_.swapChains(); });
}

bool ChainStore::setBranch(ChainSide side, const std::string& afterBlockId) {
  const bool changed = run([&] { return backend_.setChainBranch(toString(side), afterBlockId); });
  help::announce(changed ? "Branch set. The other chain receives audio after this effect."
                         : "Could not set branch. Branching requires Stereo mode and a loaded effect.");
  return changed;
}

bool ChainStore::clearBranch() {
  const bool changed = run([&] { return backend_.clearChainBranch(); });
  help::announce(changed ? "Branch removed. Chains are independent." : "Could not remove branch.");
  return changed;
}

void ChainStore::setBlockParam(const std::string& blockId, const juce::String& param, double value) {
  backend_.setBlockParam(blockId, param, value);
}

void ChainStore::setBlockEqBand(const std::string& blockId, int bandIndex, const EqBand& band) {
  backend_.setBlockEqBand(blockId, bandIndex, band.toVar());
}

bool ChainStore::setBlockEqEnabled(const std::string& blockId, bool enabled) {
  return run([&] { return backend_.setBlockEqEnabled(blockId, enabled); });
}

bool ChainStore::setBlockEqPre(const std::string& blockId, bool pre) {
  return run([&] { return backend_.setBlockEqPre(blockId, pre); });
}

bool ChainStore::resetBlockEq(const std::string& blockId) {
  return run([&] { return backend_.resetBlockEq(blockId); });
}

bool ChainStore::undo() {
  return run([&] { return backend_.undoChain(); });
}

bool ChainStore::redo() {
  return run([&] { return backend_.redoChain(); });
}

bool ChainStore::resetToDefault() {
  return run([&] { return backend_.resetToDefault(); });
}

}  // namespace t3k::ui
