// Previous / next for a tone block: steps the block through the result list
// its tone was picked from (the block's browse context, BrowseContext.h),
// without opening the browser. Each block has its own cursor, so one block
// can stay put while another cycles.
//
// A step resolves the neighbouring tone (its first loadable model, a fresh
// token for native) and swaps it into the block in place, carrying the
// moved cursor along in the same undoable edit. Off either end of the
// stored page it fetches the adjacent page of the same query first. The
// ends of the whole result list stop rather than wrap: when playing, a
// sudden jump back to result 1 is more confusing than a "last result" note.
//
// Rapid presses: every press moves the shown cursor at once and starts its
// own resolve; replies older than the block's latest press are dropped, so
// the last press always wins and nothing lands out of order.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>

#include "ChainStore.h"
#include "ConnectionGate.h"
#include "Toast.h"
#include "ToneSession.h"
#include "core/AsyncScope.h"

namespace t3k::ui {

class ToneStepper {
public:
  // Where a block's cursor stands, for the ‹ › controls and their label.
  struct Cursor {
    int position = 0;  // 1-based across the whole result list
    int total = 0;     // results in the whole list (an estimate past page 1)
    bool canPrev = false, canNext = false;
    bool busy = false;  // a step is resolving
  };

  struct Listener {
    virtual ~Listener() = default;
    virtual void stepperChanged() = 0;
  };

  ToneStepper(ChainStore& chain, ToneSession& session, ConnectionGate& connection, Toast& toast);
  ~ToneStepper();

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

  // The block's cursor, or nothing when it has no browse context.
  std::optional<Cursor> cursor(const std::string& blockId) const;
  // Step by `delta` results (±1 from the buttons and keys).
  void step(const std::string& blockId, int delta);

  // The block arrow keys step: the last one stepped, picked or opened.
  const std::string& focusBlock() const { return focus_; }
  void setFocusBlock(const std::string& blockId);

  // The keyboard's way in (← / → step the focused block, Shift+← / → move
  // the focus between blocks that have a cursor). False when there is
  // nothing to act on, so the key can go elsewhere.
  bool stepFocused(int delta);
  bool moveFocus(int direction);

  // The browse context for a pick from the browser: `page` of `query`
  // (its results in order) with the cursor on `index`.
  static juce::var makeContext(const juce::var& query, const TonePage& page, int pageSize, int index);

private:
  struct Pending {
    int generation = 0;
    int index = 0;  // the shown cursor while resolving (within `context`)
    juce::var context;
  };

  void land(const std::string& blockId, juce::var context, int index);
  void resolve(const std::string& blockId, juce::var context, int index);
  void fetchPage(const std::string& blockId, const juce::var& context, int page, bool fromEnd);
  juce::var contextOf(const std::string& blockId) const;
  void notify();

  ChainStore& chain_;
  ToneSession& session_;
  ConnectionGate& connection_;
  Toast& toast_;
  std::map<std::string, Pending> pending_;
  std::string focus_;
  juce::ListenerList<Listener> listeners_;
  AsyncScope scope_;
};

}  // namespace t3k::ui
