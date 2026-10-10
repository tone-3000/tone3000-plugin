#include "LibraryStore.h"

#include <string_view>

#include <functional>

#include "LibraryState.h"
#include "PictureFile.h"
#include "model/Tone.h"
#include "model/ToneQuery.h"

#include <algorithm>

namespace t3k::ui {

namespace {

// Library.h's LocalLibrary::defaultRoot / kDefaultOwner, which the UI can't
// include (the testbed builds without the processor sources).
juce::File defaultRoot() {
  if (LibraryStore::defaultRootOverride != juce::File()) return LibraryStore::defaultRootOverride;
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
      .getChildFile("TONE3000")
      .getChildFile("Library");
}
constexpr const char* kDefaultOwner = "My Library";

#if JUCE_IOS
constexpr bool kDesktopFiles = false;
#else
constexpr bool kDesktopFiles = true;
#endif

bool isFavoritesPath(const juce::String& path) { return path.startsWith(LibraryStore::kFavoritesPath); }

juce::String displayName(const LibraryTree& tree, const juce::String& path) {
  if (const auto* node = tree.find(path)) return node->name;
  return juce::File(path).getFileName();
}

}  // namespace

namespace {

// A gear tag at the front of a file name, as TONE3000 downloads are named
// ("[AMP] Plexi - Crunch", "[PEDAL] Klon"); "" when there is none.
juce::String gearFromNameTag(const juce::String& fileName) {
  const auto name = fileName.trimStart();
  if (!name.startsWithChar('[')) return {};
  const auto tag = name.substring(1).upToFirstOccurrenceOf("]", false, false).trim().toUpperCase();
  if (tag == "AMP" || tag == "AMP HEAD" || tag == "PREAMP" || tag == "HEAD") return "amp";
  if (tag == "AMP+CAB" || tag == "AMP + CAB" || tag == "AMP & CAB" || tag == "FULL RIG" || tag == "FULLRIG" ||
      tag == "COMBO")
    return "amp-cab";
  if (tag == "PEDAL" || tag == "DRIVE" || tag == "OD" || tag == "DIST" || tag == "FUZZ" || tag == "BOOST") return "pedal";
  if (tag == "CAB" || tag == "IR" || tag == "CABINET") return "cab";
  if (tag == "OUTBOARD" || tag == "STUDIO") return "outboard";
  return {};
}

// What a file's name says its gear is: its tag ("[AMP] …"), else a "DI"
// word ("… - DI", "DI 3"): a direct capture is an amp alone, whatever the
// trainer's metadata guessed (often amp + cab). "" when it says nothing.
juce::String gearFromName(const juce::String& fileName) {
  if (const auto tagged = gearFromNameTag(fileName); tagged.isNotEmpty()) return tagged;
  const auto stem = fileName.upToLastOccurrenceOf(".", false, false);
  for (const auto& word : juce::StringArray::fromTokens(stem, " -_()[].", ""))
    if (word == "DI") return "amp";
  return {};
}

// A folder name that says nothing about what's in it ("DI", "NAM",
// "Captures"): a block from it is better named after its capture.
bool genericFolderName(const juce::String& name) {
  static const juce::StringArray generic{"di",     "nam",     "nams",  "captures", "capture", "models",  "model",
                                         "amps",   "amp",     "cabs",  "cab",      "irs",     "ir",      "pedals",
                                         "pedal",  "files",   "misc",  "new",      "other",   "sorted",  "downloads",
                                         "export", "exports", "output", "outputs", "tone3000", "favorites", "reamp"};
  const auto n = name.trim().toLowerCase();
  return n.length() <= 2 || generic.contains(n);
}

LibraryNode section(const juce::String& name, const juce::String& path, const juce::String& type) {
  LibraryNode node;
  node.kind = LibraryNode::Kind::folder;
  node.name = name;
  node.path = path;
  node.type = type;
  node.mount = true;
  return node;
}

// Tone references (favorites, the TONE3000 account's) as tone nodes.
std::vector<LibraryNode> toneNodes(const juce::var& refs, const juce::String& under, bool favorites) {
  std::vector<LibraryNode> out;
  if (const auto* list = refs.getArray())
    for (const auto& raw : *list) {
      LibraryNode tone;
      tone.kind = LibraryNode::Kind::tone;
      tone.tone = LibraryToneRef::parse(raw);
      if (tone.tone.toneId <= 0) continue;
      tone.name = tone.tone.title;
      tone.path = under + "/" + juce::String(tone.tone.toneId);
      tone.favorites = favorites;
      tone.removable = favorites;  // unfavorite
      tone.remote = !favorites;
      out.push_back(std::move(tone));
    }
  return out;
}

bool isDiskHalf(const LibraryNode& n) { return n.mount && !n.linked && !n.remote && !n.favorites; }

// A JSON object's copy to change (a pref's value is shared).
juce::DynamicObject::Ptr copyOfObject(const juce::var& object) {
  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  if (const auto* o = object.getDynamicObject())
    for (const auto& p : o->getProperties()) out->setProperty(p.name, p.value);
  return out;
}

}  // namespace

void LibraryStore::arrange(LibraryTree& tree, const Arrangement& with) {
  // Undo the last pass: hidden sections back in place, the made-up nodes
  // (TONE3000, Favorites) gone.
  for (auto& h : tree.hidden)
    for (auto& library : tree.libraries)
      if (library.path == h.parent) library.children.push_back(std::move(h.node));
  tree.hidden.clear();
  std::erase_if(tree.libraries, [](const LibraryNode& l) { return l.site; });
  for (auto& library : tree.libraries) {
    std::erase_if(library.children, [](const LibraryNode& n) { return n.favorites; });
    for (auto& half : library.children) std::erase_if(half.children, [](const LibraryNode& n) { return n.favorites; });
  }

  // TONE3000, a user: its account's captures, the factory presets.
  LibraryNode site;
  site.kind = LibraryNode::Kind::library;
  site.name = "TONE3000";
  site.path = kSitePath;
  site.mine = true;  // nothing locked about it
  site.site = true;
  auto captures = section("Captures", kSiteCapturesPath, "captures");
  captures.remote = true;
  captures.children = toneNodes(with.siteTones, kSiteCapturesPath, false);
  if (!captures.children.empty()) site.children.push_back(std::move(captures));
  auto presets = section("Presets", kSitePresetsPath, "presets");
  presets.remote = true;
  if (const auto* factory = with.factory.getArray())
    for (const auto& p : *factory) {
      LibraryNode preset;
      preset.kind = LibraryNode::Kind::preset;
      preset.name = p["name"].toString();
      preset.presetId = p["id"].toString();
      preset.path = juce::String(kSitePresetsPath) + "/" + preset.presetId;
      preset.remote = true;
      presets.children.push_back(std::move(preset));
    }
  if (!presets.children.empty()) site.children.push_back(std::move(presets));
  // After yours (the scan lists yours first), before anyone else's.
  const auto yours = std::find_if(tree.libraries.begin(), tree.libraries.end(), [](const LibraryNode& l) { return l.mine; });
  tree.libraries.insert(yours == tree.libraries.end() ? tree.libraries.begin() : std::next(yours), std::move(site));

  for (auto& library : tree.libraries) {
    if (library.site) continue;
    auto& kids = library.children;
    // Yours: your TONE3000 favorites. (Local is a folder on disk for other
    // people's captures: the linked folders are listed in it, Import Folder
    // copies into it.)
    if (library.mine) {
      auto favorites = section("Favorites", kFavoritesPath, "captures");
      favorites.favorites = true;
      favorites.writable = true;  // takes tones: favoriting them
      favorites.children = toneNodes(with.favorites, kFavoritesPath, true);
      if (!favorites.children.empty()) kids.push_back(std::move(favorites));
    }
    // Empty sections aren't listed (found still: see LibraryTree::hidden).
    for (auto it = kids.begin(); it != kids.end();) {
      if (isDiskHalf(*it) && it->children.empty()) {
        tree.hidden.push_back({std::move(*it), library.path});
        it = kids.erase(it);
      } else {
        ++it;
      }
    }
    // Captures, Favorites, Presets, Local; anything else (an untyped folder) after.
    const auto rank = [](const LibraryNode& n) {
      if (n.favorites) return 1;
      if (n.local) return 3;
      if (n.mount && n.isCaptures()) return 0;
      if (n.mount && n.isPresets()) return 2;
      return 4;
    };
    std::stable_sort(kids.begin(), kids.end(), [&](const LibraryNode& a, const LibraryNode& b) { return rank(a) < rank(b); });
  }

  // A capture's gear: its folder's TONE3000 match (that folder or up to two
  // above, as ToneArt looks), over what the file's metadata said.
  std::map<juce::String, juce::String> matched;
  if (auto* cache = with.art.getDynamicObject())
    for (const auto& entry : cache->getProperties())
      if (const auto gear = entry.value.getProperty("gear", "").toString(); gear.isNotEmpty())
        matched[entry.name.toString()] = gear;
  if (!matched.empty()) {
    std::function<void(LibraryNode&, std::vector<juce::String>&)> walk = [&](LibraryNode& node, std::vector<juce::String>& up) {
      if (node.kind == LibraryNode::Kind::capture) {
        for (int i = static_cast<int>(up.size()) - 1, n = 0; i >= 0 && n < 3; --i, ++n)
          if (const auto hit = matched.find(up[static_cast<size_t>(i)]); hit != matched.end()) {
            node.gear = hit->second;
            break;
          }
        return;
      }
      up.push_back(node.path.toLowerCase());
      for (auto& child : node.children) walk(child, up);
      up.pop_back();
    };
    std::vector<juce::String> up;
    for (auto& library : tree.libraries) walk(library, up);
  }

  // Your folder order: a folder's folders in the order stored for it, in the
  // places folders take among its children (a linked folder listed at Local's
  // end stays there); ones it doesn't name after them, as listed. A library's
  // sections keep their fixed order.
  if (const auto* orders = with.folderOrder.getDynamicObject(); orders != nullptr && !orders->getProperties().isEmpty()) {
    std::function<void(LibraryNode&)> walk = [&](LibraryNode& node) {
      if (node.kind != LibraryNode::Kind::library)
        if (const auto* names = orders->getProperty(juce::Identifier(node.path.toLowerCase())).getArray()) {
          std::vector<size_t> slots;
          std::vector<LibraryNode> folders;
          for (size_t i = 0; i < node.children.size(); ++i)
            if (node.children[i].kind == LibraryNode::Kind::folder) {
              slots.push_back(i);
              folders.push_back(std::move(node.children[i]));
            }
          const auto rank = [names](const LibraryNode& f) {
            for (int i = 0; i < names->size(); ++i)
              if ((*names)[i].toString() == f.name) return i;
            return names->size();
          };
          std::stable_sort(folders.begin(), folders.end(),
                           [&](const LibraryNode& a, const LibraryNode& b) { return rank(a) < rank(b); });
          for (size_t k = 0; k < slots.size(); ++k) node.children[slots[k]] = std::move(folders[k]);
        }
      for (auto& child : node.children)
        if (child.isContainer()) walk(child);
    };
    for (auto& library : tree.libraries) walk(library);
  }

  // Your order; the rest keep theirs (yours, TONE3000, imported ones).
  juce::StringArray ordered;
  if (const auto* paths = with.order.getArray())
    for (const auto& p : *paths) ordered.add(p.toString());
  std::stable_sort(tree.libraries.begin(), tree.libraries.end(), [&](const LibraryNode& a, const LibraryNode& b) {
    const auto rank = [&](const LibraryNode& l) {
      const int at = ordered.indexOf(l.path);
      return at < 0 ? ordered.size() : at;
    };
    return rank(a) < rank(b);
  });
}

LibraryStore::Arrangement LibraryStore::arrangement() const {
  juce::Array<juce::var> factory;
  for (const auto& p : presets_.presets())
    if (p.factory) {
      auto* o = new juce::DynamicObject();
      o->setProperty("id", p.id);
      o->setProperty("name", p.name);
      factory.add(juce::var(o));
    }
  return {prefs_.getJson(kFavoritesPref), prefs_.getJson(kSiteTonesPref), juce::var(factory), prefs_.getJson(kOrderPref),
          prefs_.getJson(kFolderOrderPref), prefs_.getJson(ToneArt::kCachePref)};
}

juce::String LibraryStore::halfFor(const LibraryNode& item) const {
  const bool presets = item.kind == LibraryNode::Kind::preset || item.isPresets();
  const auto* half = presets ? presetsRoot() : capturesRoot();
  return half != nullptr ? half->path : juce::String();
}

LibraryStore::LibraryStore(Backend& backend, ChainStore& chain, PresetStore& presets, ToneSession& session,
                           ConnectionGate& connection, UiPrefs& prefs, Toast& toast)
    : backend_(backend),
      chain_(chain),
      presets_(presets),
      session_(session),
      connection_(connection),
      prefs_(prefs),
      toast_(toast),
      art_(session, prefs) {
  // The big ones in files of their own (UiPrefs::storeApart): kept links,
  // the favorites and TONE3000 lists (up to 1000 tones each), the artwork
  // cache (a folder per entry, ever).
  for (const char* key : {kKeptPref, kFavoritesPref, kSiteTonesPref, ToneArt::kCachePref}) prefs_.storeApart(key);
  presets_.addListener(this);
  session_.addListener(this);
  chain_.addListener(this);
  prefs_.addListener(this);
  restoreView();
  loadState();  // a restored or moved library's links, before any card asks
}

// View

void LibraryStore::restoreView() {
  const auto view = juce::JSON::parse(backend_.getLibraryView());
  if (!view.isObject()) return;
  viewRestored_ = true;
  if (const auto* open = view["open"].getArray())
    for (const auto& p : *open) open_.insert(p.toString());
  selected_ = view["selected"].toString();
  filter = view["filter"].toString();
  if (const auto* gears = view["gears"].getArray())
    for (const auto& g : *gears) gearFilter.insert(g.toString());
  scroll_ = view.getProperty("scroll", 0);
  drawerShown_ = view.getProperty("shown", false);
}

void LibraryStore::saveView() {
  auto* view = new juce::DynamicObject();
  juce::Array<juce::var> open;
  for (const auto& p : open_) open.add(p);
  view->setProperty("open", open);
  view->setProperty("selected", selected_);
  view->setProperty("filter", filter);
  juce::Array<juce::var> gears;
  for (const auto& g : gearFilter) gears.add(g);
  view->setProperty("gears", gears);
  view->setProperty("scroll", scroll_);
  view->setProperty("shown", drawerShown_);
  backend_.setLibraryView(juce::JSON::toString(juce::var(view), true));
}

void LibraryStore::setScroll(int y) {
  if (y == scroll_) return;
  scroll_ = y;
  scrollSave_.start(400, [this] { saveView(); });
}

void LibraryStore::setShown(bool shown) {
  if (shown == drawerShown_) return;
  drawerShown_ = shown;
  saveView();
}

LibraryStore::~LibraryStore() {
  scanStop_->store(true);  // a walk under way stops now (scanPool_ then joins at once)
  if (scrollSave_.pending()) saveView();  // the last scroll, not yet saved
  if (stateSave_.pending()) saveState(/*reread=*/false);  // the last change, not yet in the library
  prefs_.removeListener(this);
  chain_.removeListener(this);
  session_.removeListener(this);
  presets_.removeListener(this);
}

void LibraryStore::addListener(Listener* l) {
  const bool first = listeners_.isEmpty();
  listeners_.add(l);
  if (!first) return;
  refresh();
  if (!favoritesFetched_) fetchFavorites();
}

void LibraryStore::reload() {
  refresh(/*fresh=*/true);
  fetchFavorites();
}

// Your on-disk halves, listed or hidden while empty.
const LibraryNode* LibraryStore::capturesRoot() const {
  const auto* mine = tree_->mine();
  if (mine == nullptr) return nullptr;
  for (const auto& child : mine->children)
    if (isDiskHalf(child) && child.isCaptures() && !child.local) return &child;
  for (const auto& h : tree_->hidden)
    if (h.parent == mine->path && h.node.isCaptures() && !h.node.local) return &h.node;
  return nullptr;
}

const LibraryNode* LibraryStore::presetsRoot() const {
  const auto* mine = tree_->mine();
  if (mine == nullptr) return nullptr;
  for (const auto& child : mine->children)
    if (isDiskHalf(child) && child.isPresets()) return &child;
  for (const auto& h : tree_->hidden)
    if (h.parent == mine->path && h.node.isPresets()) return &h.node;
  return nullptr;
}

juce::File LibraryStore::root() const {
  const auto stored = prefs_.get(kRootPref);
  return stored.isNotEmpty() && juce::File::isAbsolutePath(stored) ? juce::File(stored) : defaultRoot();
}

juce::String LibraryStore::owner() const { return prefs_.get(kOwnerPref, kDefaultOwner); }

void LibraryStore::pushLocation() {
  // Fix your library's name on first use (see the header).
  if (prefs_.get(kOwnerPref).isEmpty()) {
    const auto user = session_.user();
    prefs_.set(kOwnerPref, user && user->username.isNotEmpty() ? user->username : juce::String(kDefaultOwner));
  }
  backend_.setLibraryLocation(root(), owner(), links());
  adoptUsername();
}

// Named "My Library" because nobody was signed in on first use: once someone
// is, it takes their username (a rename of your library, so pictures and
// kept links follow). Not over a folder already called that.
void LibraryStore::adoptUsername() {
  const auto user = session_.user();
  if (!user || user->username.isEmpty() || owner() != kDefaultOwner) return;
  const auto from = root().getChildFile(kDefaultOwner);
  if (root().getChildFile(juce::File::createLegalFileName(user->username)).exists()) return;
  if (!from.exists()) {
    prefs_.set(kOwnerPref, user->username);  // nothing on disk yet: just the name
    backend_.setLibraryLocation(root(), owner(), links());
    return;
  }
  const auto renamed = backend_.libraryRename(from, user->username);
  if (renamed == juce::File()) return;  // stays "My Library"; asked again next time
  remapPaths(from.getFullPathName(), renamed.getFullPathName());
  prefs_.set(kOwnerPref, renamed.getFileName());
  note("Your library is now called " + renamed.getFileName());
}

juce::Array<juce::File> LibraryStore::links() const {
  juce::Array<juce::File> out;
  const auto stored = prefs_.getJson(kLinksPref);  // held: getArray points into it
  if (const auto* paths = stored.getArray())
    for (const auto& path : *paths)
      if (juce::File::isAbsolutePath(path.toString())) out.add(juce::File(path.toString()));
  return out;
}

bool LibraryStore::link(const juce::File& dir) {
  pushLocation();
  const auto problem = backend_.libraryLinkProblem(dir);
  if (problem.isNotEmpty()) return fail("Can't link " + dir.getFileName() + ": " + problem);
  juce::Array<juce::var> paths;
  for (const auto& existing : links()) paths.add(existing.getFullPathName());
  paths.add(dir.getFullPathName());
  prefs_.setJson(kLinksPref, paths);
  open_.insert(dir.getFullPathName());
  selected_ = dir.getFullPathName();
  refresh();
  note("Linked " + dir.getFileName());
  return true;
}

bool LibraryStore::unlink(const juce::String& path) {
  juce::Array<juce::var> paths;
  bool found = false;
  for (const auto& existing : links()) {
    if (existing == juce::File(path)) found = true;
    else paths.add(existing.getFullPathName());
  }
  if (!found) return false;
  prefs_.setJson(kLinksPref, paths);
  if (selected_ == path) selected_.clear();
  chain_.refresh();
  refresh();
  note("Unlinked " + juce::File(path).getFileName());
  return true;
}

bool LibraryStore::importFolder(const juce::File& dir, const juce::String& folderPath) {
  // With no folder named: Local, beside the linked folders (a collection is
  // other people's captures; Captures is for your own). Local is made the
  // first time, so it needn't be listed yet.
  const juce::String target =
      folderPath.isNotEmpty() ? folderPath : root().getChildFile(owner()).getChildFile("Local").getFullPathName();
  const auto* folder = tree_->find(target);
  if (folderPath.isNotEmpty() && (folder == nullptr || !folder->writable || folder->favorites || folder->isPresets()))
    return fail("Choose a captures folder for this");
  pushLocation();
  progress("Copying " + dir.getFileName() + "...");
  // A collection can be gigabytes: copied off the message thread.
  backend_.libraryImportFolderAsync(dir, juce::File(target), scope_.wrap([this, dir, target](juce::File landed) {
    if (landed == juce::File()) return (void)fail("No captures found in " + dir.getFileName());
    open_.insert(target);
    open_.insert(landed.getFullPathName());
    selected_ = landed.getFullPathName();
    refresh();
    note("Imported " + dir.getFileName());
  }));
  return true;
}

void LibraryStore::importFolderDialog(const juce::String& folderPath) {
  launch(std::make_unique<juce::FileChooser>("Import Folder", juce::File()),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
         [this, folderPath](const juce::FileChooser& fc) {
           if (fc.getResult() != juce::File()) importFolder(fc.getResult(), folderPath);
         });
}

void LibraryStore::linkFolder() {
  launch(std::make_unique<juce::FileChooser>("Link Folder", juce::File()),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
         [this](const juce::FileChooser& fc) {
           if (fc.getResult() != juce::File()) link(fc.getResult());
         });
}

void LibraryStore::refresh(bool fresh) {
  pushLocation();
  ++scanGeneration_;
  if (fresh) freshWanted_->store(true);
  // A scan waiting to start already scans what is current when it does.
  if (scanQueued_->exchange(true)) return;
  const auto with = arrangement();  // prefs and the preset list are message-thread only
  const int version = arrangementVersion_;
  auto deliver = scope_.wrap([this, version](int generation, std::shared_ptr<LibraryTree> tree) {
    if (generation != scanGeneration_) return;  // a newer scan supersedes this one
    // Favorites, the TONE3000 account's tones or the order changed while it
    // scanned: arranged with what is current, not what was.
    if (version != arrangementVersion_) {
      arrange(*tree, arrangement());
      tree->reindex();
    }
    apply(std::move(tree));
  });
  scanPool_.addJob([this, deliver, with, queued = scanQueued_, freshWanted = freshWanted_, stop = scanStop_] {
    // Started: a refresh from here on queues the next scan.
    queued->store(false);
    const int generation = scanGeneration_.load();
    const bool fresh = freshWanted->exchange(false);
    // Scan, parse, place the favorites and index, all here: the message
    // thread only swaps the finished tree in.
    const auto post = [&](const juce::var& listing) {
      if (static_cast<bool>(listing.getProperty("cancelled", false))) return;  // cut short: nothing to show
      auto tree = std::make_shared<LibraryTree>(LibraryTree::parse(listing, /*index=*/false));
      arrange(*tree, with);
      tree->reindex();
      juce::MessageManager::callAsync([deliver, generation, tree] { deliver(generation, tree); });
    };
    // Until this session has walked the disk, last session's listing shows
    // at once; the checked one replaces it when the walk is done.
    if (!fresh)
      if (const auto saved = backend_.getSavedLibrary(); saved.isObject()) post(saved);
    post(backend_.getLibrary(fresh, stop.get()));
  });
}

void LibraryStore::apply(std::shared_ptr<LibraryTree> tree) {
  const bool first = !loaded_;
  loaded_ = true;
  tree_ = std::move(tree);
  loadState();  // a library brought in, or edited on another machine
  checkMissing();
  // Written once a session has its tree, changed or not: a library that
  // never had a file (an older build's) gets one.
  if (first) stateSave_.start(2000, [this] { saveState(); });
  if (first && !viewRestored_)
    for (const auto& library : tree_->libraries)
      if (library.mine && !library.site) open_.insert(library.path);  // yours (TONE3000 opens when asked)
  // Show in Library, asked before the tree had the file (focus notifies).
  if (pendingReveal_.isNotEmpty() && tree_->find(pendingReveal_) != nullptr)
    focus(std::exchange(pendingReveal_, {}));
  else
    notify();
  // Same-bytes matches are per tree; cards and titles look again.
  same_.clear();
  stashSources_.clear();
  movedChecked_.clear();
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  findMovedFiles();
  syncShown();
}

// Favorites

void LibraryStore::injectFavorites() {
  ++arrangementVersion_;
  // A new tree swapped in, never this one edited: the drawer's rows hold
  // references into it until the notify that follows rebuilds them.
  auto tree = std::make_shared<LibraryTree>(*tree_);
  arrange(*tree, arrangement());
  tree->reindex();
  tree_ = std::move(tree);
  same_.clear();
}

// Library order

void LibraryStore::applyOrder(const juce::StringArray& order) {
  juce::Array<juce::var> paths;
  for (const auto& p : order) paths.add(p);
  prefs_.setJson(kOrderPref, paths);
  injectFavorites();  // re-sorted
  notify();
}

bool LibraryStore::canMoveLibrary(const juce::String& path, int by) const {
  const auto& libraries = tree_->libraries;
  for (size_t i = 0; i < libraries.size(); ++i)
    if (libraries[i].path == path) {
      const auto to = static_cast<int>(i) + by;
      return to >= 0 && to < static_cast<int>(libraries.size());
    }
  return false;
}

void LibraryStore::moveLibrary(const juce::String& path, int by) {
  juce::StringArray order;
  for (const auto& l : tree_->libraries) order.add(l.path);
  const int from = order.indexOf(path);
  const int to = from + by;
  if (from < 0 || to < 0 || to >= order.size()) return;
  order.move(from, to);
  applyOrder(order);
}

void LibraryStore::moveLibraryBefore(const juce::String& path, const juce::String& beforePath) {
  juce::StringArray order;
  for (const auto& l : tree_->libraries) order.add(l.path);
  const int from = order.indexOf(path);
  if (from < 0 || order.indexOf(beforePath) < 0 || path == beforePath) return;
  order.remove(from);
  order.insert(order.indexOf(beforePath), path);
  applyOrder(order);
}

// Folder pictures

namespace {
juce::File picturesDir() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("LibraryPictures");
}
juce::String pictureKey(const juce::String& folderPath) { return folderPath.toLowerCase(); }
}  // namespace

