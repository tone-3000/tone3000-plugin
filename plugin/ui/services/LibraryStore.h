// The Library (plugin/docs/library.md) on the UI side: the tree, the
// drawer's per-editor view state (open folders, selection), and every action
// on it: using an item (a preset loads, a tone reference or capture lands in
// the chain), editing the folders, adding a chain block, export / import,
// and the base folder.
//
// The location is per machine (UiPrefs): the base folder, your library's
// folder name in it, and the folders linked into your library. The name is
// fixed the first time the Library is used (your TONE3000 username when
// signed in, else "My Library") so signing in or out later never turns your
// library into someone else's.
//
// Keeping: every block card has a Keep button that copies the model the
// block is playing into a Library folder: the keep folder once one is
// picked (Keep Here), else one you pick. Audition a folder of captures in
// one block, keep the best, move on to the next folder: a curated
// collection without leaving the chain. The keep folder is a pref, so a
// long session survives reopening. Each kept capture remembers the file it
// was kept from (kKeptPref), so a card can jump between the two: from a
// kept copy back to its original folder (to try its neighbours), and from
// an original to the copies of it you kept.
//
// Favorites mirror TONE3000: a folder at the top of your Captures listing
// the tones you favorited on the site, fetched when signed in and cached
// (prefs) so it still browses offline. Adding a tone there favorites it on
// the site; removing one unfavorites it. Nothing is written to disk for it.
//
// The tree is read when a view subscribes and after every edit, plus when
// the preset list changes (a save from the preset bar lands in the Presets
// folder); with nobody subscribed nothing is scanned. Scans run on a worker
// thread (a linked capture collection can take seconds) and land on the
// message thread; the last tree stays for the editor's life, so reopening
// the drawer shows it at once while a fresh scan catches up.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "ChainStore.h"
#include "ConnectionGate.h"
#include "PresetStore.h"
#include "Toast.h"
#include "ToneArt.h"
#include "ToneSession.h"
#include "UiPrefs.h"
#include "core/AsyncScope.h"
#include "core/DelayedCall.h"
#include "model/Library.h"

