// The Library tree as the drawer reads it (Backend::getLibrary, format in
// plugin/docs/library.md), and the one piece of logic over it worth testing
// on its own: which rows show for a set of open folders and a filter.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <set>
#include <vector>

namespace t3k::ui {

// A .t3ktone: a pointer at one model of a TONE3000 catalog tone. `raw` is
// the reference as stored, the shape a new one is written in.
struct LibraryToneRef {
  int toneId = 0;
  int modelId = 0;
  juce::String title;
  juce::String modelName;
  juce::String gear;
  juce::String format;
  juce::String creator;
  juce::var raw;

  static LibraryToneRef parse(const juce::var& v);
};

struct LibraryNode {
  enum class Kind { library, folder, preset, tone, capture };

  Kind kind = Kind::folder;
  juce::String name;
  // Identity: absolute, unique, stable across scans (until the file moves).
  juce::String path;
  bool mine = false;       // library: yours
  bool mount = false;      // folder: a fixed root (Captures, Presets, Favorites, a link)
  // Folders: "captures" or "presets" (the library half it is in), "" for
  // an untyped one. Each half lists and takes only its kind.
  juce::String type;
  // Favorites: the folder mirroring your TONE3000 favorites, and the tones
  // in it (virtual paths under "tone3000:favorites"; no files behind them).
  bool favorites = false;
  bool linked = false;     // folder: linked in from elsewhere (unlink = forget the link)
  bool local = false;      // folder: your library's Local (the linked folders list in it)
  // library: TONE3000's, as a user's (the TONE3000 account's captures, the
  // factory presets), put together by the Library store; no folder on disk.
  bool site = false;
  // An item with no file behind it (a TONE3000 account tone, a factory
  // preset) or a section that is a view (TONE3000's, your Local).
  bool remote = false;
  bool missing = false;    // linked folder: not found (an unplugged drive)
  bool editable = false;   // rename / move
  bool removable = false;  // delete (an imported library: "unfollow")
  bool writable = false;   // containers: new items may go in
  juce::String presetId;   // preset
  LibraryToneRef tone;     // tone
  bool nam = true;         // capture: .nam (else an IR .wav)
  juce::String gear;       // capture: its folder's gear, best effort ("" unknown)
  // The catalog gear id this is (a tone's, a capture's folder's), "preset"
  // for a preset, "" for anything else or unknown.
  juce::String gearKind() const;
  std::vector<LibraryNode> children;

  bool isContainer() const { return kind == Kind::library || kind == Kind::folder; }
  bool isCaptures() const { return type == "captures"; }
  bool isPresets() const { return type == "presets"; }
  // A file on disk behind it (not a Favorites entry).
  bool onDisk() const { return !favorites && !site && !remote; }
  // A captures folder can load as one block switching between its files.
  bool loadsAsBlock() const { return kind == Kind::folder && isCaptures() && !favorites && !missing; }
  // `item` may go in here by kind (a writable container's check is apart).
  bool accepts(const LibraryNode& item) const;
  juce::File file() const { return juce::File(path); }

  static LibraryNode parse(const juce::var& v);
};

struct LibraryTree {
  juce::String root;
  juce::String owner;
  // The scan stopped at its node budget (a root pointed somewhere huge).
  bool truncated = false;
  // Yours first, then imported libraries.
  std::vector<LibraryNode> libraries;
  // Sections not listed while they are empty (your Captures before the
  // first folder, say), still found by path: actions that land in them work.
  struct Hidden {
    LibraryNode node;
    juce::String parent;  // the library it belongs to
  };
  std::vector<Hidden> hidden;

  const LibraryNode* find(const juce::String& path) const;
  // The container holding `path`, or nullptr for a library (or unknown).
  const LibraryNode* parentOf(const juce::String& path) const;
  // Your library (the one on disk, not TONE3000's), wherever it is listed.
  const LibraryNode* mine() const;
  // Captures with this file name (case-insensitive), wherever they are.
  std::vector<const LibraryNode*> capturesNamed(const juce::String& fileName) const;
  // Inside a folder linked in from elsewhere (a collection), not your own.
  bool inLinked(const juce::String& path) const;

  // find / parentOf by path in log time (a linked collection can hold tens
  // of thousands of nodes, and drags ask on every move). Rebuilt by parse;
  // anyone who then edits the node vectors (or copies the tree) reindexes.
  void reindex();

  // `index`: false when the caller edits the tree and reindexes after.
  static LibraryTree parse(const juce::var& v, bool index = true);

private:
  void indexNode(const LibraryNode& node, const LibraryNode* parent);
  std::map<juce::String, const LibraryNode*> byPath_;
  std::map<juce::String, const LibraryNode*> parentByPath_;
  std::multimap<juce::String, const LibraryNode*> capturesByName_;
};

struct LibraryRow {
  const LibraryNode* node = nullptr;
  int depth = 0;
  bool open = false;
};

// The rows to show, in order. Without a filter: every library, and the
// children of each open container. With one (case-insensitive; every
// whitespace-separated word must appear in the name): items whose names
// match, inside their containers, and containers whose names match, with
// everything in them, so searching "Metal" brings up the whole Metal folder
// and "BE100" the preset wherever it lives. Filtered rows show open, but of
// the folders whose names match only the first: the others show closed (a
// search for "Bogner" doesn't spill hundreds of captures), until opened.
// `closedInFilter`: containers the user closed while filtering (shown, but
// not their contents), so a search never leaves folders stuck open;
// `openedInFilter`: matching folders the user opened.
// `gears`: only items of these gear kinds (gearKind(); "preset" for
// presets), with the containers holding them, open; none = any.
std::vector<LibraryRow> libraryRows(const LibraryTree& tree, const std::set<juce::String>& open,
                                    const juce::String& filter,
                                    const std::set<juce::String>& closedInFilter = {},
                                    const std::set<juce::String>& gears = {},
                                    const std::set<juce::String>& openedInFilter = {});

bool libraryNameMatches(const juce::String& name, const juce::String& filter);

}  // namespace t3k::ui