juce::File LibraryStore::pictureFor(const juce::String& folderPath) const {
  const auto stored = prefs_.getJson(kPicturesPref).getProperty(juce::Identifier(pictureKey(folderPath)), {}).toString();
  if (!juce::File::isAbsolutePath(stored)) return {};
  const juce::File picture(stored);
  return picture.existsAsFile() ? picture : juce::File();
}

void LibraryStore::choosePicture(const juce::String& folderPath) {
  launch(std::make_unique<juce::FileChooser>("Picture for " + juce::File(folderPath).getFileName(), juce::File{},
                                             picture_file::patterns()),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
         [this, folderPath](const juce::FileChooser& chooser) {
           const auto picked = chooser.getResult();
           if (picked.existsAsFile()) setPicture(folderPath, picked);
         });
}

void LibraryStore::setPicture(const juce::String& folderPath, const juce::File& image) {
  // A copy of its own: the original may move, or sit on a drive that's gone.
  // In the folder's library (library state), so it goes where that goes.
  const auto home = stateHome(pictureKey(folderPath), true);
  const auto dir = home != juce::File() ? library_state::picturesOf(home) : picturesDir();
  dir.createDirectory();
  // PNG, JPEG and GIF are kept as they are; anything else the system can
  // read (WebP) is kept as a PNG, so showing it never needs the decoder again.
  const auto ext = image.getFileExtension().toLowerCase();
  const bool asIs = ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif";
  const auto copy = dir.getChildFile(juce::Uuid().toString() + (asIs ? ext : juce::String(".png")));
  if (asIs) {
    if (!image.copyFileTo(copy)) return (void)fail("Couldn't use that picture");
  } else {
    const auto decoded = picture_file::load(image);
    juce::FileOutputStream out(copy);
    if (!decoded.isValid() || !out.openedOk() || !juce::PNGImageFormat().writeImageToStream(decoded, out)) {
      copy.deleteFile();
      return (void)fail("Couldn't read that picture");
    }
  }
  if (const auto old = pictureFor(folderPath); old != juce::File() && !pictureInUse(old, pictureKey(folderPath)))
    old.deleteFile();
  auto all = prefs_.getJson(kPicturesPref);
  if (!all.isObject()) all = juce::var(new juce::DynamicObject());
  all.getDynamicObject()->setProperty(juce::Identifier(pictureKey(folderPath)), copy.getFullPathName());
  prefs_.setJson(kPicturesPref, all);
  refreshArtUnder(juce::File(folderPath));
  notify();
  note("Picture set for " + juce::File(folderPath).getFileName());
}

void LibraryStore::removePicture(const juce::String& folderPath) {
  if (const auto old = pictureFor(folderPath); old != juce::File() && !pictureInUse(old, pictureKey(folderPath)))
    old.deleteFile();
  auto all = prefs_.getJson(kPicturesPref);
  if (auto* obj = all.getDynamicObject()) obj->removeProperty(juce::Identifier(pictureKey(folderPath)));
  prefs_.setJson(kPicturesPref, all);
  refreshArtUnder(juce::File(folderPath));
  notify();
}

void LibraryStore::refreshArtUnder(const juce::File& folder) {
  std::vector<std::pair<std::string, juce::File>> blocks;
  for (const auto* block : chain_.state().toneBlocks()) {
    const auto playing = playingSource(block->blockId);
    if (playing.isEmpty()) continue;
    // A copy kept from a TONE3000 tone wears the tone's artwork, whatever
    // its folder's picture.
    if (siteOriginalOf(playing)) continue;
    // A kept copy dresses as its original: that's the folder that counts.
    const auto original = originalOf(playing);
    const juce::File source = original != juce::File() ? original : juce::File(playing);
    if (source.isAChildOf(folder)) blocks.emplace_back(block->blockId, source);
  }
  for (const auto& [id, source] : blocks) {
    auto* clear = new juce::DynamicObject();
    clear->setProperty("clear", true);
    chain_.setLocalToneArt(id, juce::var(clear));
    artFor(source, id);
  }
}

void LibraryStore::sessionChanged() {
  // Signed in with your library still "My Library": it takes your username.
  if (session_.authenticated() && owner() == kDefaultOwner) refresh();  // pushLocation adopts it
  // Signed in: fetch them for the drawer that is up (else on its next open).
  favoritesFetched_ = false;
  if (!listeners_.isEmpty()) fetchFavorites();
  // And the artwork that waited for it, for blocks still in the chain.
  if (session_.authenticated() && !artAfterSignIn_.empty()) {
    const auto waiting = std::exchange(artAfterSignIn_, {});
    for (const auto& [blockId, file] : waiting)
      if (const auto* block = chain_.state().findBlock(blockId); block != nullptr && block->isTone() && block->tone.local)
        artFor(file, blockId);
  }
}

void LibraryStore::fetchFavorites() {
  fetchSiteTones();
  if (!session_.authenticated() || favoritesLoading_) return;
  favoritesLoading_ = true;
  favoritesFetched_ = true;
  ToneQuery query;
  query.profile = Profile::favorited;
  fetchRefsPage(query, 1, std::make_shared<juce::Array<juce::var>>(), [this](const juce::Array<juce::var>* refs) {
    favoritesLoading_ = false;
    if (refs == nullptr) return;  // keep the cached list
    prefs_.setJson(kFavoritesPref, juce::var(*refs));
    injectFavorites();
    notify();
  });
}