namespace t3k::ui {

class LibraryStore : private PresetStore::Listener,
                     private ToneSession::Listener,
                     private ChainStore::Listener,
                     private UiPrefs::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void libraryChanged() = 0;
  };

  static constexpr const char* kRootPref = "t3k.libraryRoot";
  // The Library's folder with no kRootPref: the testbed's own, so a test
  // never reads or writes the real one under Documents.
  static inline juce::File defaultRootOverride;
  static constexpr const char* kOwnerPref = "t3k.libraryOwner";
  static constexpr const char* kLinksPref = "t3k.libraryLinks";          // JSON array of paths
  static constexpr const char* kFavoritesPref = "t3k.libraryFavorites";  // cached site favorites
  // The keep folder's path. (Named before Keep was: kept so a folder set
  // in an older build still is.)
  static constexpr const char* kKeepPref = "t3k.libraryCollect";
  // JSON object: kept copy's path -> the file it was kept from, or the
  // TONE3000 tone and model ({ tone, model, hash }) (see the header). Kept in
  // step with moves and renames made here; one whose file is gone stays
  // until found or forgotten (Missing files).
  static constexpr const char* kKeptPref = "t3k.libraryKept";
  // JSON object: the kept links (copy paths) learnKept worked out from
  // matching bytes, not made by a Keep: they go quietly when a side does.
  static constexpr const char* kLearnedPref = "t3k.libraryLearned";
  // JSON object: missing paths the drawer's notice was hidden for.
  static constexpr const char* kMissingHiddenPref = "t3k.libraryMissingHidden";
  // The Favorites folder's virtual path; its tones are "<this>/<tone id>".
  static constexpr const char* kFavoritesPath = "tone3000:favorites";
  static constexpr const char* kSitePath = "tone3000:library";
  static constexpr const char* kSiteCapturesPath = "tone3000:captures";
  static constexpr const char* kSitePresetsPath = "tone3000:presets";
  // The TONE3000 account whose tones are TONE3000's Captures.
  static constexpr const char* kSiteUsername = "tone3000";
  static constexpr const char* kSiteTonesPref = "t3k.librarySiteTones";  // cached, like favorites
  // JSON array of library paths, top first (Move Up / Down, dragging one
  // library onto another); libraries it doesn't name follow in scan order.
  static constexpr const char* kOrderPref = "t3k.libraryOrder";
  // JSON object: a folder's path (lower case) -> its picture, a copy kept in
  // the folder's library (Set Picture; library state). Blocks loaded from the folder, or a
  // folder inside it, show it instead of a TONE3000 lookup.
  static constexpr const char* kPicturesPref = "t3k.libraryPictures";
  // Folders in your order (dropped between folders in the drawer): the
  // lower-cased path of a folder -> the names of its folders, in order.
  // Folders a list doesn't name follow, as listed (natural order). Items
  // (captures, presets) keep their own order.
  static constexpr const char* kFolderOrderPref = "t3k.libraryFolderOrder";
  // A Library row's drag description: { t3kLibraryPath: <node path> }. The
  // drawer's rows start these; gallery tiles and folder rows accept them.
  static constexpr const char* kDragKey = "t3kLibraryPath";

  LibraryStore(Backend& backend, ChainStore& chain, PresetStore& presets, ToneSession& session,
               ConnectionGate& connection, UiPrefs& prefs, Toast& toast);
  ~LibraryStore() override;

  // The first listener triggers a scan.
  void addListener(Listener* l);
  void removeListener(Listener* l) { listeners_.remove(l); }

  const LibraryTree& tree() const { return *tree_; }
  // Rescan in the background; listeners hear when it lands. Folders that
  // haven't changed reuse their listing unless `fresh`.
  void refresh(bool fresh = false);
  // Nothing scanned yet (the very first open): the drawer says so.
  bool loading() const { return !loaded_; }
  // A full rescan (every folder listed again) plus a fresh fetch of the
  // site favorites: the drawer's Refresh.
  void reload();
  // Your Captures and Presets halves (nullptr before the first scan).
  const LibraryNode* capturesRoot() const;
  const LibraryNode* presetsRoot() const;
  // The section of yours `item` belongs in (captures or presets), listed or
  // not: where a drop on your library, or its Add here, lands.
  juce::String halfFor(const LibraryNode& item) const;
  juce::File root() const;
  void adoptUsername();
  juce::String owner() const;

  // View state, kept while the editor lives so the drawer reopens as left.
  bool isOpen(const juce::String& path) const { return open_.count(path) != 0; }
  void setOpen(const juce::String& path, bool open);
  // The view (open folders, selection, search, scroll, shown) is this
  // instance's: kept with the project, restored when the editor opens.
  void saveView();
  void setScroll(int y);
  int savedScroll() const { return scroll_; }
  // A scroll is saved once it settles, not per pixel.
  DelayedCall scrollSave_;
  void setShown(bool shown);
  bool savedShown() const { return drawerShown_; }
  const std::set<juce::String>& openPaths() const { return open_; }
  const juce::String& selected() const { return selected_; }
  // Silent: a press selects, and a notify would rebuild the drawer's rows
  // under the row handling it. The drawer repaints for itself.
  void select(const juce::String& path) {
    selected_ = path;
    saveView();
  }
  // Show `path` in the drawer: the folders above it opened, it selected and
  // scrolled into view (a search that would hide it is cleared). The drawer
  // takes the request on its next change.
  void focus(const juce::String& path);
  // "Show in Library" (a tile's menu, a card's header): the drawer opens on
  // the file the block plays, once the tree has it. canShow: it plays one.
  bool canShow(const std::string& blockId) { return playingSource(blockId).isNotEmpty(); }
  void showBlock(const std::string& blockId);
  // The file a block plays ("" for none), and the block last loaded from
  // the Library (the one being auditioned).
  juce::String playingFile(const std::string& blockId) { return playingSource(blockId); }
  const std::string& auditionBlockId() const { return auditionBlock_; }
  // Select `path` and scroll to it, quietly (the audition block stepped to
  // another capture): no toast, and a search that hides it stays.
  void follow(const juce::String& path);
  juce::String takeFocus() { return std::exchange(focus_, {}); }

  // Library order: one place up (-1) or down (+1), or to just before another.
  void moveLibrary(const juce::String& path, int by);
  void moveLibraryBefore(const juce::String& path, const juce::String& beforePath);
  bool canMoveLibrary(const juce::String& path, int by) const;

  // Folder pictures (kPicturesPref).
  juce::File pictureFor(const juce::String& folderPath) const;
  void choosePicture(const juce::String& folderPath);
  void setPicture(const juce::String& folderPath, const juce::File& image);
  void removePicture(const juce::String& folderPath);
  juce::String filter;
  // The drawer's gear filter (LibraryNode::gearKind ids; empty = all).
  std::set<juce::String> gearFilter;
  // While filtering, containers the user closed, and matching folders the
  // user opened (only the first shows open by itself); cleared with the
  // filter.
  std::set<juce::String> closedInFilter, openedInFilter;

  // Use an item. A preset loads (the whole rig). A tone reference or a
  // capture lands on `targetBlockId` the way a browser pick does (an insert
  // slot adds, a tone block swaps in place). With no target it auditions:
  // it replaces the block whose card is open, else the block the Library
  // last loaded into, else lands at the end of the active lane, so trying
  // captures one after another swaps one amp instead of stacking amps.
  // A captures folder loads as one block whose models are its files (the
  // tiles' Load Folder). Other folders do nothing.
  void use(const LibraryNode& node, const std::string& targetBlockId = {});
  // A tone or capture as a new block at the end of the active lane.
  void addAsNewBlock(const LibraryNode& node);
  // Runs before a preset load (PluginRoot leaves the tuner / browser).
  std::function<void()> beforePresetLoad;
  // An item used with no target, its part played by more than one block
  // and none pointed at (no open card, nothing loaded into that part yet):
  // asks which, then `pick` with that block ("" for a new one). The drawer
  // asks with a menu; with nobody to ask, the last such block is used.
  std::function<void(const std::vector<std::string>& blocks, std::function<void(const std::string&)> pick)> chooseBlock;

  // Edits; each reports failure as a toast and refreshes on success.
  juce::String createFolder(const juce::String& parentPath, const juce::String& name);
  bool rename(const juce::String& path, const juce::String& name);
  bool remove(const juce::String& path);
  // Drag-and-drop between folders: yours move, anyone else's copy in (and a
  // tone dropped on Favorites is favorited). Kinds must fit (LibraryNode::accepts).
  bool moveOrCopy(const juce::String& path, const juce::String& folderPath);
  // One of your captures (or tones) into a new folder beside it, named as it
  // is: the folder a capture's other takes can join. The folder opens with
  // the capture selected.
  bool putInOwnFolder(const juce::String& path);
  bool copyToMine(const juce::String& path, const juce::String& folderPath);
  // Save the current rig as a preset in a folder (it becomes active).
  bool saveRig(const juce::String& folderPath, const juce::String& name);
  // A chain block into a captures folder: catalog tones as a reference to
  // the active model, local ones as a copy of the active capture file;
  // into Favorites, favorited on the site (catalog tones only).
  bool addBlock(const std::string& blockId, const juce::String& folderPath,
                const juce::String& toastVerb = "Added to");
  // Favorite / unfavorite a catalog tone on TONE3000 (needs a sign-in).
  void setFavorite(const LibraryToneRef& ref, bool favorite);

  // Keeping (see the header). A separate listener from the tree's: block
  // cards follow the target without subscribing to (and so triggering) scans.
  struct KeepListener {
    virtual ~KeepListener() = default;
    // The keep folder, or the kept links, changed.
    virtual void keepChanged() = 0;
  };
  void addKeepListener(KeepListener* l) { keepListeners_.add(l); }
  void removeKeepListener(KeepListener* l) { keepListeners_.remove(l); }
  // The keep folder's path ("" when none is set, or when it is gone).
  juce::String keepTarget() const;
  juce::String keepTargetName() const { return juce::File(keepTarget()).getFileName(); }
  void setKeepTarget(const juce::String& folderPath);
  void stopKeeping() { setKeepTarget({}); }
  // Copy the model `blockId` is playing into the target: a local capture's
  // file, or a TONE3000 tone's capture (the model it plays, as a file of its
  // own, linked to the tone: Source loads the tone again, with all its
  // models). keepInto is the same for any folder.
  bool keep(const std::string& blockId);
  bool keepInto(const std::string& blockId, const juce::String& folderPath);
  // The ways to keep a block (KEEP and its menu), and "Add to Library":
  //   keep      the capture it plays (keepInto)
  //   keepLink  a TONE3000 tone as a link (a .t3ktone: one item holding all
  //             its captures, played from TONE3000)
  //   download  every capture of a TONE3000 tone, as files in a folder named
  //             after it (each linked to its model; one download at a time)
  //   add       a tile's Add to Library (a TONE3000 tone as its link)
  enum class AddKind { add, keep, keepLink, download };
  // Into the keep folder, or with none, ask for a folder (beginAdd).
  void keepAs(const std::string& blockId, AddKind kind);
  void downloadInto(const std::string& blockId, const juce::String& folderPath);
  void keepLinkInto(const std::string& blockId, const juce::String& folderPath);
  juce::var siteRefForBlock(const std::string& blockId);
  bool downloading() const { return downloading_; }
  void remapKeptForTesting(const juce::String& from, const juce::String& to) { remapKept(from, to); }
  void remapPathsForTesting(const juce::String& from, const juce::String& to) { remapPaths(from, to); }
  void forgetPathsForTesting(const juce::String& path) { forgetPaths(path); }
  void rememberKeptForTesting(const juce::File& copy, const juce::var& source) { rememberKept(copy, source); }

  // Library state (LibraryState.h): the kept links, folder pictures, keep
  // folder, library order and linked folders, mirrored into a file in each
  // library folder a moment after they change (saveState: now), and merged
  // back from one that is new or changed since this instance last looked: a
  // restored backup, a Library moved or copied elsewhere, another machine's
  // edits to a synced folder. The prefs stay the working copy.
  // `reread`: read changed files first (not when closing: write only).
  void saveState(bool reread = true);
  bool loadStateForTesting() { return loadState(); }

  // Missing links: kept links (copy or original), folder pictures and the
  // keep folder naming a path in the Library or a linked folder that isn't
  // there, on a drive that is (moved or renamed outside the plugin). Worked
  // out when a scan lands.
  struct Missing {
    enum class Kind { copy, original, picture, keepFolder };
    juce::String path;  // as linked (a picture's folder: lower case)
    Kind kind;
    juce::String other;  // a copy: what it was kept from; an original: its kept copy
    bool site = false;   // a copy kept from a TONE3000 tone (downloadMissing brings it back)
  };
  const std::vector<Missing>& missing() const { return missing_; }
  // The drawer's notice: copies you kept and the keep folder gone (an
  // original or a picture's folder gone shows in the list only), not ones
  // hidden (Hide: remembered until they are found or forgotten).
  int missingNoticeCount() const;
  bool missingNoticeShown() const { return missingNoticeCount() > 0; }
  void hideMissingNotice();
  // Ask for the folder they went to, then findMissing: named for `path`
  // and opening where it was (the nearest folder still there), or for all.
  void findMissingDialog(const juce::String& path = {});
  // Relink each missing path found under `folder`: the longest end of its
  // path that is there, `folder` standing in for a folder of its name (pick
  // the moved folder itself, or the one it went into). How many it found.
  // A match by the file name alone is trusted for `asked` (the one picked
  // in the list), or for several from one folder turning up together.
  int findMissing(const juce::File& folder, const juce::String& asked = {});
  // Drop the missing links (their files were deleted on purpose).
  void forgetMissing();
  // Missing copies kept from TONE3000 tones (Missing::site), downloaded
  // again where they were (their folder made again if it went too), one at
  // a time; `paths` empty: all of them.
  void downloadMissing(const juce::StringArray& paths = {});
  void checkMissingForTesting() { checkMissing(); }
  // A/B to another tone: what a block played before (ChainStore::previous)
  // loaded into it again: a local capture from its file (with its folder),
  // a TONE3000 tone from TONE3000 on that model.
  void playAgain(const std::string& blockId, const ChainItem& before);
  // A/B for a block: back to what it played before (another capture of its
  // folder switched; anything else loaded again). False: nothing to go to.
  bool abSwitch(const std::string& blockId);
  // The block the Library's A/B and loads are for: the one it last loaded
  // into, else the one whose card is open.
  std::string abBlock() const;
  std::string auditionTargetForTesting(const juce::String& format, const juce::String& gear = {}) const {
    return auditionTarget(format, gear);
  }
  std::vector<std::string> auditionChoicesForTesting(const juce::String& format, const juce::String& gear) const {
    return auditionChoices(format, gear);
  }
  void forgetAuditionForTesting() {
    auditionBlock_.clear();
    auditionByKind_.clear();
  }

  // Kept links (see the header), for the capture `blockId` is playing: the
  // file it was kept from, if it is a kept copy (and the original is still
  // there), and the copies of it kept into the Library.
  // Without a recorded link (a copy kept before links existed, or from a
  // block loaded before it knew its file), a Library capture with the same
  // file name and the same bytes counts: in a linked folder it is the
  // original of one in your own folders, and the other way round.
  juce::File keptFrom(const std::string& blockId);
  juce::Array<juce::File> keptCopies(const std::string& blockId);
  // The same for a capture file (a Library row).
  juce::File originalOf(const juce::String& capturePath);
  // originalOf without reading any file: a recorded link, or a same-bytes
  // match already worked out (for a block). What a drawer row asks while
  // scrolling.
  juce::File knownOriginalOf(const juce::String& capturePath);
  juce::Array<juce::File> copiesOf(const juce::String& capturePath);
  // A capture kept from a TONE3000 tone: the tone and model it came from.
  std::optional<LibraryToneRef> siteOriginalOf(const juce::String& capturePath) const;
  // Whether the block plays a kept copy (of a file or of a TONE3000 tone).
  bool hasOriginal(const std::string& blockId);
  // The TONE3000 tone (and model) the capture `blockId` plays was kept from.
  std::optional<LibraryToneRef> siteOriginalForBlock(const std::string& blockId) {
    return siteOriginalOf(playingSource(blockId));
  }
  // Folder order: `dragged` can go just before / after `sibling` (two
  // folders of one folder; a library's sections keep their places).
  bool canPlaceBeside(const juce::String& dragged, const juce::String& sibling) const;
  void placeFolder(const juce::String& dragged, const juce::String& sibling, bool after);
  // REFRESH (a block card): captures of the kind the block plays in its
  // folder that it doesn't list (kept, dropped or copied in since it
  // loaded its folder); 0 for none. Cached by the folder's date.
  int newInFolder(const std::string& blockId);
  // The block's folder loaded into it again, on the capture it plays.
  void refreshBlock(const std::string& blockId);
  // A Library row's TONE3000 original, loaded the way a pick is (from your
  // own copy of it when you have one: localSiteOriginal).
  void useSiteOriginal(const juce::String& capturePath);
  // A capture kept from a TONE3000 tone: the same model of that tone as a
  // file of yours elsewhere (Download All Captures, or a copy kept before),
  // by its link to the tone and model, else by the same bytes; of several,
  // the one whose folder holds the most of that tone. Invalid with none.
  // What SOURCE loads before going to TONE3000: no download, and the folder
  // steps without lag.
  juce::File localSiteOriginal(const juce::String& capturePath) const;
  // Swap the block to the original's folder / a kept copy's folder, starting
  // on that capture.
  void openOriginal(const std::string& blockId);
  void openKept(const std::string& blockId, const juce::File& copy);
  // The folder whose picture a block shows: the playing capture's (its
  // original's, for a kept copy); "" for a block not from a file.
  juce::String pictureFolderFor(const std::string& blockId);

  // "Add to Library" from a tile: park the block and ask for the drawer; the
  // next folder picked there takes it.
  void beginAdd(const std::string& blockId, AddKind kind = AddKind::add);
  AddKind pendingKind() const { return pendingKind_; }
  // The folder picked for it.
  void finishAdd(const juce::String& folderPath);
  void cancelAdd();
  const std::optional<std::string>& pendingAdd() const { return pendingAdd_; }
  std::function<void()> onRequestShow;

  // Link a folder from elsewhere into your library (nothing is copied), or
  // forget a link (nothing is deleted). linkFolder() picks one in the OS
  // dialog.
  bool link(const juce::File& dir);
  bool unlink(const juce::String& path);
  void linkFolder();
  juce::Array<juce::File> links() const;
  // Or copy one in instead: its captures and references, subfolders kept,
  // into a captures folder (your Captures by default). importFolderDialog()
  // picks the folder in the OS dialog.
  bool importFolder(const juce::File& dir, const juce::String& folderPath = {});
  void importFolderDialog(const juce::String& folderPath = {});

  // OS files dropped on a folder: captures, presets and references are
  // copied in, .t3klibrary archives imported. With no folder, each file goes
  // to the half its kind belongs in. (Dropped folders are the drawer's to
  // ask about: link or copy.)
  void addFiles(const juce::StringArray& paths, const juce::String& folderPath);

  // Files (OS dialogs; one at a time).
  // A backup: everything, as it is (export).
  void exportItem(const juce::String& path);
  // To share (library.md "Sharing"): asks where, then works out which
  // captures are from TONE3000 (kept from a tone, or a folder matched to one:
  // ToneArt, asking the site for folders never asked, one a second) and
  // exports those as links, leaving every other capture home and emptying
  // the preset blocks that play one.
  void shareItem(const juce::String& path);
  void shareItemTo(const juce::String& path, const juce::File& archive);
  bool sharing() const { return sharing_; }
  // What an Export for Sharing is doing while it asks TONE3000 about folders
  // ("" otherwise; onShareStatus hears each change), and stopping it.
  const juce::String& shareStatus() const { return shareStatus_; }
  std::function<void()> onShareStatus;
  void cancelShare();
  void importArchive();
  void importFile(const juce::File& archive);
  void chooseRoot();
  void reveal(const juce::String& path);
  static bool canReveal();

