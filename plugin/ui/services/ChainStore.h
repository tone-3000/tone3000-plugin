// Single owner of the chain state on the UI side (port of useChainState.ts).
//
// Sync model: native is the source of truth; we hold a revision-tagged
// snapshot. Every UiClock tick compares the processor's atomic revision
// counter with ours (a load, no allocation) and resyncs on a mismatch; every
// mutation resyncs immediately too. Continuous params (knob drags) go
// through setBlockParam fire-and-forget: the control keeps its optimistic
// value, native defers the revision bump until the gesture settles, and the
// resulting resync converges everyone.
#pragma once

#include <juce_events/juce_events.h>

#include "UiClock.h"
#include "backend/Backend.h"
#include "model/ChainState.h"

namespace t3k::ui {

class ChainStore : private UiClock::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void chainChanged(const ChainState& state) = 0;
  };

  ChainStore(Backend& backend, UiClock& clock);
  ~ChainStore() override;

  const ChainState& state() const { return state_; }
  // A fresh pull; `force` ignores the known revision.
  void refresh(bool force = false);

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  // Actions (each resyncs afterwards)
  // Add a tone at an insert slot; the new blockId, "" on failure.
  std::string loadTone(const juce::String& toneJson, const std::string& targetInsertId = {});
  // Load a local .nam/.wav file or a folder of them ("" = success, else a
  // user-facing error).
  juce::String loadLocalTonePath(const juce::File& source, const std::string& targetId);
  juce::String loadLocalToneUrls(const juce::Array<juce::URL>& sources, const std::string& targetId);
  bool swapTone(const std::string& blockId, const juce::String& toneJson);
  bool refreshToneMetadata(const juce::String& toneJson);
  bool switchModel(const std::string& blockId, int modelId, const juce::var& model);
  bool retryModelLoad(const std::string& blockId);
  void removeBlock(const std::string& blockId);
  void reorderBlocks(const std::vector<std::string>& orderedIds);
  bool moveBlockToChain(const std::string& blockId, ChainSide side, int index);
  std::string duplicateBlock(const std::string& sourceBlockId, ChainSide side, int index);
  bool copyBlock(const std::string& blockId);
  std::string pasteBlock(ChainSide side, int index);
  void setStereoMode(bool enabled);
  void setInputMode(InputMode mode);
  bool setBlockSlimSize(const std::string& blockId, double slimSize);
  void setNamSlimSizeDefault(double slimSize);
  void setMultiCore(bool enabled);
  void setActiveSide(ChainSide side);
  bool swapChains();
  bool setBranch(ChainSide side, const std::string& afterBlockId);
  bool clearBranch();
  // Fire-and-forget (safe at drag rates); no resync.
  void setBlockParam(const std::string& blockId, const juce::String& param, double value);
  void setBlockParam(const std::string& blockId, const juce::String& param, bool value) {
    setBlockParam(blockId, param, value ? 1.0 : 0.0);
  }
  void setBlockEqBand(const std::string& blockId, int bandIndex, const EqBand& band);
  bool setBlockEqEnabled(const std::string& blockId, bool enabled);
  bool setBlockEqPre(const std::string& blockId, bool pre);
  bool resetBlockEq(const std::string& blockId);
  bool undo();
  bool redo();
  bool resetToDefault();

private:
  void tick() override;
  template <typename Fn>
  auto run(Fn&& fn) -> decltype(fn());
  juce::String localLoadResult(const juce::var& res);

  Backend& backend_;
  UiClock& clock_;
  ChainState state_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