// TONE3000's Captures: the TONE3000 account's tones (a creator search).
void LibraryStore::fetchSiteTones() {
  if (!session_.authenticated() || siteTonesLoading_) return;
  siteTonesLoading_ = true;
  ToneQuery query;
  query.creators = {kSiteUsername};
  fetchRefsPage(query, 1, std::make_shared<juce::Array<juce::var>>(), [this](const juce::Array<juce::var>* refs) {
    siteTonesLoading_ = false;
    if (refs == nullptr) return;
    prefs_.setJson(kSiteTonesPref, juce::var(*refs));
    injectFavorites();
    notify();
  });
}

// One page of a tone search, as references (no model: the tone, loading
// takes its first model, as a browser pick), then the next. A reply may land
// synchronously (a cached or mocked session), so nothing here may outlive
// its own call.
void LibraryStore::fetchRefsPage(ToneQuery query, int page, std::shared_ptr<juce::Array<juce::var>> collected,
                                 std::function<void(const juce::Array<juce::var>*)> done) {
  constexpr int kPageSize = 100, kMaxPages = 10;
  session_.searchTones(query, page, kPageSize, scope_.wrap([this, query, collected, page, done](Result<TonePage> result) {
    if (!result) return done(nullptr);
    for (const auto& tone : result->data) {
      const bool seen = std::any_of(collected->begin(), collected->end(), [&tone](const juce::var& ref) {
        return static_cast<int>(ref["tone"]["id"]) == tone.id;
      });
      if (seen) continue;  // a page boundary that moved under us
      auto* toneObj = new juce::DynamicObject();
      toneObj->setProperty("id", tone.id);
      toneObj->setProperty("title", tone.title);
      toneObj->setProperty("gear", tone.gear);
      toneObj->setProperty("format", tone.format);
      if (!tone.images.empty()) toneObj->setProperty("image", tone.images.front());
      if (tone.url.isNotEmpty()) toneObj->setProperty("url", tone.url);
      if (tone.user) {
        auto* user = new juce::DynamicObject();
        user->setProperty("username", tone.user->username);
        toneObj->setProperty("user", user);
      }
      auto* ref = new juce::DynamicObject();
      ref->setProperty("format", "t3ktone");
      ref->setProperty("version", 1);
      ref->setProperty("tone", toneObj);
      ref->setProperty("model", new juce::DynamicObject());
      collected->add(juce::var(ref));
    }
    if (page < result->totalPages && page < kMaxPages) return fetchRefsPage(query, page + 1, collected, done);
    done(collected.get());
  }));
}

void LibraryStore::setFavorite(const LibraryToneRef& ref, bool favorite) {
  if (!session_.authenticated()) return (void)fail("Sign in to change your TONE3000 favorites");
  if (ref.toneId <= 0) return (void)fail("Only TONE3000 tones can be favorites");
  session_.setToneFavorite(ref.toneId, favorite, scope_.wrap([this, ref, favorite](const juce::String& error) {
    if (error.isNotEmpty()) return (void)fail(error);
    // Update the cache at once; the next fetch confirms it.
    juce::Array<juce::var> refs;
    const auto stored = prefs_.getJson(kFavoritesPref);  // held: getArray points into it
    if (const auto* cached = stored.getArray())
      for (const auto& raw : *cached)
        if (LibraryToneRef::parse(raw).toneId != ref.toneId) refs.add(raw);
    if (favorite) {
      // A favorite is the tone, not one of its models.
      auto* stripped = new juce::DynamicObject();
      stripped->setProperty("format", "t3ktone");
      stripped->setProperty("version", 1);
      stripped->setProperty("tone", ref.raw["tone"]);
      stripped->setProperty("model", new juce::DynamicObject());
      refs.insert(0, juce::var(stripped));
    }
    prefs_.setJson(kFavoritesPref, juce::var(refs));
    open_.insert(kFavoritesPath);
    injectFavorites();
    notify();
    note(favorite ? "Added to Favorites" : "Removed from Favorites");
  }));
}

void LibraryStore::notify() {
  listeners_.call([](Listener& l) { l.libraryChanged(); });
}

void LibraryStore::presetsChanged(const std::vector<PresetInfo>&) {
  if (!listeners_.isEmpty()) refresh();
}

void LibraryStore::focus(const juce::String& path) {
  if (tree_->find(path) == nullptr) return (void)fail("That isn't in the Library");
  for (const auto* up = tree_->parentOf(path); up != nullptr; up = tree_->parentOf(up->path)) open_.insert(up->path);
  selected_ = path;
  focus_ = path;
  saveView();
  if (filter.isNotEmpty() && !libraryNameMatches(juce::File(path).getFileNameWithoutExtension(), filter)) {
    filter.clear();
    closedInFilter.clear();
    openedInFilter.clear();
  } else if (filter.isNotEmpty()) {
    // The search stays: the folders above it shown open.
    for (const auto* up = tree_->parentOf(path); up != nullptr; up = tree_->parentOf(up->path)) {
      closedInFilter.erase(up->path);
      openedInFilter.insert(up->path);
    }
  }
  notify();
}

void LibraryStore::showBlock(const std::string& blockId) {
  const auto path = playingSource(blockId);
  if (path.isEmpty()) return (void)fail("This block isn't playing a file");
  // Only somewhere the Library lists: your library or a linked folder.
  const juce::File file(path);
  bool listed = file.isAChildOf(root());
  for (const auto& link : links()) listed = listed || file.isAChildOf(link);
  if (!listed) return (void)fail("That file isn't in the Library");
  if (onRequestShow) onRequestShow();
  if (loaded_ && tree_->find(path) != nullptr) return focus(path);
  pendingReveal_ = path;  // focused once a scan has it (apply)
  if (scanGeneration_ == 0) refresh();
}

void LibraryStore::follow(const juce::String& path) {
  if (!loaded_ || tree_->find(path) == nullptr || selected_ == path) return;
  for (const auto* up = tree_->parentOf(path); up != nullptr; up = tree_->parentOf(up->path)) open_.insert(up->path);
  selected_ = path;
  if (filter.isEmpty() || libraryNameMatches(juce::File(path).getFileNameWithoutExtension(), filter)) focus_ = path;
  saveView();
  notify();
}

void LibraryStore::setOpen(const juce::String& path, bool open) {
  if (open == isOpen(path)) return;
  if (open) open_.insert(path);
  else open_.erase(path);
  saveView();
  notify();
}

int LibraryStore::missingNoticeCount() const {
  // The strip is for what you kept (a copy, the keep folder): an original
  // or a picture's folder gone is only in the list (⋯ → Missing Files...).
  const auto hidden = prefs_.getJson(kMissingHiddenPref);
  int count = 0;
  for (const auto& entry : missing_)
    if ((entry.kind == Missing::Kind::copy || entry.kind == Missing::Kind::keepFolder) &&
        !hidden.hasProperty(juce::Identifier(entry.path)))
      ++count;
  return count;
}

void LibraryStore::hideMissingNotice() {
  auto hidden = juce::JSON::parse(juce::JSON::toString(prefs_.getJson(kMissingHiddenPref)));
  if (!hidden.isObject()) hidden = juce::var(new juce::DynamicObject());
  for (const auto& entry : missing_) hidden.getDynamicObject()->setProperty(juce::Identifier(entry.path), true);
  prefs_.setJson(kMissingHiddenPref, hidden);
  notify();
}

void LibraryStore::progress(const juce::String& message) { toast_.pin(message, Toast::Style::quiet); }

void LibraryStore::note(const juce::String& message) { toast_.show(message, Toast::Style::quiet); }

bool LibraryStore::fail(const juce::String& message) {
  toast_.show(message);
  return false;
}

// Using items

std::string LibraryStore::appendSlot() const {
  const auto& state = chain_.state();
  const bool right = state.stereoEnabled && state.activeSide == ChainSide::right && state.chainRight;
  const auto& lane = right ? *state.chainRight : state.chain;
  // The first empty slot after the last block: right behind the chain, not
  // the lane's far end (a lane shows empty slots past its blocks; landing in
  // the last would leave a gap, the new block off to the side).
  size_t after = 0;
  for (size_t i = 0; i < lane.size(); ++i)
    if (!lane[i].isInsert) after = i + 1;
  for (size_t i = after; i < lane.size(); ++i)
    if (lane[i].isInsert) return lane[i].blockId;
  for (auto it = lane.rbegin(); it != lane.rend(); ++it)
    if (it->isInsert) return it->blockId;  // the lane is full past its last block: any gap
  return {};
}

namespace {
// The part of the rig a tone or capture plays, for auditioning: "amp" (a
// head, an amp and cab, a full rig), "pedal", "cab" (a NAM cab or any IR),
// "outboard", "space"; "" when its gear isn't known.
juce::String auditionKind(const juce::String& format, const juce::String& gear) {
  const auto g = gear.toLowerCase();
  if (format == "ir" || g == "cab") return "cab";
  if (g == "amp" || g == "amp-cab" || g == "full-rig") return "amp";
  if (g == "pedal" || g == "outboard" || g == "space") return g;
  return {};
}

// What a Library item plays: its format ("nam", "ir"; "" a preset or not
// known) and gear ("" not known).
std::pair<juce::String, juce::String> playsOf(const LibraryNode& node) {
  switch (node.kind) {
    case LibraryNode::Kind::capture: return {node.nam ? "nam" : "ir", node.gear};
    case LibraryNode::Kind::tone: return {node.tone.format, node.tone.gear};
    case LibraryNode::Kind::folder:
      for (const auto& child : node.children)
        if (child.kind == LibraryNode::Kind::capture) return {child.nam ? "nam" : "ir", child.gear};
      return {};
    default: return {};
  }
}
}  // namespace

std::string LibraryStore::auditionTarget(const juce::String& format, const juce::String& gear) const {
  const auto& state = chain_.state();
  const auto kind = auditionKind(format, gear);
  // A tone block of the same part: an amp tried replaces the amp, a pedal
  // the pedal, an IR the cab, never one the other. With either's gear not
  // known, at least the same format (a capture never over an IR).
  auto fits = [&state, &format, &kind](const std::string& id) {
    const auto* block = state.findBlock(id);
    if (block == nullptr || !block->isTone()) return false;
    const auto blockKind = auditionKind(block->tone.format, block->tone.gear);
    if (kind.isNotEmpty() && blockKind.isNotEmpty()) return kind == blockKind;
    return format.isEmpty() || block->tone.format.isEmpty() || block->tone.format == format;
  };
  const auto open = prefs_.session.find(UiPrefs::kDetailBlockId);
  if (open != prefs_.session.end() && fits(open->second.toStdString())) return open->second.toStdString();
  for (const auto& key : {kind, format})
    if (const auto it = auditionByKind_.find(key); key.isNotEmpty() && it != auditionByKind_.end() && fits(it->second))
      return it->second;
  if (!auditionBlock_.empty() && fits(auditionBlock_)) return auditionBlock_;
  // The chain already plays that part (a project's pedal, a block from the
  // browser): that block, the last one in the active lane, else the other
  // lane's. Only for a part known on both sides: an unknown capture never
  // guesses which block it is meant for.
  if (kind.isNotEmpty()) {
    const bool right = state.stereoEnabled && state.activeSide == ChainSide::right && state.chainRight;
    const auto lastIn = [&kind](const std::vector<ChainItem>& lane) -> std::string {
      for (auto it = lane.rbegin(); it != lane.rend(); ++it)
        if (it->isTone() && auditionKind(it->tone.format, it->tone.gear) == kind) return it->blockId;
      return {};
    };
    if (auto id = lastIn(right ? *state.chainRight : state.chain); !id.empty()) return id;
    if (state.chainRight)
      if (auto id = lastIn(right ? state.chain : *state.chainRight); !id.empty()) return id;
  }
  return appendSlot();
}

void LibraryStore::auditioning(const std::string& blockId) {
  auditionBlock_ = blockId;
  if (const auto* block = chain_.state().findBlock(blockId); block != nullptr && block->isTone()) {
    if (const auto kind = auditionKind(block->tone.format, block->tone.gear); kind.isNotEmpty())
      auditionByKind_[kind] = blockId;
    auditionByKind_[block->tone.format] = blockId;
  }
}

std::vector<std::string> LibraryStore::auditionChoices(const juce::String& format, const juce::String& gear) const {
  const auto kind = auditionKind(format, gear);
  if (kind.isEmpty()) return {};
  const auto& state = chain_.state();
  const auto playsKind = [&state, &kind](const std::string& id) {
    const auto* block = state.findBlock(id);
    return block != nullptr && block->isTone() && auditionKind(block->tone.format, block->tone.gear) == kind;
  };
  // Already pointed at: the open card, or the block that part last went into.
  if (const auto open = prefs_.session.find(UiPrefs::kDetailBlockId);
      open != prefs_.session.end() && playsKind(open->second.toStdString()))
    return {};
  if (const auto it = auditionByKind_.find(kind); it != auditionByKind_.end() && playsKind(it->second)) return {};
  if (!auditionBlock_.empty() && playsKind(auditionBlock_)) return {};
  // Otherwise every block of that part, the active lane's first.
  std::vector<std::string> choices;
  const bool right = state.stereoEnabled && state.activeSide == ChainSide::right && state.chainRight;
  const auto collect = [&](const std::vector<ChainItem>& lane) {
    for (const auto& item : lane)
      if (item.isTone() && auditionKind(item.tone.format, item.tone.gear) == kind) choices.push_back(item.blockId);
  };
  collect(right ? *state.chainRight : state.chain);
  if (state.chainRight) collect(right ? state.chain : *state.chainRight);
  return choices.size() > 1 ? choices : std::vector<std::string>();
}

void LibraryStore::use(const LibraryNode& node, const std::string& targetBlockId) {
  const auto [format, gear] = playsOf(node);
  // Two blocks of the part it plays (two pedals) and neither pointed at:
  // ask which (once: the pick is remembered for that part).
  if (targetBlockId.empty() && chooseBlock)
    if (const auto choices = auditionChoices(format, gear); !choices.empty()) {
      chooseBlock(choices, [this, path = node.path](const std::string& picked) {
        const auto* again = tree_->find(path);
        if (again == nullptr) return;
        const auto slot = picked.empty() ? appendSlot() : picked;
        if (slot.empty()) return (void)fail("The chain is full");
        use(*again, slot);
      });
      return;
    }
  const std::string target = targetBlockId.empty() ? auditionTarget(format, gear) : targetBlockId;
  switch (node.kind) {
    case LibraryNode::Kind::preset:
      if (beforePresetLoad) beforePresetLoad();
      if (!presets_.load(node.presetId)) fail("Couldn't load " + node.name);
      break;
    case LibraryNode::Kind::tone:
      loadToneRef(node.tone, target);
      break;
    case LibraryNode::Kind::capture:
      loadCapture(node.file(), target);
      break;
    case LibraryNode::Kind::folder:
      if (node.loadsAsBlock()) loadCapture(node.file(), target);
      break;
    case LibraryNode::Kind::library:
      break;
  }
}

bool LibraryStore::abSwitch(const std::string& blockId) {
  const auto* block = chain_.state().findBlock(blockId);
  const auto* before = chain_.previous(blockId);
  if (block == nullptr || !block->isTone() || before == nullptr) return false;
  // Another capture of the folder the block plays: switched, nothing read.
  if (block->tone.local && before->tone.local) {
    juce::String file;
    for (const auto& m : before->tone.models)
      if (m.id == before->activeModelId) file = m.sourcePath;
    for (const auto& m : block->tone.models)
      if (file.isNotEmpty() && m.sourcePath == file && m.modelUrl.isNotEmpty()) {
        if (m.id == block->activeModelId) return true;
        auto* raw = new juce::DynamicObject();
        raw->setProperty("id", m.id);
        raw->setProperty("name", m.name);
        raw->setProperty("model_url", m.modelUrl);
        chain_.switchModel(blockId, m.id, juce::var(raw));
        return true;
      }
  }
  playAgain(blockId, *before);
  return true;
}