private:
  void presetsChanged(const std::vector<PresetInfo>&) override;
  void prefChanged(const juce::String& key) override;  // the state's prefs: saved once they settle
  // Merge the library state files that changed since last read into the
  // prefs (theirs win); true when that changed anything.
  bool loadState();
  // The library folders that hold state: the Library's, yours first.
  juce::Array<juce::File> stateLibraries() const;
  // Where something at `path` keeps its state: the library holding it, or
  // yours for a linked folder or the Presets (an empty file: nowhere).
  juce::File stateHome(const juce::String& path, bool lowerCased) const;
  juce::File stateHome(const juce::String& path, bool lowerCased, const juce::Array<juce::File>& libraries,
                       const juce::Array<juce::File>& linked) const;
  // Another folder's picture is this file too (never deleted then).
  bool pictureInUse(const juce::File& picture, const juce::String& exceptKey) const;
  void checkMissing();
  std::vector<Missing> missing_;
  std::shared_ptr<void> missingStep_;  // downloadMissing's chain while it runs

  // Old path -> new, exactly, in the kept links (copies and originals), the
  // pictures and the keep folder.
  void relink(const std::map<juce::String, juce::String>& moved);
  // Drop these copies' kept links (and their learned marks).
  void forgetLinks(const juce::StringArray& copies);
  std::map<juce::String, juce::int64> stateSeen_;  // library path -> its file's stamp when last read or written
  // library path -> what its file held when last read or written: an entry
  // gone from the file since was deleted elsewhere (a 3-way merge).
  std::map<juce::String, juce::var> stateSnapshot_;
  // library path -> its file's entries left unread ({ kept, pictures }: another
  // owner's paths, a picture not synced yet), written back as they were.
  std::map<juce::String, juce::var> stateSkipped_;
  DelayedCall stateSave_;
  bool loadingState_ = false;
  void sessionChanged() override;
  // A scan landed (the newest one only), built into a tree on the worker.
  void apply(std::shared_ptr<LibraryTree> tree);
  // Put the cached Favorites folder at the top of your Captures, replacing
  // the one there: a favorites change needs no rescan.
  void injectFavorites();
  // What arrange needs from the message thread (prefs, the preset list).
  struct Arrangement {
    juce::var favorites, siteTones, factory, order;
    juce::var folderOrder;  // kFolderOrderPref
    juce::var art;  // ToneArt's cache: folders matched to TONE3000 tones (their gear)
  };
  Arrangement arrangement() const;
  // After every scan: every library as a user's, its sections Captures,
  // Favorites, Presets and (yours) Local, each listed only when it has
  // something in it. TONE3000 is a user too: the TONE3000 account's tones
  // and the factory presets. Your favorites and linked folders move into
  // their sections of yours; the libraries go in your order. Idempotent (it
  // undoes its last pass first). The caller reindexes.
  static void arrange(LibraryTree& tree, const Arrangement& with);

  void fetchFavorites();
  // Pages of a tone search as references (favorites, the TONE3000 account's
  // tones), then `done` with them all (nothing on a failure: keep the cache).
  void fetchRefsPage(ToneQuery query, int page, std::shared_ptr<juce::Array<juce::var>> collected,
                     std::function<void(const juce::Array<juce::var>*)> done);
  void fetchSiteTones();
  // Where an item with no target lands: a tone block playing the same part
  // of the rig (amp, pedal, cab...; by format, "nam" / "ir", when a gear
  // isn't known; "" any) whose card is open, else the one the Library last
  // loaded that part into, else a new block.
  std::string auditionTarget(const juce::String& format = {}, const juce::String& gear = {}) const;
  // The blocks to ask between (two or more of the part, none pointed at), or none.
  std::vector<std::string> auditionChoices(const juce::String& format, const juce::String& gear) const;
  void auditioning(const std::string& blockId);
  std::map<juce::String, std::string> auditionByKind_;  // part (or format) -> the block it last went into
  // Work under way, up until its result (a note or a fail) replaces it.
  void progress(const juce::String& message);
  // Load a capture (with the captures beside it, starting on it) or a
  // folder of them, and remember the block it landed in.
  // `again`: the folder read afresh even when the block has the capture
  // (REFRESH), not just a switch to it.
  void loadCapture(const juce::File& file, const std::string& target, bool again = false);
  // Put the TONE3000 artwork of the capture's folder (ToneArt) on the block.
  void artFor(const juce::File& file, const std::string& blockId);
  juce::var refForBlock(const ChainItem& item) const;
  void pushLocation();
  void notify();
  bool fail(const juce::String& message);
  // Re-dress the blocks playing captures from inside `folder` (its picture
  // changed).
  void refreshArtUnder(const juce::File& folder);
  void applyOrder(const juce::StringArray& order);
  // ChainStore: a block playing a kept copy shows its original's folder.
  void chainChanged(const ChainState& state) override;
  // A block playing a kept copy takes its original's folder name as its
  // title and that folder's TONE3000 artwork (a keep folder names no tone),
  // following the model picker; back on a capture without one, its own
  // title returns. blockId -> the folder shown, the title we set, and the
  // block's own.
  struct Shown {
    juce::String folder, title, ownTitle;
    juce::String models;  // the block's model ids when we set it (a new tone resets)
  };
  std::map<std::string, Shown> shown_;
  juce::String focus_;
  int scroll_ = 0;
  bool drawerShown_ = false, viewRestored_ = false;
  void restoreView();
  // Bumped whenever arrange's inputs change (favorites, the TONE3000
  // account's tones, the order): a scan that started before is arranged
  // again when it lands, so it never brings back the old sections.
  int arrangementVersion_ = 0;
  void syncShown();
  void syncShownOnce();
  // Forget the look given to a block (its tone was replaced): the next pass
  // dresses it from scratch.
  void forgetDressing(const std::string& blockId);
  bool syncingShown_ = false, resyncShown_ = false;
  // The last artwork asked for each block: a reply for anything older is
  // dropped (the picker moved on).
  std::map<std::string, juce::String> artWanted_;
  // Library captures with `path`'s file name and the same bytes, cached per
  // tree (cleared when a scan lands).
  juce::Array<juce::File> sameCaptures(const juce::String& path);
  std::map<juce::String, juce::Array<juce::File>> same_;
  struct FolderFiles {
    juce::int64 stamp = -1;  // the folder's date when listed
    juce::String extension;
    juce::StringArray names;  // lower-cased file names of that kind
  };
  std::map<juce::String, FolderFiles> folderFiles_;
  // The original of a stash copy the block plays without knowing its file.
  juce::File originalInLibrary(const juce::File& stash, const juce::String& name);
  // Blocks playing files that aren't at their paths any more (a project
  // saved before a folder was renamed, a rename in Explorer): each looked
  // for in the Library by its name, a find counting only when its bytes give
  // the block's model id, and the block re-pointed there
  // (Backend::relinkLocalFiles). Once per missing path per listing.
  void findMovedFiles();
  std::set<juce::String> movedChecked_;
  // A confirmation (the quiet toast): fail() stays the loud one.
  void note(const juce::String& message);
  // The source_path of the capture `blockId` plays ("" for none).
  juce::String playingSource(const std::string& blockId);
  // Blocks without a recorded file: stash path|name -> the Library file with
  // those bytes ("" none), per tree.
  std::map<juce::String, juce::String> stashSources_;
  // blockId -> the folder whose look it was last given (the dressing pass).
  std::map<std::string, juce::String> dressed_;
  // Blocks a TONE3000 match named (their capture's own name doesn't apply).
  std::set<std::string> artTitled_;
  // `source`: the original's path, or a TONE3000 reference (a tone and
  // model: { tone, model }, as refForBlock gives).
  void rememberKept(const juce::File& copy, const juce::var& source);
  // A same-bytes match (sameCaptures) recorded as a kept link, unless the
  // copy has one.
  void learnKept(const juce::File& copy, const juce::File& original);
  DelayedCall learnedNotify_;
  // The kept copy landed: linked, shown, said.
  void keptLanded(const juce::File& copy, const juce::var& source, const juce::String& folderPath);
  bool downloading_ = false;
  bool sharing_ = false;
  // Paths a move / delete is under way for (not started twice).
  std::set<juce::String> busy_;
  // An Export for Sharing under way (shareItemTo).
  struct ShareRun {
    juce::DynamicObject::Ptr refs = new juce::DynamicObject();  // capture path -> its TONE3000 { tone, model }
    juce::File item, archive;
    juce::String name;
    bool signedOut = false;
    int unchecked = 0;  // folders not asked (no creator named, signed out, past the cap)
    std::map<juce::String, juce::StringArray> byFolder;  // captures without a ref yet, by folder
    juce::Array<juce::File> stops;  // never looked up: the Library's own folders, above what is shared
    struct Ask {
      juce::String folder, creator;
      juce::Array<juce::File> candidates;
    };
    std::vector<Ask> toAsk;  // folders to ask TONE3000 about, one at a time
    size_t asked = 0;
    std::function<void()> next;
    std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
  };
  static constexpr size_t kMaxShareLookups = 300;  // a few minutes at a second each
  std::shared_ptr<ShareRun> shareRun_;
  juce::String shareStatus_;
  void setShareStatus(const juce::String& status);
  std::shared_ptr<void> downloadStep_;
  // A move / rename here: links to and from `from` (or inside it) follow.
  void remapKept(const juce::String& from, const juce::String& to);
  // Everything kept by path, for a move / rename (kept links, the keep
  // folder, pictures, the artwork cache, the library order, open folders)
  // and a delete (pictures and their files, artwork, kept links, open).
  void remapPaths(const juce::String& from, const juce::String& to);
  void forgetPaths(const juce::String& path);
  // The active lane's last insert slot: where a double-clicked item goes.
  std::string appendSlot() const;
  void loadToneRef(const LibraryToneRef& ref, const std::string& targetBlockId);
  void launch(std::unique_ptr<juce::FileChooser> chooser, int flags,
              std::function<void(const juce::FileChooser&)> done);

  Backend& backend_;
  ChainStore& chain_;
  PresetStore& presets_;
  ToneSession& session_;
  ConnectionGate& connection_;
  UiPrefs& prefs_;
  Toast& toast_;
  // Built whole on the scan worker and swapped in: a big collection's tree
  // (tens of thousands of nodes) never parses on the message thread.
  std::shared_ptr<LibraryTree> tree_ = std::make_shared<LibraryTree>();
  ToneArt art_;
  // Blocks loaded while signed out, waiting for artwork: looked up on sign-in.
  std::map<std::string, juce::File> artAfterSignIn_;
  std::set<juce::String> open_;
  juce::String selected_;
  std::optional<std::string> pendingAdd_;
  AddKind pendingKind_ = AddKind::add;
  // The block the Library last loaded a tone or capture into (audition).
  std::string auditionBlock_;
  juce::String pendingReveal_;  // showBlock before the tree had it
  bool favoritesFetched_ = false, favoritesLoading_ = false, siteTonesLoading_ = false;
  std::unique_ptr<juce::FileChooser> chooser_;
  juce::ListenerList<Listener> listeners_;
  juce::ListenerList<KeepListener> keepListeners_;
  bool loaded_ = false;
  std::atomic<int> scanGeneration_{0};
  // Scans: one waiting at a time (it scans what is current when it starts;
  // a fresh walk if any refresh asked for one), and a stop the destructor
  // raises so it never waits out a walk of a big collection.
  std::shared_ptr<std::atomic<bool>> scanQueued_ = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<bool>> freshWanted_ = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<bool>> scanStop_ = std::make_shared<std::atomic<bool>>(false);
  AsyncScope scope_;
  // Last: destroyed first, so a scan in flight finishes before the rest goes.
  juce::ThreadPool scanPool_{1};
};

}  // namespace t3k::ui
