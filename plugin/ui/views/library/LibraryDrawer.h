// The Library side drawer (plugin/docs/library.md): a panel docked at the
// left edge under the header, beside the chain (which narrows to make room)
// over the faceplate's left end, so items can be dragged straight onto a
// tile. Header (title, new folder, more menu, close), search, the tree, and
// a footer naming the Library folder.
//
// The tree is one row per visible node (LibraryStore + libraryRows): a
// click opens or closes a folder, a double-click uses an item (a preset
// loads; a tone or capture auditions, swapping the block it last went into), a drag carries the node
// onto a gallery tile or into another folder (yours move, anyone else's copy
// in), files dropped from the OS land in the folder under them, and a
// right-click opens the node's actions. While a tile's "Add to Library" is
// pending, a strip asks for a folder and the next folder click takes it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/DelayedCall.h"
#include "services/Services.h"
#include "widgets/ContextMenu.h"
#include "widgets/DragScroller.h"
#include "widgets/IconButton.h"
#include "widgets/TextField.h"

namespace t3k::ui {

class LibraryDrawer : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private LibraryStore::Listener,
                      private ChainStore::Listener {
public:
  static constexpr int kWidth = 272;

  explicit LibraryDrawer(Services& services);
  ~LibraryDrawer() override;

  std::function<void()> onClose;

  void paint(juce::Graphics& g) override;
  void resized() override;
  // The keyboard once a row was clicked: Up / Down move the selection, Enter
  // loads it (a folder opens or closes), Right opens a folder (then steps
  // into it), Left closes it (else steps up to its folder).
  bool keyPressed(const juce::KeyPress& key) override;
  // A digit typed (keyPressed): digits close together make one number, the
  // capture or tone at that place in the selected row's folder (from 1), as
  // long as the folder has that many. It is selected and, with the setting
  // on (UiPrefs::kLibraryNumberLoads), loaded once no more digits can follow.
  void typeNumber(int digit);
  // After a load made from the keyboard: the keyboard back here (a host can
  // take it from the plugin's window as the chain changes).
  void keepKeyboard();
  int typed_ = 0;
  juce::uint32 lastDigitMs_ = 0;
  DelayedCall numberLoad_;
  static constexpr juce::uint32 kNumberPauseMs = 800;
  // The strips above the list: a pick (Add here), a share asking TONE3000, the
  // keep folder; and the missing files.
  void updateStrips();

  bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
  void fileDragMove(const juce::StringArray&, int x, int y) override;
  void fileDragExit(const juce::StringArray&) override;
  void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
  class Row;
  class Prompt;
  class Strip;

  void libraryChanged() override;
  void chainChanged(const ChainState&) override;
  void rebuild();
  void layoutRows();
  // Rows exist only for the visible window (plus a margin): a folder of
  // thousands of captures costs what a screenful does.
  void updateVisibleRows();
  // Centre `path`'s row in the list (LibraryStore::focus).
  void scrollToRow(const juce::String& path);
  // The saved scroll (LibraryStore's view) is put back once, when the tree
  // first shows; the restore itself isn't a scroll to save.
  bool scrollPending_ = true, restoringScroll_ = false;

  // Row gestures.
  void rowClicked(const LibraryNode& node);
  void rowDoubleClicked(const LibraryNode& node);
  void rowMenu(Row& row, juce::Point<int> at);
  void openMenu(std::vector<ContextMenu::Item> items, juce::Component& at, juce::Point<int> point);
  // The links to files that moved (LibraryStore::missing): each to find,
  // Find All, Forget These Links.
  void showMissingList(juce::Component& at);
  std::vector<ContextMenu::Item> menuFor(const LibraryNode& node);
  // The folder new things go into: the selected container (or the selected
  // item's), when it takes them, else your library.
  juce::String targetFolder() const;
  // The row under a point in this component's space, or nullptr.
  Row* rowAt(juce::Point<int> p);
  void setDropHighlight(const juce::String& path);

  // Name prompts and the delete confirmation share one popover.
  void promptName(const juce::String& title, const juce::String& prefill, const juce::String& action,
                  std::function<void(const juce::String&)> done);
  void confirmDelete(const LibraryNode& node);

  Services& services_;
  IconButton newFolder_{Icon::FolderPlus, 28};
  IconButton more_{Icon::Ellipsis, 28};
  IconButton close_{Icon::X, 28};
  TextField search_;
  DelayedCall searchWait_;
  void applySearch();
  class GearRow;
  std::unique_ptr<GearRow> gears_;
  std::unique_ptr<Strip> strip_, missingStrip_;
  static constexpr int kScrollBarWidth = 10;
  DragScroller scroller_{DragScroller::Axis::vertical};
  juce::Component content_;
  // Every row the tree shows (cheap), and components for the visible ones,
  // by index into it.
  std::vector<LibraryRow> model_;
  std::map<int, std::unique_ptr<Row>> rows_;
  std::unique_ptr<ContextMenu> menu_;
  std::unique_ptr<Prompt> prompt_;
  juce::String dropHighlight_;
  // A folder dragged to just before / after another of its folder: the line
  // between them (dropLine_ the row, dropLineAfter_ its bottom edge).
  juce::String dropLine_;
  bool dropLineAfter_ = false;
  void setDropLine(const juce::String& path, bool after);
  // Where a drag over `row` lands (beside it, into it, nowhere), marked.
  void dragOver(Row& row, const juce::DragAndDropTarget::SourceDetails& details);
  // While a row is dragged: near the list's top or bottom edge the list
  // scrolls, faster the closer to the edge (JUCE's drags don't).
  struct DragScroll : juce::Timer {
    std::function<void()> tick;
    void timerCallback() override { tick(); }
  } dragScroll_;
  void startDragScroll();
  void dragScrollTick();
  juce::String activePresetId_;
  // What the chain plays: local blocks' files, TONE3000 blocks' tone ids
  // (the rows that show it get the active dot), and the audition block's
  // file the selection follows.
  std::set<juce::String> playingFiles_;
  std::set<int> playingTones_;
  juce::String followed_;
  bool playing(const LibraryNode& node) const;
};

}  // namespace t3k::ui