std::string LibraryStore::abBlock() const {
  const auto& state = chain_.state();
  const auto isTone = [&state](const std::string& id) {
    const auto* block = state.findBlock(id);
    return block != nullptr && block->isTone();
  };
  if (!auditionBlock_.empty() && isTone(auditionBlock_)) return auditionBlock_;
  if (const auto open = prefs_.session.find(UiPrefs::kDetailBlockId); open != prefs_.session.end())
    if (isTone(open->second.toStdString())) return open->second.toStdString();
  return {};
}

void LibraryStore::playAgain(const std::string& blockId, const ChainItem& before) {
  if (before.tone.local) {
    for (const auto& m : before.tone.models)
      if (m.id == before.activeModelId && m.sourcePath.isNotEmpty()) return loadCapture(juce::File(m.sourcePath), blockId);
    return (void)fail("Can't go back: that capture didn't come from a file");
  }
  LibraryToneRef ref;
  ref.toneId = before.tone.id;
  ref.modelId = before.activeModelId;
  ref.title = before.tone.title;
  loadToneRef(ref, blockId);
}

void LibraryStore::addAsNewBlock(const LibraryNode& node) {
  const auto slot = appendSlot();
  if (slot.empty()) return (void)fail("The chain is full");
  use(node, slot);
}

bool LibraryStore::canPlaceBeside(const juce::String& dragged, const juce::String& sibling) const {
  if (dragged.isEmpty() || dragged == sibling) return false;
  const auto* a = tree_->find(dragged);
  const auto* b = tree_->find(sibling);
  if (a == nullptr || b == nullptr || a->kind != LibraryNode::Kind::folder || b->kind != LibraryNode::Kind::folder ||
      a->favorites || b->favorites)
    return false;
  const auto* parent = tree_->parentOf(dragged);
  return parent != nullptr && parent == tree_->parentOf(sibling) && parent->kind != LibraryNode::Kind::library;
}

void LibraryStore::placeFolder(const juce::String& dragged, const juce::String& sibling, bool after) {
  if (!canPlaceBeside(dragged, sibling)) return;
  const auto* parent = tree_->parentOf(dragged);
  const auto moved = tree_->find(dragged)->name;
  const auto beside = tree_->find(sibling)->name;
  juce::StringArray names;  // the folders as they show now, the dragged one out
  for (const auto& child : parent->children)
    if (child.kind == LibraryNode::Kind::folder && child.path != dragged) names.add(child.name);
  int at = names.indexOf(beside);
  if (at < 0) return;
  names.insert(after ? at + 1 : at, moved);
  juce::Array<juce::var> list;
  for (const auto& name : names) list.add(name);
  auto all = copyOfObject(prefs_.getJson(kFolderOrderPref));
  all->setProperty(juce::Identifier(parent->path.toLowerCase()), juce::var(list));
  prefs_.setJson(kFolderOrderPref, juce::var(all.get()));
  injectFavorites();  // re-sorted, no rescan
  notify();
}

int LibraryStore::newInFolder(const std::string& blockId) {
  const auto* block = chain_.state().findBlock(blockId);
  if (block == nullptr || !block->isTone() || !block->tone.local) return 0;
  const auto playing = playingSource(blockId);
  if (playing.isEmpty()) return 0;
  const juce::File file(playing);
  const auto folder = file.getParentDirectory();
  if (!file.existsAsFile()) return 0;
  // What a folder load takes: the files of the playing one's kind, no
  // subfolders, none of a Mac zip's "._" leftovers.
  auto& seen = folderFiles_[folder.getFullPathName()];
  const auto stamp = folder.getLastModificationTime().toMilliseconds();
  const auto extension = file.getFileExtension().toLowerCase();
  if (seen.stamp != stamp || seen.extension != extension) {
    seen = {stamp, extension, {}};
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*" + extension))
      if (!f.getFileName().startsWith("._")) seen.names.add(f.getFileName().toLowerCase());
  }
  if (seen.names.size() > 300) return 0;  // past the folder-load cap: it loads alone anyway
  std::set<juce::String> listed;
  for (const auto& m : block->tone.models) {
    const auto path = juce::File::isAbsolutePath(m.sourcePath) ? m.sourcePath
                      : juce::URL(m.modelUrl).isLocalFile() ? juce::URL(m.modelUrl).getLocalFile().getFullPathName()
                                                            : juce::String();
    if (path.isNotEmpty() && juce::File(path).getParentDirectory() == folder)
      listed.insert(juce::File(path).getFileName().toLowerCase());
  }
  int fresh = 0;
  for (const auto& name : seen.names) fresh += listed.count(name) == 0 ? 1 : 0;
  return fresh;
}

void LibraryStore::refreshBlock(const std::string& blockId) {
  const auto playing = playingSource(blockId);
  if (playing.isEmpty() || !juce::File(playing).existsAsFile()) return (void)fail("The capture it plays is missing");
  loadCapture(juce::File(playing), blockId, /*again=*/true);
}

void LibraryStore::loadCapture(const juce::File& file, const std::string& target, bool again) {
  // Moved or deleted outside the plugin since the last scan: say so, and
  // scan again so the row goes.
  if (!file.exists()) {
    fail(file.getFileNameWithoutExtension() + " isn't there anymore");
    refresh();
    return;
  }
  // Another capture of the folder the block already plays: just that model,
  // as the block's own picker switches (no reading the folder again).
  if (const auto* block = chain_.state().findBlock(target);
      !again && !file.isDirectory() && block != nullptr && block->isTone() && block->tone.local)
    for (const auto& m : block->tone.models)
      if (m.sourcePath.isNotEmpty() && juce::File(m.sourcePath) == file && m.modelUrl.isNotEmpty()) {
        if (m.id != block->activeModelId) {
          auto* raw = new juce::DynamicObject();
          raw->setProperty("id", m.id);
          raw->setProperty("name", m.name);
          raw->setProperty("model_url", m.modelUrl);
          if (!chain_.switchModel(target, m.id, juce::var(raw))) break;  // the full load, then
        }
        auditioning(target);
        return;
      }
  // A load is a fresh tone in the block (no artwork, its own title): it is
  // dressed again, even with the very captures it showed before.
  forgetDressing(target);
  // The new block is whichever tone block wasn't there before (a swap keeps
  // the target's id).
  std::set<std::string> before;
  for (const auto* block : chain_.state().toneBlocks()) before.insert(block->blockId);
  const auto landedIn = [this, before, target](const juce::String& error) {
    if (error.isNotEmpty()) return (void)fail(error);
    std::string landed;
    for (const auto* block : chain_.state().toneBlocks())
      if (before.count(block->blockId) == 0) landed = block->blockId;
    if (landed.empty() && before.count(target) != 0) landed = target;
    if (landed.empty()) return;
    auditioning(landed);
    // Its look comes from the chain change (syncShown dresses a block whose
    // folder is new), not a second lookup from here.
  };
  // A file brings its folder: one block, the picked capture active and its
  // neighbours a step away in the model picker (read and checked off the
  // message thread: up to 300 files). A folder loads whole.
  if (file.isDirectory()) return landedIn(chain_.loadLocalTonePath(file, target));
  chain_.loadLocalToneInFolderAsync(file, target, scope_.wrap(landedIn));
}

void LibraryStore::artFor(const juce::File& picked, const std::string& blockId) {
  // A kept copy's artwork is its original's (a keep folder names no tone).
  const auto original = picked.existsAsFile() ? originalOf(picked.getFullPathName()) : juce::File();
  const juce::File& file = original != juce::File() ? original : picked;
  // This block now wants this file's look: a reply still out for what it
  // played before is dropped (whichever way this returns).
  const auto wanted = artWanted_[blockId] = file.getFullPathName();
  // The capture's folder and up to two above it, stopping at the Library's
  // own folders (a link root like "sorted" names no tone).
  const auto links = this->links();
  const auto top = root();
  juce::Array<juce::File> folders;
  for (auto folder = file.isDirectory() ? file : file.getParentDirectory();
       folders.size() < 3 && folder.getParentDirectory() != folder; folder = folder.getParentDirectory()) {
    if (folder == top || folder.getParentDirectory() == top || links.contains(folder) ||
        folder.getFileName() == "Captures")
      break;
    folders.add(folder);
  }
  if (folders.isEmpty()) return;
  // A picture you set for the folder (or one above it) wins.
  for (const auto& folder : folders)
    if (const auto picture = pictureFor(folder.getFullPathName()); picture != juce::File()) {
      auto* art = new juce::DynamicObject();
      art->setProperty("clear", true);  // no TONE3000 creator / link for it
      art->setProperty("image", juce::URL(picture).toString(false));
      chain_.setLocalToneArt(blockId, juce::var(art));
      return;
    }
  // A match found before answers at once, signed in or not (offline, a
  // session being refreshed). Asking the site needs a sign-in: hold the
  // block until there is one.
  const bool cached = art_.cachedAnswer(folders);
  if (!cached && !session_.authenticated()) {
    artAfterSignIn_[blockId] = file;
    return;
  }
  // The creator (a whole .nam read) only when the site is to be asked: a
  // cached answer needs none.
  const auto creator = cached ? juce::String() : ToneArt::creatorOf(file.isDirectory() ? file : file.getParentDirectory());
  const auto named = gearFromName(picked.getFileName());
  art_.lookup(folders, creator, scope_.wrap([this, blockId, wanted, named](std::optional<ToneArt::Art> art) {
    if (!art || artWanted_[blockId] != wanted) return;
    // The tone it matched names the block (a folder is often just "DI").
    auto look = art->toVar();
    // Its gear is the whole tone's; the capture's name ("[AMP]", "DI") speaks
    // for this one.
    if (named.isNotEmpty()) look.getDynamicObject()->setProperty("gear", named);
    if (art->title.isNotEmpty()) {
      look.getDynamicObject()->setProperty("block_title", art->title);
      artTitled_.insert(blockId);
    }
    chain_.setLocalToneArt(blockId, look);
  }));
}

// A reference resolves the way a browser pick does: the tone, then its
// models (to find the saved one, else the first), a fresh token for native,
// and the same add-or-swap landing as ToneLoadFlow.
void LibraryStore::loadToneRef(const LibraryToneRef& ref, const std::string& targetBlockId) {
  if (!session_.authenticated()) {
    fail("Sign in to load TONE3000 tones");
    return;
  }
  connection_.requireConnection([this, ref, targetBlockId] {
    session_.getTone(ref.toneId, scope_.wrap([this, ref, targetBlockId](Result<Tone> tone) {
      if (!tone) return (void)fail("Couldn't load " + ref.title);
      const Tone found = *tone;
      session_.listToneModels(ref.toneId, found.format,
                              scope_.wrap([this, ref, targetBlockId, found](Result<std::vector<Model>> models) {
        if (!models || models->empty()) return (void)fail("Couldn't load " + ref.title);
        Model pick = models->front();
        for (const auto& m : *models)
          if (m.id == ref.modelId) pick = m;
        const juce::String json = found.withModels({pick}).toJson();
        session_.ensureNativeAuth(scope_.wrap([this, ref, targetBlockId, json](const juce::String& error) {
          if (error.isNotEmpty()) return (void)fail(error);
          const auto* block = chain_.state().findBlock(targetBlockId);
          if (block != nullptr && block->isTone() && chain_.swapTone(targetBlockId, json)) {
            auditioning(targetBlockId);
            return;
          }
          const auto added = chain_.loadTone(json, targetBlockId);
          if (added.empty()) fail("Couldn't load " + ref.title);
          else auditioning(added);
        }));
      }));
    }));
  });
}

// Edits

juce::String LibraryStore::createFolder(const juce::String& parentPath, const juce::String& name) {
  if (const auto problem = backend_.libraryFolderNameProblem(name); problem.isNotEmpty()) {
    fail(problem);
    return {};
  }
  const auto folder = backend_.libraryCreateFolder(juce::File(parentPath), name);
  if (folder == juce::File()) {
    fail("Couldn't create the folder");
    return {};
  }
  open_.insert(parentPath);
  selected_ = folder.getFullPathName();
  refresh();
  return selected_;
}

bool LibraryStore::rename(const juce::String& path, const juce::String& name) {
  const auto* node = tree_->find(path);
  if (node == nullptr) return false;
  const bool yourLibrary = node->kind == LibraryNode::Kind::library && node->mine;
  if (!node->editable && !yourLibrary) return false;
  if (node->isContainer())
    if (const auto problem = backend_.libraryFolderNameProblem(name); problem.isNotEmpty()) return fail(problem);
  const bool wasOpen = isOpen(path);
  const auto renamed = backend_.libraryRename(node->file(), name);
  if (renamed == juce::File()) return fail("Couldn't rename " + node->name);
  remapPaths(path, renamed.getFullPathName());
  if (yourLibrary) prefs_.set(kOwnerPref, renamed.getFileName());
  if (wasOpen) open_.insert(renamed.getFullPathName());
  selected_ = renamed.getFullPathName();
  presets_.refresh();  // a preset (or a folder of them) renamed: the preset bar's name
  refresh();
  return true;
}

bool LibraryStore::remove(const juce::String& path) {
  const auto* node = tree_->find(path);
  if (node == nullptr || !node->removable) return false;
  if (node->favorites) {
    setFavorite(node->tone, false);
    return true;
  }
  const auto name = node->name;
  if (!busy_.insert(path).second) return fail("Still working on " + name);
  // A big folder (or one on a drive without a Recycle Bin) takes a while:
  // off the message thread.
  backend_.libraryRemoveAsync(node->file(), scope_.wrap([this, path, name](bool removed) {
    busy_.erase(path);
    if (!removed) return (void)fail("Couldn't delete " + name);
    forgetPaths(path);
    if (selected_ == path) selected_.clear();
    presets_.refresh();  // the preset list and the active preset may have moved
    chain_.refresh();
    refresh();
  }));
  return true;
}

bool LibraryStore::moveOrCopy(const juce::String& path, const juce::String& folderPath) {
  const auto* node = tree_->find(path);
  const auto* folder = tree_->find(folderPath);
  if (node == nullptr || folder == nullptr || !folder->writable) return false;
  if (!folder->accepts(*node))
    return fail(folder->favorites   ? juce::String("Only TONE3000 tones can be favorites")
                : folder->isPresets() ? juce::String("Presets only hold presets")
                                      : juce::String("Captures only hold captures and tones"));
  if (folder->favorites) {
    setFavorite(node->tone, true);
    return true;
  }
  if (!node->editable) return copyToMine(path, folderPath);
  const auto parent = tree_->parentOf(path);
  if (parent != nullptr && parent->path == folderPath) return false;  // dropped where it was
  const auto name = node->name, folderName = folder->name;
  if (!busy_.insert(path).second) return fail("Still working on " + name);
  // Across drives a move is a copy, a folder of thousands a long one: off
  // the message thread, saying so while it lasts.
  if (node->isContainer()) progress("Moving " + name + "...");
  backend_.libraryMoveAsync(node->file(), folder->file(), scope_.wrap([this, path, name, folderPath, folderName](juce::File moved) {
    busy_.erase(path);
    if (moved == juce::File()) return (void)fail("Couldn't move " + name);
    remapPaths(path, moved.getFullPathName());
    open_.insert(folderPath);
    selected_ = moved.getFullPathName();
    presets_.refresh();
    chain_.refresh();
    refresh();
    note("Moved to " + folderName);
  }));
  return true;
}

bool LibraryStore::putInOwnFolder(const juce::String& path) {
  const auto* node = tree_->find(path);
  if (node == nullptr || !node->editable || !node->onDisk() || node->isContainer()) return false;
  const auto file = node->file();
  const auto name = node->name;
  // Named as the file (its whole name: a display name may drop a tag).
  const auto folder = backend_.libraryCreateFolder(file.getParentDirectory(), file.getFileNameWithoutExtension());
  if (folder == juce::File()) return fail("Couldn't create the folder");
  const auto moved = backend_.libraryMove(file, folder);
  if (moved == juce::File()) {
    folder.deleteFile();  // still empty: don't leave it behind
    return fail("Couldn't move " + name);
  }
  remapPaths(path, moved.getFullPathName());
  open_.insert(file.getParentDirectory().getFullPathName());
  open_.insert(folder.getFullPathName());
  selected_ = moved.getFullPathName();
  presets_.refresh();
  chain_.refresh();
  refresh();
  note("Moved into " + folder.getFileName());
  return true;
}

bool LibraryStore::copyToMine(const juce::String& path, const juce::String& folderPath) {
  const auto* node = tree_->find(path);
  const auto* folder = tree_->find(folderPath);
  if (node == nullptr || folder == nullptr || !folder->writable || folder->favorites) return false;
  const auto name = node->name, folderName = folder->name;
  // A favorite or a TONE3000 account tone has no file: it becomes a
  // reference in the folder.
  if (!node->onDisk() && node->kind != LibraryNode::Kind::tone) return fail("Only files and TONE3000 tones can be copied");
  const auto landed = [this, name, folderPath, folderName](juce::File copied) {
    if (copied == juce::File()) return (void)fail("Couldn't copy " + name);
    open_.insert(folderPath);
    selected_ = copied.getFullPathName();
    presets_.refresh();
    refresh();
    note("Copied to " + folderName);
  };
  if (!node->onDisk()) {
    landed(backend_.libraryAddTone(folder->file(), node->tone.raw));
    return true;
  }
  // A folder can be a whole collection: copied off the message thread.
  if (node->isContainer()) progress("Copying " + name + "...");
  backend_.libraryCopyAsync(node->file(), folder->file(), scope_.wrap(landed));
  return true;
}

bool LibraryStore::saveRig(const juce::String& folderPath, const juce::String& name) {
  if (name.trim().isEmpty()) return false;
  const auto saved = backend_.savePresetToFolder(juce::File(folderPath), name.trim());
  if (!saved.isObject()) return fail("Couldn't save the preset");
  open_.insert(folderPath);
  presets_.refresh();
  chain_.refresh();
  refresh();
  note("Preset Saved");
  return true;
}

juce::var LibraryStore::refForBlock(const ChainItem& item) const {
  const auto& tone = item.tone;
  const ToneModelRef* model = tone.models.empty() ? nullptr : &tone.models.front();
  for (const auto& m : tone.models)
    if (m.id == item.activeModelId) model = &m;
  auto* toneObj = new juce::DynamicObject();
  toneObj->setProperty("id", tone.id);
  toneObj->setProperty("title", tone.title);
  toneObj->setProperty("gear", tone.gear);
  toneObj->setProperty("format", tone.format);
  if (tone.image.isNotEmpty()) toneObj->setProperty("image", tone.image);
  if (tone.url.isNotEmpty()) toneObj->setProperty("url", tone.url);
  if (tone.user) {
    auto* user = new juce::DynamicObject();
    user->setProperty("username", tone.user->username);
    toneObj->setProperty("user", user);
  }
  auto* modelObj = new juce::DynamicObject();
  modelObj->setProperty("id", model != nullptr ? model->id : item.activeModelId);
  modelObj->setProperty("name", model != nullptr ? model->name : juce::String());
  auto* ref = new juce::DynamicObject();
  ref->setProperty("tone", toneObj);
  ref->setProperty("model", modelObj);
  return juce::var(ref);
}

// Keeping

juce::String LibraryStore::keepTarget() const {
  const auto path = prefs_.get(kKeepPref);
  return path.isNotEmpty() && juce::File::isAbsolutePath(path) && juce::File(path).isDirectory() ? path : juce::String();
}

void LibraryStore::setKeepTarget(const juce::String& folderPath) {
  if (folderPath.isEmpty()) prefs_.remove(kKeepPref);
  else prefs_.set(kKeepPref, folderPath);
  notify();  // the drawer's strip and row mark
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  if (folderPath.isNotEmpty()) note("Keeping in " + juce::File(folderPath).getFileName());
}

bool LibraryStore::keep(const std::string& blockId) {
  const auto target = keepTarget();
  if (target.isEmpty()) return fail("Right-click a folder and choose Keep Here first");
  return keepInto(blockId, target);
}

namespace {
// A TONE3000 reference with another of the tone's models.
juce::var withModel(const juce::var& ref, const Model& model) {
  auto copy = juce::JSON::parse(juce::JSON::toString(ref));
  auto* m = new juce::DynamicObject();
  m->setProperty("id", model.id);
  m->setProperty("name", model.name);
  copy.getDynamicObject()->setProperty("model", juce::var(m));
  return copy;
}

// A kept TONE3000 capture's name: the tone, then the model (unless it only
// repeats the tone's title). Native makes it a legal file name.
juce::String siteCaptureName(const juce::String& title, const juce::String& modelName) {
  auto name = title.trim();
  if (modelName.trim().isNotEmpty() && modelName.trim().compareIgnoreCase(name) != 0)
    name = name.isEmpty() ? modelName.trim() : name + " - " + modelName.trim();
  return name;
}
}  // namespace

bool LibraryStore::keepInto(const std::string& blockId, const juce::String& folderPath) {
  const auto* item = chain_.state().findBlock(blockId);
  if (item == nullptr || !item->isTone()) return fail("That block is gone");
  if (item->tone.local || isFavoritesPath(folderPath)) return addBlock(blockId, folderPath, "Kept in");
  // A TONE3000 tone: the capture it plays, from the bytes native loaded.
  const auto ref = refForBlock(*item);
  const auto name = siteCaptureName(item->tone.title, ref["model"]["name"].toString());
  const juce::File folder(folderPath);
  if (const auto kept = backend_.libraryKeepModel(blockId, folder, name); kept != juce::File()) {
    keptLanded(kept, ref, folderPath);
    return true;
  }
  // Not in memory (still loading, or a failed load): download it, from the
  // block's model URL, else the one the tone's model list gives.
  juce::String url;
  for (const auto& m : item->tone.models)
    if (m.id == item->activeModelId) url = m.modelUrl;
  const bool ir = item->tone.format == "ir";
  const int toneId = item->tone.id, modelId = item->activeModelId;
  const auto download = [this, ir, folder, name, ref, folderPath](const juce::String& from) {
    session_.ensureNativeAuth(scope_.wrap([this, from, ir, folder, name, ref, folderPath](const juce::String& error) {
      if (error.isNotEmpty()) return (void)fail(error);
      backend_.libraryDownloadModel(from, ir, folder, name, scope_.wrap([this, ref, folderPath, name](juce::File kept) {
        if (kept == juce::File()) return (void)fail("Couldn't download " + name);
        keptLanded(kept, ref, folderPath);
      }));
    }));
  };
  if (url.isNotEmpty()) {
    download(url);
    return true;
  }
  session_.listToneModels(toneId, item->tone.format,
                          scope_.wrap([this, modelId, name, download](Result<std::vector<Model>> models) {
                            if (models)
                              for (const auto& m : *models)
                                if (m.id == modelId && m.modelUrl.isNotEmpty()) return download(m.modelUrl);
                            fail("Couldn't keep " + name);
                          }));
  return true;
}

void LibraryStore::keptLanded(const juce::File& copy, const juce::var& source, const juce::String& folderPath) {
  rememberKept(copy, source);
  open_.insert(folderPath);
  selected_ = copy.getFullPathName();
  refresh();
  note("Kept in " + displayName(*tree_, folderPath));
}

void LibraryStore::keepAs(const std::string& blockId, AddKind kind) {
  const auto target = keepTarget();
  if (target.isEmpty()) return beginAdd(blockId, kind);  // no keep folder: ask for one
  pendingAdd_ = blockId;
  pendingKind_ = kind;
  finishAdd(target);
}

// The TONE3000 tone a block stands for: its own, or for a capture kept from
// one, that one ({ tone, model }; void for neither).
juce::var LibraryStore::siteRefForBlock(const std::string& blockId) {
  const auto* item = chain_.state().findBlock(blockId);
  if (item == nullptr || !item->isTone()) return {};
  if (!item->tone.local) return refForBlock(*item);
  if (const auto site = siteOriginalForBlock(blockId)) return site->raw;
  return {};
}

void LibraryStore::keepLinkInto(const std::string& blockId, const juce::String& folderPath) {
  const auto ref = siteRefForBlock(blockId);
  if (!ref.isObject()) return (void)fail("Only TONE3000 tones can be kept as a reference");
  const auto added = backend_.libraryAddTone(juce::File(folderPath), ref);
  if (added == juce::File()) return (void)fail("Couldn't keep " + ref["tone"]["title"].toString());
  open_.insert(folderPath);
  selected_ = added.getFullPathName();
  refresh();
  note("Kept in " + displayName(*tree_, folderPath));
}

void LibraryStore::downloadInto(const std::string& blockId, const juce::String& target) {
  if (downloading_) return (void)fail("Already downloading a tone");
  const auto base = siteRefForBlock(blockId);
  if (!base.isObject()) return (void)fail("Only TONE3000 tones can be downloaded");
  if (!session_.authenticated()) return (void)fail("Sign in to download TONE3000 tones");
  const auto title = base["tone"]["title"].toString();
  const auto format = base["tone"]["format"].toString();
  const bool ir = format == "ir";
  downloading_ = true;
  progress("Downloading " + title);
  // Every model, one at a time, into a folder named after the tone.
  struct Download {
    juce::var base;
    juce::String title, target;
    bool ir = false;
    juce::File folder;
    std::vector<Model> models;
    size_t next = 0;
    int kept = 0, failed = 0, already = 0;
  };
  auto run = std::make_shared<Download>();
  run->base = base;
  run->title = title;
  run->target = target;
  run->ir = ir;
  const auto done = [this](const std::shared_ptr<Download>& d, const juce::String& error) {
    downloading_ = false;
    if (error.isNotEmpty()) return (void)fail(error);
    // Nothing came down (offline, or the downloads were refused): an error,
    // and the folder made for them goes if it is still empty.
    if (d->kept == 0 && d->failed > 0) {
      if (d->folder.isDirectory() && d->folder.findChildFiles(juce::File::findFilesAndDirectories, false).isEmpty())
        d->folder.deleteFile();
      refresh();
      return (void)fail("Couldn't download " + d->title);
    }
    open_.insert(d->target);
    open_.insert(d->folder.getFullPathName());
    selected_ = d->folder.getFullPathName();
    refresh();
    const auto captures = [](int n) { return juce::String(n) + (n == 1 ? " capture" : " captures"); };
    juce::StringArray notes;
    if (d->already > 0) notes.add(juce::String(d->already) + " already there");
    if (d->failed > 0) notes.add(juce::String(d->failed) + " failed");
    const auto extra = notes.isEmpty() ? juce::String() : " (" + notes.joinIntoString(", ") + ")";
    note(d->kept == 0 && d->failed == 0 ? "All " + captures(d->already) + " already in " + d->folder.getFileName()
                                        : "Kept " + captures(d->kept) + " in " + d->folder.getFileName() + extra);
  };
  auto step = std::make_shared<std::function<void(std::shared_ptr<Download>)>>();
  *step = [this, done, weakStep = std::weak_ptr<std::function<void(std::shared_ptr<Download>)>>(step)](
              std::shared_ptr<Download> d) {
    if (d->next >= d->models.size()) return done(d, {});
    const auto& model = d->models[d->next++];
    progress("Downloading " + d->title + " (" + juce::String(static_cast<int>(d->next)) + " of " +
         juce::String(static_cast<int>(d->models.size())) + ")");
    const auto ref = withModel(d->base, model);
    backend_.libraryDownloadModel(
        model.modelUrl, d->ir, d->folder, model.name,
        scope_.wrap([this, d, ref, weakStep](juce::File file) {
          if (file == juce::File()) ++d->failed;
          else {
            ++d->kept;
            rememberKept(file, ref);
          }
          if (const auto again = weakStep.lock()) (*again)(d);
        }));
  };
  const int toneId = base["tone"]["id"];
  session_.listToneModels(toneId, format, scope_.wrap([this, run, step, done](Result<std::vector<Model>> models) {
    if (!models || models->empty()) return done(run, "Couldn't list " + run->title + "'s captures");
    run->models = *models;
    session_.ensureNativeAuth(scope_.wrap([this, run, step, done](const juce::String& error) {
      if (error.isNotEmpty()) return done(run, error);
      // Again: into the folder it went before (one holding captures kept
      // from this tone), only what is missing from it.
      const int toneId = static_cast<int>(run->base["tone"]["id"]);
      std::set<int> have;
      for (const auto& dir : juce::File(run->target).findChildFiles(juce::File::findDirectories, false)) {
        std::set<int> inDir;
        for (const auto& file : dir.findChildFiles(juce::File::findFiles, false))
          if (const auto site = siteOriginalOf(file.getFullPathName()); site && site->toneId == toneId)
            inDir.insert(site->modelId);
        if (!inDir.empty()) {
          run->folder = dir;
          have = std::move(inDir);
          break;
        }
      }
      if (run->folder == juce::File()) run->folder = backend_.libraryCreateFolder(juce::File(run->target), run->title);
      if (run->folder == juce::File()) return done(run, "Couldn't create the folder");
      const auto all = run->models.size();
      run->models.erase(std::remove_if(run->models.begin(), run->models.end(),
                                       [&have](const Model& m) { return have.count(m.id) != 0; }),
                        run->models.end());
      run->already = static_cast<int>(all - run->models.size());
      (*step)(run);
      // The chain of downloads holds the step from here on.
      downloadStep_ = step;
    }));
  }));
}

// Kept links

juce::String LibraryStore::playingSource(const std::string& blockId) {
  const auto* item = chain_.state().findBlock(blockId);
  if (item == nullptr || !item->isTone() || !item->tone.local || item->tone.models.empty()) return {};
  const ToneModelRef* model = &item->tone.models.front();
  for (const auto& m : item->tone.models)
    if (m.id == item->activeModelId) model = &m;
  if (model->sourcePath.isNotEmpty()) return model->sourcePath;
  // A block from before blocks knew their file (an older project): the
  // Library file with its name and its bytes, found once.
  const auto stash = juce::URL(model->modelUrl).getLocalFile();
  const auto key = stash.getFullPathName() + "|" + model->name;
  if (const auto hit = stashSources_.find(key); hit != stashSources_.end()) return hit->second;
  if (!loaded_ || !stash.existsAsFile()) return {};
  const auto found = originalInLibrary(stash, model->name);
  return stashSources_[key] = found != juce::File() ? found.getFullPathName() : juce::String();
}

juce::File LibraryStore::keptFrom(const std::string& blockId) {
  const auto playing = playingSource(blockId);
  return playing.isEmpty() ? juce::File() : originalOf(playing);
}

juce::Array<juce::File> LibraryStore::keptCopies(const std::string& blockId) {
  // A TONE3000 tone: the captures kept from the model it plays.
  if (const auto* item = chain_.state().findBlock(blockId); item != nullptr && item->isTone() && !item->tone.local) {
    juce::Array<juce::File> copies;
    const auto index = prefs_.getJson(kKeptPref);  // held: the properties live in it
    if (auto* links = index.getDynamicObject())
      for (const auto& link : links->getProperties())
        if (link.value.isObject() && static_cast<int>(link.value["tone"]["id"]) == item->tone.id &&
            static_cast<int>(link.value["model"]["id"]) == item->activeModelId)
          if (const juce::File copy(link.name.toString()); copy.existsAsFile()) copies.addIfNotAlreadyThere(copy);
    return copies;
  }
  const auto playing = playingSource(blockId);
  return playing.isEmpty() ? juce::Array<juce::File>() : copiesOf(playing);
}

std::optional<LibraryToneRef> LibraryStore::siteOriginalOf(const juce::String& path) const {
  if (path.isEmpty()) return std::nullopt;
  const auto source = prefs_.getJson(kKeptPref).getProperty(juce::Identifier(path), {});
  if (!source.isObject() || !juce::File(path).existsAsFile()) return std::nullopt;
  auto ref = LibraryToneRef::parse(source);
  if (ref.toneId <= 0) return std::nullopt;
  return ref;
}

bool LibraryStore::hasOriginal(const std::string& blockId) {
  if (keptFrom(blockId) != juce::File()) return true;
  return siteOriginalOf(playingSource(blockId)).has_value();
}

void LibraryStore::useSiteOriginal(const juce::String& path) {
  const auto ref = siteOriginalOf(path);
  if (!ref) return;
  if (const auto local = localSiteOriginal(path); local != juce::File()) return loadCapture(local, auditionTarget());
  loadToneRef(*ref, auditionTarget());
}

juce::File LibraryStore::localSiteOriginal(const juce::String& path) const {
  const auto index = prefs_.getJson(kKeptPref);  // held: the properties live in it
  const auto link = index.getProperty(juce::Identifier(path), {});
  const int toneId = link["tone"]["id"], modelId = link["model"]["id"];
  if (toneId <= 0) return {};
  const auto hash = link["hash"].toString();
  // Every file of yours linked to that tone, by folder (to tell a whole
  // download from a single copy kept somewhere).
  std::map<juce::String, int> ofTone;
  std::vector<juce::File> sameModel;
  if (const auto* links = index.getDynamicObject())
    for (const auto& other : links->getProperties()) {
      if (other.name.toString() == path || static_cast<int>(other.value["tone"]["id"]) != toneId) continue;
      const juce::File file(other.name.toString());
      if (!file.existsAsFile()) continue;
      ++ofTone[file.getParentDirectory().getFullPathName()];
      const bool model = modelId > 0 && static_cast<int>(other.value["model"]["id"]) == modelId;
      if (model || (hash.isNotEmpty() && other.value["hash"].toString() == hash)) sameModel.push_back(file);
    }
  juce::File best;
  int most = 0;
  for (const auto& file : sameModel)
    if (const int n = ofTone[file.getParentDirectory().getFullPathName()]; n > most) {
      best = file;
      most = n;
    }
  if (best != juce::File() || hash.isEmpty()) return best;
  // Not linked (downloaded from the website into a linked folder): a folder
  // the artwork lookup matched to that tone, its file with those bytes (the
  // size first, so only a candidate is read).
  const auto size = hash.fromLastOccurrenceOf(":", false, false).getLargeIntValue();
  if (const auto art = prefs_.getJson(ToneArt::kCachePref); const auto* folders = art.getDynamicObject())
    for (const auto& entry : folders->getProperties()) {
      if (static_cast<int>(entry.value.getProperty("id", 0)) != toneId) continue;
      const juce::File folder(entry.name.toString());
      for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.nam;*.wav")) {
        if (file.getFullPathName() == path || file.getSize() != size) continue;
        juce::MemoryBlock bytes;
        if (file.loadFileAsData(bytes) && library_state::contentHash(bytes) == hash) return file;
      }
    }
  return {};
}

juce::File LibraryStore::knownOriginalOf(const juce::String& path) {
  if (!juce::File(path).existsAsFile()) return {};
  const auto link = prefs_.getJson(kKeptPref).getProperty(juce::Identifier(path), {});
  if (link.isObject()) return {};
  if (const auto source = link.toString(); juce::File::isAbsolutePath(source) && juce::File(source).existsAsFile())
    return juce::File(source);
  if (!loaded_ || tree_->inLinked(path)) return {};
  if (const auto hit = same_.find(path); hit != same_.end())
    for (const auto& same : hit->second)
      if (tree_->inLinked(same.getFullPathName())) return same;
  return {};
}

juce::File LibraryStore::originalOf(const juce::String& playing) {
  if (!juce::File(playing).existsAsFile()) return {};
  const auto link = prefs_.getJson(kKeptPref).getProperty(juce::Identifier(playing), {});
  if (link.isObject()) return {};  // kept from a TONE3000 tone (siteOriginalOf)
  const auto source = link.toString();
  if (juce::File::isAbsolutePath(source) && juce::File(source).existsAsFile()) return juce::File(source);
  // No link: the same capture in a linked collection, for one of yours.
  if (tree_->inLinked(playing)) return {};
  for (const auto& same : sameCaptures(playing))
    if (tree_->inLinked(same.getFullPathName())) {
      learnKept(juce::File(playing), same);
      return same;
    }
  return {};
}

void LibraryStore::learnKept(const juce::File& copy, const juce::File& original) {
  // A match by bytes is worked out per scan (and only when asked): recorded
  // as a link, it holds from then on (the drawer's rows, the library state).
  if (prefs_.getJson(kKeptPref).hasProperty(juce::Identifier(copy.getFullPathName()))) return;
  rememberKept(copy, original.getFullPathName());
  auto learned = prefs_.getJson(kLearnedPref);
  if (!learned.isObject()) learned = juce::var(new juce::DynamicObject());
  learned.getDynamicObject()->setProperty(juce::Identifier(copy.getFullPathName()), true);
  prefs_.setJson(kLearnedPref, learned);
  if (!learnedNotify_.pending()) learnedNotify_.start(50, [this] { notify(); });  // rows redraw as kept
}

juce::Array<juce::File> LibraryStore::copiesOf(const juce::String& playing) {
  juce::Array<juce::File> copies;
  // No links needed for these: the same capture in your own folders, for
  // one in a linked collection.
  if (tree_->inLinked(playing))
    for (const auto& same : sameCaptures(playing))
      if (!tree_->inLinked(same.getFullPathName())) {
        copies.addIfNotAlreadyThere(same);
        learnKept(same, juce::File(playing));
      }
  const auto index = prefs_.getJson(kKeptPref);  // held: the properties live in it
  if (auto* links = index.getDynamicObject())
    for (const auto& link : links->getProperties())
      if (link.value.toString() == playing) {
        const juce::File copy(link.name.toString());
        if (copy.existsAsFile() && copy.getFullPathName() != playing) copies.addIfNotAlreadyThere(copy);
      }
  return copies;
}

juce::Array<juce::File> LibraryStore::sameCaptures(const juce::String& path) {
  if (!loaded_) {
    if (scanGeneration_ == 0) refresh();  // nothing scanned yet (the drawer never opened): the cached listing is quick
    return {};
  }
  if (const auto hit = same_.find(path); hit != same_.end()) return hit->second;
  juce::Array<juce::File> same;
  const juce::File file(path);
  for (const auto* node : tree_->capturesNamed(file.getFileName()))
    if (node->path != path && node->file().getSize() == file.getSize() && node->file().hasIdenticalContentTo(file))
      same.add(node->file());
  return same_[path] = same;
}

juce::File LibraryStore::originalInLibrary(const juce::File& stash, const juce::String& name) {
  if (!loaded_) return {};
  juce::File fallback;
  for (const auto* node : tree_->capturesNamed(name + stash.getFileExtension()))
    if (node->file().getSize() == stash.getSize() && node->file().hasIdenticalContentTo(stash)) {
      if (tree_->inLinked(node->path)) return node->file();
      if (fallback == juce::File()) fallback = node->file();
    }
  return fallback;
}

// The title and artwork of blocks playing kept copies (see the header).
void LibraryStore::chainChanged(const ChainState& state) {
  // The block waiting for a folder is gone (removed, undone away): nothing
  // to add any more.
  if (pendingAdd_ && state.findBlock(*pendingAdd_) == nullptr) cancelAdd();
  findMovedFiles();
  syncShown();
}

void LibraryStore::findMovedFiles() {
  // The file a model plays: its source path, else (a block from an older
  // project, which recorded none) its model URL's file.
  const auto fileOf = [](const ToneModelRef& m) -> juce::String {
    if (juce::File::isAbsolutePath(m.sourcePath)) return m.sourcePath;
    if (const juce::URL url(m.modelUrl); url.isLocalFile()) return url.getLocalFile().getFullPathName();
    return {};
  };
  if (!loaded_) {
    // Nothing listed yet (the drawer not opened this session): a block with
    // a missing file starts the listing (the saved one is quick), and the
    // search runs when it lands (apply).
    if (scanGeneration_ == 0)
      for (const auto* block : chain_.state().toneBlocks())
        if (block->tone.local)
          for (const auto& m : block->tone.models)
            if (const auto path = fileOf(m); path.isNotEmpty() && !juce::File(path).existsAsFile()) {
              juce::Logger::writeToLog("[Library] A block's file isn't at its path (" + path +
                                       "): listing the Library to look for it");
              return refresh();
            }
    return;
  }
  // The missing files of each block, by the folder they were in.
  struct Missing {
    juce::File file;
    int id;
  };
  std::map<juce::String, std::vector<Missing>> byFolder;
  for (const auto* block : chain_.state().toneBlocks()) {
    if (!block->tone.local) continue;
    for (const auto& m : block->tone.models) {
      const auto path = fileOf(m);
      if (path.isEmpty() || !movedChecked_.insert(path).second) continue;
      const juce::File file(path);
      if (file.existsAsFile()) continue;
      auto drive = file;
      while (drive.getParentDirectory() != drive) drive = drive.getParentDirectory();
      if (!drive.exists()) continue;  // on a drive that isn't plugged in: not moved, just away
      byFolder[file.getParentDirectory().getFullPathName()].push_back({file, m.id});
    }
  }
  const auto bytesGive = [](const juce::File& file, int id) {
    juce::MemoryBlock bytes;
    const int got = file.loadFileAsData(bytes) ? library_state::localModelId(bytes.getData(), bytes.getSize()) : 0;
    if (got != id)
      juce::Logger::writeToLog("[Library]   not it (other bytes): " + file.getFullPathName());
    return got == id;
  };
  for (const auto& [folderPath, files] : byFolder) {
    juce::Logger::writeToLog("[Library] " + juce::String(static_cast<int>(files.size())) +
                             " file(s) a block plays aren't at their paths in " + folderPath + ": looking for them");
    // One file found by its name and bytes says where the folder went. The
    // same bytes can be in several places (a kept copy and its original):
    // the folder holding the most of the missing files is the one.
    const auto holds = [&files](const juce::File& folder) {
      int n = 0;
      for (const auto& missing : files) n += folder.getChildFile(missing.file.getFileName()).existsAsFile() ? 1 : 0;
      return n;
    };
    juce::File found;
    int best = 0;
    for (const auto& missing : files) {
      for (const auto* node : tree_->capturesNamed(missing.file.getFileName()))
        if (const int n = holds(node->file().getParentDirectory()); n > best && bytesGive(node->file(), missing.id)) {
          found = node->file();
          best = n;
        }
      if (found != juce::File()) break;
    }
    if (found == juce::File()) {
      juce::Logger::writeToLog("[Library]   none found in the Library by name and bytes");
      continue;
    }
    const juce::File from(folderPath), to = found.getParentDirectory();
    juce::Logger::writeToLog("[Library]   found in " + to.getFullPathName());
    // The others by their names there.
    bool all = true;
    for (const auto& missing : files) all = all && to.getChildFile(missing.file.getFileName()).existsAsFile();
    // The whole folder went (renamed, moved): one relink for it. Else each
    // file on its own (the folder is still there, or some went elsewhere).
    if (all && !from.exists()) {
      backend_.relinkLocalFiles(from, to);
      continue;
    }
    for (const auto& missing : files)
      if (const auto there = to.getChildFile(missing.file.getFileName()); there.existsAsFile())
        backend_.relinkLocalFiles(missing.file, there);
  }
}

void LibraryStore::syncShown() {
  // Setting a block's look changes the chain, which lands back here at once
  // (and replaces the state this walks): the walk runs over a copy, and a
  // change arriving mid-walk walks again after.
  if (syncingShown_) {
    resyncShown_ = true;
    return;
  }
  const juce::ScopedValueSetter<bool> walking(syncingShown_, true);
  do {
    resyncShown_ = false;
    syncShownOnce();
  } while (resyncShown_);
}

void LibraryStore::syncShownOnce() {
  struct Block {
    std::string id;
    juce::String title, models, gear;
  };
  std::vector<Block> blocks;
  std::set<std::string> present;
  for (const auto* block : chain_.state().toneBlocks()) {
    present.insert(block->blockId);
    if (!block->tone.local) {
      // A TONE3000 tone took the block: what it showed before is gone with
      // its tone, so the same capture coming back is dressed again.
      forgetDressing(block->blockId);
      continue;
    }
    // Ids alone can't tell a kept copy from its original (same bytes, same
    // id): the files they came from can.
    juce::String models;
    for (const auto& m : block->tone.models) models << m.id << ":" << m.sourcePath << "|";
    blocks.push_back({block->blockId, block->tone.title, models, block->tone.gear});
  }
  for (const auto& block : blocks) {
    const auto& id = block.id;
    const auto& models = block.models;
    auto it = shown_.find(id);
    // Other models: a new tone landed in the block (Source / Kept, a
    // drop), start over. Titles can't tell: the new one may be the very name
    // we set.
    if (it != shown_.end() && models != it->second.models) {
      shown_.erase(it);
      it = shown_.end();
    }
    const auto original = keptFrom(id);
    // Kept from a TONE3000 tone: dressed as the tone (one look for all its
    // models, so stepping through a downloaded tone keeps it).
    const auto site = original == juce::File() ? siteOriginalOf(playingSource(id)) : std::nullopt;
    const juce::String folder = original != juce::File() ? original.getParentDirectory().getFullPathName()
                                : site                    ? "tone3000:" + juce::String(site->toneId)
                                                          : juce::String();
    // Not a kept copy: its own folder's look (artwork, the matched tone's
    // title and gear), once per folder. A block restored with the project
    // gets it here too; a lookup already made answers from the cache.
    if (folder.isEmpty() && it == shown_.end()) {
      const auto playing = playingSource(id);
      if (playing.isEmpty()) continue;
      const juce::File file(playing);
      const auto own = file.getParentDirectory().getFullPathName();
      if (dressed_[id] != own) {
        dressed_[id] = own;
        artTitled_.erase(id);
        artFor(file, id);
      }
      // No TONE3000 match: a generic folder's block is named after its
      // capture (following the picker); a match, when one comes, names it
      // instead. The file's name ("[AMP]", "DI") gives its gear either way:
      // it speaks for this one capture, a matched tone for all of them.
      auto* look = new juce::DynamicObject();
      const auto name = file.getFileNameWithoutExtension();
      if (artTitled_.count(id) == 0 && genericFolderName(file.getParentDirectory().getFileName()) &&
          block.title != name)
        look->setProperty("block_title", name);
      if (const auto named = gearFromName(file.getFileName()); named.isNotEmpty() && block.gear != named)
        look->setProperty("gear", named);
      if (look->getProperties().size() > 0) chain_.setLocalToneArt(id, juce::var(look));
      else delete look;
      continue;
    }
    // The playing capture's name gives its gear (a copy is named as its
    // original), following the picker.
    juce::String named, nameGear;
    if (const auto playing = playingSource(id); playing.isNotEmpty())
      if (nameGear = named = gearFromName(juce::File(playing).getFileName()); named == block.gear) named = {};
    if (folder == (it == shown_.end() ? juce::String() : it->second.folder)) {
      if (named.isNotEmpty()) {
        auto* gear = new juce::DynamicObject();
        gear->setProperty("gear", named);
        chain_.setLocalToneArt(id, juce::var(gear));
      }
      continue;
    }

    auto* look = new juce::DynamicObject();
    if (named.isNotEmpty()) look->setProperty("gear", named);
    look->setProperty("clear", true);  // the keep folder's (or last original's) artwork goes
    if (folder.isEmpty()) {
      look->setProperty("block_title", it->second.ownTitle);
      artWanted_.erase(id);
      shown_.erase(it);
      chain_.setLocalToneArt(id, juce::var(look));
      continue;
    }
    if (site) {
      shown_[id] = {folder, site->title, it == shown_.end() ? block.title : it->second.ownTitle, models};
      const auto tone = site->raw["tone"];
      look->setProperty("block_title", site->title);
      if (const auto image = tone["image"].toString(); image.isNotEmpty()) look->setProperty("image", image);
      if (const auto user = tone["user"]["username"].toString(); user.isNotEmpty()) look->setProperty("username", user);
      if (const auto url = tone["url"].toString(); url.isNotEmpty()) look->setProperty("url", url);
      if (const auto gear = nameGear.isNotEmpty() ? nameGear : site->gear; gear.isNotEmpty())
        look->setProperty("gear", gear);
      artWanted_.erase(id);
      chain_.setLocalToneArt(id, juce::var(look));
      continue;
    }
    // Its original's folder name, or for a generic one ("DI") its own name.
    const auto originalFolder = original.getParentDirectory().getFileName();
    const auto title = genericFolderName(originalFolder) ? original.getFileNameWithoutExtension() : originalFolder;
    shown_[id] = {folder, title, it == shown_.end() ? block.title : it->second.ownTitle, models};
    look->setProperty("block_title", title);
    chain_.setLocalToneArt(id, juce::var(look));
    artFor(original, id);
  }
  for (auto it = shown_.begin(); it != shown_.end();)
    it = present.count(it->first) != 0 ? std::next(it) : shown_.erase(it);
  for (auto it = dressed_.begin(); it != dressed_.end();)
    it = present.count(it->first) != 0 ? std::next(it) : dressed_.erase(it);
  for (auto it = artTitled_.begin(); it != artTitled_.end();)
    it = present.count(*it) != 0 ? std::next(it) : artTitled_.erase(it);
}

void LibraryStore::forgetDressing(const std::string& blockId) {
  shown_.erase(blockId);
  dressed_.erase(blockId);
  artTitled_.erase(blockId);
}

void LibraryStore::rememberKept(const juce::File& copy, const juce::var& source) {
  if (source.isString() && copy.getFullPathName() == source.toString()) return;
  juce::DynamicObject::Ptr links = new juce::DynamicObject();
  const auto index = prefs_.getJson(kKeptPref);
  // A link whose copy is gone stays: moved by mistake it can be found again
  // (findMissing), deleted it can be forgotten (forgetMissing).
  if (auto* old = index.getDynamicObject())
    for (const auto& link : old->getProperties()) links->setProperty(link.name, link.value);
  // Kept on purpose now (learnKept marks its own again after this).
  if (auto learned = prefs_.getJson(kLearnedPref); learned.hasProperty(juce::Identifier(copy.getFullPathName()))) {
    auto copyOfLearned = juce::JSON::parse(juce::JSON::toString(learned));
    copyOfLearned.getDynamicObject()->removeProperty(juce::Identifier(copy.getFullPathName()));
    prefs_.setJson(kLearnedPref, copyOfLearned);
  }
  // A copy of a TONE3000 capture carries a hash of its bytes: what a shared
  // preset's block must match to keep them (LocalLibrary::shareArchive).
  auto value = source;
  if (source.isObject()) {
    juce::MemoryBlock bytes;
    if (copy.loadFileAsData(bytes) && bytes.getSize() > 0) {
      value = juce::JSON::parse(juce::JSON::toString(source));
      value.getDynamicObject()->setProperty("hash", library_state::contentHash(bytes));
    }
  }
  links->setProperty(juce::Identifier(copy.getFullPathName()), value);
  prefs_.setJson(kKeptPref, juce::var(links.get()));
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
}

namespace {
// `path` is `from` or inside it (`from` a folder).
bool atOrUnder(const juce::String& path, const juce::String& from) {
  return path == from || path.startsWith(from + juce::File::getSeparatorString());
}
juce::String movedTo(const juce::String& path, const juce::String& from, const juce::String& to) {
  return atOrUnder(path, from) ? to + path.substring(from.length()) : path;
}
}  // namespace

void LibraryStore::remapPaths(const juce::String& from, const juce::String& to) {
  remapKept(from, to);
  // Pictures and the artwork cache are keyed by the lower-cased path. A
  // picture's file moves too when it lives under the path (its library's
  // .t3kpictures, a library renamed).
  const auto lowFrom = from.toLowerCase(), lowTo = to.toLowerCase();
  juce::StringArray picturesMoved;  // folders (keys) whose picture file moved
  for (const char* pref : {kPicturesPref, ToneArt::kCachePref}) {
    const auto all = prefs_.getJson(pref);
    auto* old = all.getDynamicObject();
    if (old == nullptr) continue;
    juce::DynamicObject::Ptr moved = new juce::DynamicObject();
    bool changed = false;
    for (const auto& entry : old->getProperties()) {
      const auto key = entry.name.toString();
      const auto next = movedTo(key, lowFrom, lowTo);
      juce::var value = entry.value;
      if (std::string_view(pref) == kPicturesPref)
        if (const auto file = value.toString(); atOrUnder(file.toLowerCase(), lowFrom)) {
          value = to + file.substring(from.length());
          picturesMoved.add(next);
        }
      changed = changed || next != key || value != entry.value;
      moved->setProperty(juce::Identifier(next), value);
    }
    if (changed) prefs_.setJson(pref, juce::var(moved.get()));
  }
  // A block wearing one of those pictures still points at its old file:
  // dressed again from where things are now. What it plays is looked up
  // through the move too (its file may have moved with it).
  std::vector<std::pair<std::string, juce::File>> redress;
  if (!picturesMoved.isEmpty())
    for (const auto* block : chain_.state().toneBlocks()) {
      const auto playing = block->tone.local ? playingSource(block->blockId) : juce::String();
      if (playing.isEmpty()) continue;
      const auto now = atOrUnder(playing.toLowerCase(), lowFrom) ? to + playing.substring(from.length()) : playing;
      const auto original = originalOf(now);
      const juce::File source = original != juce::File() ? original : juce::File(now);
      const auto low = source.getFullPathName().toLowerCase();
      for (const auto& folder : picturesMoved)
        if (atOrUnder(low, folder)) {
          redress.emplace_back(block->blockId, source);
          break;
        }
    }
  for (const auto& [id, source] : redress) {
    auto* clear = new juce::DynamicObject();
    clear->setProperty("clear", true);
    chain_.setLocalToneArt(id, juce::var(clear));
    artFor(source, id);
  }
  // Folder orders: keyed by a folder's path (moved like the others), and a
  // folder renamed keeps its place in its parent's list under its new name.
  if (const auto orders = prefs_.getJson(kFolderOrderPref); const auto* old = orders.getDynamicObject()) {
    // As strings (a path from elsewhere may not read as a file here).
    const auto cut = [](const juce::String& p) { return juce::jmax(p.lastIndexOfChar('/'), p.lastIndexOfChar('\\')); };
    const auto parentKey = from.substring(0, cut(from)).toLowerCase();
    const bool renamed = juce::File(from).getParentDirectory() == juce::File(to).getParentDirectory();
    const auto oldName = from.substring(cut(from) + 1), newName = to.substring(cut(to) + 1);
    juce::DynamicObject::Ptr moved = new juce::DynamicObject();
    bool changed = false;
    for (const auto& entry : old->getProperties()) {
      const auto key = entry.name.toString();
      const auto next = movedTo(key, lowFrom, lowTo);
      juce::var value = entry.value;
      if (renamed && key == parentKey)
        if (const auto* names = entry.value.getArray()) {
          juce::Array<juce::var> list;
          for (const auto& name : *names) list.add(name.toString() == oldName ? juce::var(newName) : name);
          value = juce::var(list);
          changed = true;
        }
      changed = changed || next != key;
      moved->setProperty(juce::Identifier(next), value);
    }
    if (changed) prefs_.setJson(kFolderOrderPref, juce::var(moved.get()));
  }
  // The library order (a renamed library keeps its place).
  if (const auto order = prefs_.getJson(kOrderPref); const auto* paths = order.getArray()) {
    juce::Array<juce::var> next;
    bool changed = false;
    for (const auto& p : *paths) {
      const auto moved = movedTo(p.toString(), from, to);
      changed = changed || moved != p.toString();
      next.add(moved);
    }
    if (changed) prefs_.setJson(kOrderPref, juce::var(next));
  }
  // Open folders stay open where they went.
  std::set<juce::String> open;
  for (const auto& p : open_) open.insert(movedTo(p, from, to));
  open_ = std::move(open);
}

void LibraryStore::forgetPaths(const juce::String& path) {
  const auto low = path.toLowerCase();
  // Its pictures (and their files) and its artwork go.
  for (const char* pref : {kPicturesPref, ToneArt::kCachePref}) {
    const auto all = prefs_.getJson(pref);
    auto* old = all.getDynamicObject();
    if (old == nullptr) continue;
    juce::DynamicObject::Ptr kept = new juce::DynamicObject();
    bool changed = false;
    for (const auto& entry : old->getProperties()) {
      if (!atOrUnder(entry.name.toString(), low)) {
        kept->setProperty(entry.name, entry.value);
        continue;
      }
      changed = true;
      if (std::string_view(pref) == kPicturesPref)
        if (const juce::File picture(entry.value.toString());
            (picture.isAChildOf(picturesDir()) || picture.getParentDirectory().getFileName() == library_state::kPicturesFolder) &&
            !pictureInUse(picture, entry.name.toString()))
          picture.deleteFile();
    }
    if (changed) prefs_.setJson(pref, juce::var(kept.get()));
  }
  // Kept links of the copies that went, and to the originals that went.
  if (const auto index = prefs_.getJson(kKeptPref); auto* old = index.getDynamicObject()) {
    juce::DynamicObject::Ptr links = new juce::DynamicObject();
    bool changed = false;
    for (const auto& link : old->getProperties()) {
      const bool gone = atOrUnder(link.name.toString(), path) ||
                        (link.value.isString() && atOrUnder(link.value.toString(), path));
      if (gone) changed = true;
      else links->setProperty(link.name, link.value);
    }
    if (changed) {
      prefs_.setJson(kKeptPref, juce::var(links.get()));
      keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
    }
  }
  for (auto it = open_.begin(); it != open_.end();) it = atOrUnder(*it, path) ? open_.erase(it) : std::next(it);
  // Its folder orders (and those of the folders in it) go.
  if (const auto orders = prefs_.getJson(kFolderOrderPref); const auto* old = orders.getDynamicObject()) {
    juce::DynamicObject::Ptr kept = new juce::DynamicObject();
    bool changed = false;
    for (const auto& entry : old->getProperties()) {
      if (atOrUnder(entry.name.toString(), low)) changed = true;
      else kept->setProperty(entry.name, entry.value);
    }
    if (changed) prefs_.setJson(kFolderOrderPref, juce::var(kept.get()));
  }
}

void LibraryStore::remapKept(const juce::String& from, const juce::String& to) {
  // `from` itself, or anything inside it when it is a folder.
  const auto remap = [&](const juce::String& path) -> juce::String {
    if (path == from) return to;
    const auto inside = from + juce::File::getSeparatorString();
    return path.startsWith(inside) ? to + path.substring(from.length()) : path;
  };
  // The keep folder, if it (or a folder holding it) moved.
  if (const auto keep = prefs_.get(kKeepPref); keep.isNotEmpty() && remap(keep) != keep) {
    prefs_.set(kKeepPref, remap(keep));
    keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  }
  const auto index = prefs_.getJson(kKeptPref);
  auto* old = index.getDynamicObject();
  if (old == nullptr) return;
  juce::DynamicObject::Ptr links = new juce::DynamicObject();
  bool changed = false;
  for (const auto& link : old->getProperties()) {
    // A TONE3000 reference stays as it is: only files move.
    const auto copy = remap(link.name.toString());
    const auto source = link.value.isString() ? juce::var(remap(link.value.toString())) : link.value;
    changed = changed || copy != link.name.toString() || (link.value.isString() && source != link.value);
    links->setProperty(juce::Identifier(copy), source);
  }
  if (!changed) return;
  prefs_.setJson(kKeptPref, juce::var(links.get()));
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
}

juce::String LibraryStore::pictureFolderFor(const std::string& blockId) {
  const auto playing = playingSource(blockId);
  if (playing.isEmpty()) return {};
  const auto original = originalOf(playing);
  return (original != juce::File() ? original : juce::File(playing)).getParentDirectory().getFullPathName();
}

void LibraryStore::openOriginal(const std::string& blockId) {
  if (const auto site = siteOriginalOf(playingSource(blockId))) {
    if (const auto local = localSiteOriginal(playingSource(blockId)); local != juce::File())
      return loadCapture(local, blockId);
    return loadToneRef(*site, blockId);
  }
  const auto original = keptFrom(blockId);
  if (original == juce::File()) return (void)fail("The original is missing");
  loadCapture(original, blockId);
}

void LibraryStore::openKept(const std::string& blockId, const juce::File& copy) {
  if (!copy.existsAsFile()) return (void)fail("The kept copy is missing");
  loadCapture(copy, blockId);
}

bool LibraryStore::addBlock(const std::string& blockId, const juce::String& folderPath, const juce::String& toastVerb) {
  const auto* item = chain_.state().findBlock(blockId);
  if (item == nullptr || !item->isTone()) return fail("That block is gone");
  const auto& tone = item->tone;
  if (isFavoritesPath(folderPath)) {
    if (tone.local) return fail("Only TONE3000 tones can be favorites");
    setFavorite(LibraryToneRef::parse(refForBlock(*item)), true);
    return true;
  }
  const ToneModelRef* model = tone.models.empty() ? nullptr : &tone.models.front();
  for (const auto& m : tone.models)
    if (m.id == item->activeModelId) model = &m;

  const juce::File folder(folderPath);
  // What is there already: one the same comes back as itself ("Already in").
  const auto before = folder.findChildFiles(juce::File::findFiles, false);
  juce::File added;
  if (tone.local) {
    // The active capture, from the stash copy the block loaded.
    const auto source = model != nullptr ? juce::URL(model->modelUrl).getLocalFile() : juce::File();
    if (!source.existsAsFile()) return fail("Couldn't find the file for " + tone.title);
    // Named as the file it came from (a model's name can be cut at a dot
    // when read as a file name: "Gain 7.5" isn't "Gain 7").
    auto name = model->sourcePath.isNotEmpty() ? juce::File(model->sourcePath).getFileNameWithoutExtension()
                                               : model->name.isNotEmpty() ? model->name : tone.title;
    if (name.endsWithIgnoreCase(".nam") || name.endsWithIgnoreCase(".wav")) name = name.dropLastCharacters(4);
    added = backend_.libraryAddCapture(folder, source, juce::File::createLegalFileName(name));
    const auto from = model->sourcePath.isNotEmpty() ? juce::File(model->sourcePath) : originalInLibrary(source, model->name);
    if (added != juce::File() && from != juce::File()) {
      // A copy of one kept from a TONE3000 tone is that tone's too.
      if (const auto site = siteOriginalOf(from.getFullPathName())) rememberKept(added, site->raw);
      else rememberKept(added, from.getFullPathName());
    }
  } else {
    added = backend_.libraryAddTone(folder, refForBlock(*item));
  }
  if (added == juce::File()) return fail("Couldn't add " + tone.title);
  const auto folderName = displayName(*tree_, folderPath);
  open_.insert(folderPath);
  selected_ = added.getFullPathName();
  refresh();
  note((before.contains(added) ? juce::String("Already in") : toastVerb) + " " + folderName);
  return true;
}

void LibraryStore::beginAdd(const std::string& blockId, AddKind kind) {
  pendingAdd_ = blockId;
  pendingKind_ = kind;
  notify();
  if (onRequestShow) onRequestShow();
}

void LibraryStore::cancelAdd() {
  if (!pendingAdd_) return;
  pendingAdd_.reset();
  notify();
}

void LibraryStore::finishAdd(const juce::String& folderPath) {
  if (!pendingAdd_) return;
  const auto blockId = *pendingAdd_;
  const auto kind = std::exchange(pendingKind_, AddKind::add);
  cancelAdd();
  // The folder a first KEEP was given is where KEEP keeps from now on (the
  // drawer's strip says so, with Stop): one pick, not one per keep.
  if (kind != AddKind::add && keepTarget().isEmpty() && !isFavoritesPath(folderPath)) {
    prefs_.set(kKeepPref, folderPath);
    keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  }
  switch (kind) {
    case AddKind::keep: keepInto(blockId, folderPath); break;
    case AddKind::keepLink: keepLinkInto(blockId, folderPath); break;
    case AddKind::download: downloadInto(blockId, folderPath); break;
    case AddKind::add: addBlock(blockId, folderPath); break;
  }
}

// Files

bool LibraryStore::canReveal() { return kDesktopFiles; }

void LibraryStore::reveal(const juce::String& path) {
  juce::File file(path);
  // Your library before its folder exists: show where it will be.
  while (!file.exists() && file.getParentDirectory() != file) file = file.getParentDirectory();
  file.revealToUser();
}

void LibraryStore::launch(std::unique_ptr<juce::FileChooser> chooser, int flags,
                          std::function<void(const juce::FileChooser&)> done) {
  if (chooser_ != nullptr) return;
  chooser_ = std::move(chooser);
  chooser_->launchAsync(flags, scope_.wrap([this, done = std::move(done)](const juce::FileChooser& fc) {
    done(fc);
    // Release the chooser once its callback unwinds (it is the caller).
    juce::MessageManager::callAsync(scope_.wrap([this] { chooser_.reset(); }));
  }));
}

void LibraryStore::exportItem(const juce::String& path) {
  const auto* node = tree_->find(path);
  if (node == nullptr) return;
  const auto name = node->name;
  const auto file = node->file();
  auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile(juce::File::createLegalFileName(name) + ".t3klibrary");
  launch(std::make_unique<juce::FileChooser>("Export " + name, start, "*.t3klibrary"),
         juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
             juce::FileBrowserComponent::warnAboutOverwriting,
         [this, name, file](const juce::FileChooser& fc) {
           auto target = fc.getResult();
           if (target == juce::File()) return;
           if (!target.hasFileExtension(".t3klibrary")) target = target.withFileExtension(".t3klibrary");
           progress("Exporting " + name + "...");
           saveState();  // the archive carries it
           backend_.libraryExportAsync(file, target, scope_.wrap([this, name](bool ok) {
             if (ok) note("Exported " + name);
             else fail("Couldn't export " + name);
           }));
         });
}

void LibraryStore::shareItem(const juce::String& path) {
  const auto* node = tree_->find(path);
  if (node == nullptr) return;
  const auto name = node->name;
  auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile(juce::File::createLegalFileName(name + " (shared)") + ".t3klibrary");
  launch(std::make_unique<juce::FileChooser>("Export " + name + " for Sharing", start, "*.t3klibrary"),
         juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
             juce::FileBrowserComponent::warnAboutOverwriting,
         [this, path](const juce::FileChooser& fc) {
           auto target = fc.getResult();
           if (target == juce::File()) return;
           if (!target.hasFileExtension(".t3klibrary")) target = target.withFileExtension(".t3klibrary");
           shareItemTo(path, target);
         });
}

void LibraryStore::shareItemTo(const juce::String& path, const juce::File& archive) {
  const auto* node = tree_->find(path);
  if (node == nullptr) return;
  if (sharing_) return (void)fail("Already exporting");
  const auto name = node->name;
  const auto item = node->file();
  // Every capture it holds, by folder (its linked folders too: the tree
  // lists them in your library).
  std::map<juce::String, juce::StringArray> byFolder;
  std::function<void(const LibraryNode&)> walk = [&](const LibraryNode& n) {
    if (n.kind == LibraryNode::Kind::capture && n.onDisk())
      byFolder[juce::File(n.path).getParentDirectory().getFullPathName()].add(n.path);
    for (const auto& child : n.children) walk(child);
  };
  walk(*node);
  // What is known already: copies kept from a tone (any: a preset may play
  // one from elsewhere). Their recorded content hash is what lets a preset
  // block keep its bytes; a folder match only ever makes a link.
  using Share = ShareRun;
  auto share = std::make_shared<Share>();
  share->item = item;
  share->archive = archive;
  share->name = name;
  share->signedOut = !session_.authenticated();
  // The Library's fixed folders name no tone: its root, each library, their
  // halves; and nothing above what is shared or a linked folder.
  const auto base = root();
  share->stops.add(base);
  if (base.isDirectory())
    for (const auto& library : base.findChildFiles(juce::File::findDirectories, false)) {
      share->stops.add(library);
      for (const char* half : {"Captures", "Local", "Presets"}) share->stops.add(library.getChildFile(half));
    }
  for (auto above = item.getParentDirectory(); above != above.getParentDirectory(); above = above.getParentDirectory())
    share->stops.add(above);
  for (const auto& link : links()) share->stops.add(link.getParentDirectory());
  if (const auto index = prefs_.getJson(kKeptPref); const auto* links = index.getDynamicObject())
    for (const auto& link : links->getProperties())
      if (link.value.isObject() && static_cast<int>(link.value["tone"]["id"]) > 0) share->refs->setProperty(link.name, link.value);
  for (auto& [folder, files] : byFolder) {
    juce::StringArray rest;
    for (const auto& file : files)
      if (!share->refs->hasProperty(juce::Identifier(file))) rest.add(file);
    if (!rest.isEmpty()) share->byFolder[folder] = rest;
  }
  sharing_ = true;
  shareRun_ = share;  // what keeps it going (Cancel lets it go)
  progress("Exporting " + name + " for sharing...");
  const auto finish = [this](const std::shared_ptr<Share>& s) {
    setShareStatus({});
    backend_.libraryShareAsync(s->item, s->archive, juce::var(s->refs.get()), scope_.wrap([this, s](juce::var summary) {
      sharing_ = false;
      shareRun_.reset();
      if (!summary.isObject()) return (void)fail("Couldn't export " + s->name);
      if (static_cast<bool>(summary["empty"]))
        return (void)fail("Nothing to share in " + s->name + (s->signedOut ? " (sign in to find TONE3000 captures)" : ""));
      // Short: one line. The details are in library.md.
      const int links = summary["links"], leftOut = summary["leftOut"];
      juce::StringArray parts;
      parts.add(juce::String(links) + (links == 1 ? " tone" : " tones") + " as links");
      if (leftOut > 0) parts.add(juce::String(leftOut) + " local-only left out");
      if (s->signedOut && leftOut > 0) parts.add("sign in to find more");
      else if (s->unchecked > 0) parts.add("export again to check more");
      note("Shared " + s->name + ": " + parts.joinIntoString(", "));
    }));
  };
  if (share->byFolder.empty()) return finish(share);
  // A folder matched: its captures go as links to that tone. The match is
  // its creator's: a folder whose captures name someone else (a reamp of your
  // own beside a download) isn't that tone.
  const auto matched = [](Share& s, const juce::String& folder, const juce::String& creator,
                          const std::optional<ToneArt::Art>& art) {
    if (!art || art->toneId <= 0) return;
    if (creator.isNotEmpty() && ToneArt::normalized(creator) != ToneArt::normalized(art->username)) return;
    auto* tone = new juce::DynamicObject();
    tone->setProperty("id", art->toneId);
    tone->setProperty("title", art->title);
    tone->setProperty("gear", art->gear);
    auto* user = new juce::DynamicObject();
    user->setProperty("username", art->username);
    tone->setProperty("user", juce::var(user));
    auto* ref = new juce::DynamicObject();
    ref->setProperty("tone", juce::var(tone));
    ref->setProperty("model", juce::var(new juce::DynamicObject()));
    for (const auto& file : s.byFolder[folder]) s.refs->setProperty(juce::Identifier(file), juce::var(ref));
  };
  // Which folders are TONE3000 tones: the creator from each folder's first
  // capture (whole files read, off the message thread), then ToneArt's
  // match. What the cache knows answers at once; the rest are asked one at a
  // time, a second apart (Cancel stops it), only for folders whose captures
  // name their creator, at most kMaxShareLookups per export (asking again
  // goes on from there: the answers are cached).
  std::vector<juce::String> folders;
  for (const auto& [folder, files] : share->byFolder) folders.push_back(folder);
  // Wrapped here, on the message thread: the worker only posts it.
  const std::weak_ptr<Share> weak = share;
  auto resume = scope_.wrap([this, weak, finish, matched](std::map<juce::String, juce::String> creators) {
    const auto s = weak.lock();
    if (s == nullptr) return;  // cancelled
    for (const auto& [folder, creator] : creators) {
      // The folder; above it only from a generic one ("DI" in the tone's
      // folder), up to two, never into the Library's own folders.
      juce::Array<juce::File> candidates;
      for (auto at = juce::File(folder); candidates.size() < 3 && !s->stops.contains(at); at = at.getParentDirectory()) {
        candidates.add(at);
        if (!genericFolderName(at.getFileName())) break;
      }
      if (candidates.isEmpty()) continue;
      if (art_.cachedAnswer(candidates)) {
        art_.lookup(candidates, creator, [&](std::optional<ToneArt::Art> art) { matched(*s, folder, creator, art); });
        continue;
      }
      if (creator.isEmpty() || s->signedOut) {
        ++s->unchecked;
        continue;
      }
      s->toAsk.push_back({folder, creator, candidates});
    }
    if (s->toAsk.size() > kMaxShareLookups) {
      s->unchecked += static_cast<int>(s->toAsk.size() - kMaxShareLookups);
      s->toAsk.resize(kMaxShareLookups);
    }
    s->next = [this, weak, finish, matched] {
      const auto run = weak.lock();
      if (run == nullptr) return;
      if (run->asked >= run->toAsk.size()) return finish(run);
      const auto ask = run->toAsk[run->asked++];
      setShareStatus("Checking folders on TONE3000: " + juce::String(static_cast<int>(run->asked)) + " of " +
                     juce::String(static_cast<int>(run->toAsk.size())));
      art_.lookup(ask.candidates, ask.creator, scope_.wrap([weak, ask, matched](std::optional<ToneArt::Art> art) {
        if (const auto again = weak.lock()) {
          matched(*again, ask.folder, ask.creator, art);
          again->next();
        }
      }));
    };
    s->next();
  });
  scanPool_.addJob([folders, resume, stop = share->stop, alive = scanStop_] {
    std::map<juce::String, juce::String> creators;
    for (const auto& folder : folders) {
      if (alive->load() || stop->load()) return;
      creators[folder] = ToneArt::creatorOf(juce::File(folder));
    }
    juce::MessageManager::callAsync([resume, creators] { resume(creators); });
  });
}

void LibraryStore::setShareStatus(const juce::String& status) {
  if (status == shareStatus_) return;
  shareStatus_ = status;
  if (onShareStatus) onShareStatus();
}

void LibraryStore::cancelShare() {
  if (!sharing_ || shareRun_ == nullptr) return;
  shareRun_->stop->store(true);  // the creators still being read: no more
  shareRun_.reset();  // a lookup under way answers into nothing
  sharing_ = false;
  setShareStatus({});
  note("Export cancelled");
}

void LibraryStore::importArchive() {
  launch(std::make_unique<juce::FileChooser>("Import Library", juce::File(), "*.t3klibrary"),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
         [this](const juce::FileChooser& fc) {
           if (fc.getResult() != juce::File()) importFile(fc.getResult());
         });
}

void LibraryStore::importFile(const juce::File& archive) {
  pushLocation();
  progress("Importing " + archive.getFileName() + "...");
  backend_.libraryImportAsync(archive, scope_.wrap([this, archive](juce::File landed) {
    if (landed == juce::File()) return (void)fail("Couldn't import " + archive.getFileName());
    // Show what arrived: open everything above it.
    for (auto f = landed; f.isAChildOf(root()); f = f.getParentDirectory()) open_.insert(f.getFullPathName());
    selected_ = landed.getFullPathName();
    presets_.refresh();
    refresh();
    note("Imported " + landed.getFileNameWithoutExtension());
  }));
}

void LibraryStore::addFiles(const juce::StringArray& paths, const juce::String& folderPath) {
  if (folderPath.isEmpty()) {
    // Nowhere in particular: each file to its kind's half.
    juce::StringArray rigs, rest;
    for (const auto& path : paths) (juce::File(path).hasFileExtension(".t3kpreset") ? rigs : rest).add(path);
    const auto* captures = capturesRoot();
    const auto* presets = presetsRoot();
    const auto capturesPath = captures != nullptr ? captures->path : juce::String();
    const auto presetsPath = presets != nullptr ? presets->path : juce::String();
    if (!rigs.isEmpty() && presetsPath.isNotEmpty()) addFiles(rigs, presetsPath);
    if (!rest.isEmpty() && capturesPath.isNotEmpty()) addFiles(rest, capturesPath);
    return;
  }
  const auto* folder = tree_->find(folderPath);
  if (folder == nullptr || !folder->writable || folder->favorites) return;
  const auto folderName = folder->name;
  // Archives import; folders are the drawer's to ask about (link or copy);
  // anything that isn't Library content is said, not quietly dropped.
  juce::Array<juce::File> files;
  int notLibrary = 0;
  for (const auto& path : paths) {
    const juce::File file(path);
    if (file.isDirectory()) continue;
    if (file.hasFileExtension(".t3klibrary")) importFile(file);
    else if (file.hasFileExtension(".nam;.wav;.t3kpreset;.t3ktone")) files.add(file);
    else ++notLibrary;
  }
  if (files.isEmpty()) {
    if (notLibrary > 0) fail("Only .nam, .wav, preset and tone files can be added");
    return;
  }
  if (files.size() > 3) progress("Adding " + juce::String(files.size()) + " files to " + folderName + "...");
  backend_.libraryCopyFilesAsync(files, juce::File(folderPath), scope_.wrap([this, folderPath, folderName, notLibrary](juce::var result) {
    const int copied = result["copied"], skipped = static_cast<int>(result["skipped"]) + notLibrary;
    if (copied == 0) return (void)fail("Couldn't add " + juce::String(skipped) + (skipped == 1 ? " file" : " files"));
    open_.insert(folderPath);
    selected_ = result["last"].toString();
    presets_.refresh();
    refresh();
    note((copied == 1 ? juce::String("Added to ") : "Added " + juce::String(copied) + " to ") + folderName +
         (skipped > 0 ? " (" + juce::String(skipped) + " skipped)" : juce::String()));
  }));
}

void LibraryStore::chooseRoot() {
  launch(std::make_unique<juce::FileChooser>("Choose Library Folder", root()),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
         [this](const juce::FileChooser& fc) {
           const auto dir = fc.getResult();
           if (dir == juce::File() || dir == root()) return;
           prefs_.set(kRootPref, dir.getFullPathName());
           open_.clear();
           selected_.clear();
           tree_ = std::make_shared<LibraryTree>();
           loaded_ = false;  // reopens your library on the refresh (a fresh view,
           viewRestored_ = false;  // not the old location's)
           notify();
           refresh();
         });
}

}  // namespace t3k::ui
