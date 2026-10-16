// LibraryStore: the library state files (LibraryState.h). The prefs hold
// the working copy, by absolute path; each library folder holds its own
// part of it, by paths relative to that folder.
#include "LibraryState.h"
#include "LibraryStore.h"

namespace t3k::ui {

namespace ls = library_state;

namespace {
bool same(const juce::var& a, const juce::var& b) { return juce::JSON::toString(a, true) == juce::JSON::toString(b, true); }

juce::DynamicObject::Ptr copyOf(const juce::var& object) {
  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  if (const auto* o = object.getDynamicObject())
    for (const auto& p : o->getProperties()) out->setProperty(p.name, p.value);
  return out;
}

// Where an older build kept folder pictures (and setPicture still does for a
// folder no library holds).
juce::File legacyPicturesDir() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("LibraryPictures");
}
}  // namespace

juce::Array<juce::File> LibraryStore::stateLibraries() const {
  juce::Array<juce::File> out;
  const auto base = root();
  if (const auto own = base.getChildFile(owner()); own.isDirectory()) out.add(own);
  if (base.isDirectory())
    for (const auto& dir : base.findChildFiles(juce::File::findDirectories, false))
      if (!dir.getFileName().startsWithChar('.') && !out.contains(dir)) out.add(dir);
  return out;
}

juce::File LibraryStore::stateHome(const juce::String& path, bool lowerCased) const {
  return stateHome(path, lowerCased, stateLibraries(), links());
}

juce::File LibraryStore::stateHome(const juce::String& path, bool lowerCased, const juce::Array<juce::File>& libraries,
                                   const juce::Array<juce::File>& linked) const {
  const auto inside = [&](const juce::File& dir) {
    return lowerCased ? ls::lowerRelative(path, dir).isNotEmpty() : ls::relative(juce::File(path), dir).isNotEmpty();
  };
  const auto base = root();
  if (inside(base)) {
    for (const auto& library : libraries)
      if (inside(library)) return library;
    return {};
  }
  // Outside the Library: a linked folder's, or the Presets mount's, in yours.
  const auto own = base.getChildFile(owner());
  if (!own.isDirectory()) return {};
  for (const auto& link : linked)
    if (inside(link)) return own;
  if (const auto* presets = presetsRoot(); presets != nullptr && inside(juce::File(presets->path))) return own;
  return {};
}

bool LibraryStore::pictureInUse(const juce::File& picture, const juce::String& exceptKey) const {
  if (const auto all = prefs_.getJson(kPicturesPref); const auto* pictures = all.getDynamicObject())
    for (const auto& entry : pictures->getProperties())
      if (entry.name.toString() != exceptKey && juce::File(entry.value.toString()) == picture) return true;
  return false;
}

void LibraryStore::prefChanged(const juce::String& key) {
  if (loadingState_) return;
  for (const char* pref : {kKeptPref, kPicturesPref, kKeepPref, kOrderPref, kLinksPref, kFolderOrderPref})
    if (key == pref) return stateSave_.start(500, [this] { saveState(); });
}

namespace {
// A picture file name a state file may name: a plain name, nothing that
// climbs or roots ("..", "C:x.png", a hidden one).
bool plainPictureName(const juce::String& name) {
  return name.isNotEmpty() && !name.startsWithChar('.') && name == juce::File::createLegalFileName(name) &&
         !name.containsAnyOf("/\\:");
}
}  // namespace

bool LibraryStore::loadState() {
  const juce::ScopedLock fileLock(ls::fileLock());
  const auto base = root();
  const auto own = base.getChildFile(owner());
  auto kept = copyOf(prefs_.getJson(kKeptPref));
  auto pictures = copyOf(prefs_.getJson(kPicturesPref));
  auto folderOrder = copyOf(prefs_.getJson(kFolderOrderPref));
  bool keptChanged = false, picturesChanged = false, keepChanged = false, orderChanged = false, linksChanged = false,
       foldersChanged = false;
  for (const auto& library : stateLibraries()) {
    const auto path = library.getFullPathName();
    const auto stamp = ls::stamp(library);
    // A picture it left unread (not synced yet) that has arrived since: read again.
    const auto arrived = [&] {
      if (const auto* waiting = stateSkipped_[path]["pictures"].getDynamicObject())
        for (const auto& entry : waiting->getProperties())
          if (plainPictureName(entry.value.toString()) &&
              ls::picturesOf(library).getChildFile(entry.value.toString()).existsAsFile())
            return true;
      return false;
    };
    if (const auto seen = stateSeen_.find(path); seen != stateSeen_.end() && seen->second == stamp && !arrived()) continue;
    stateSeen_[path] = stamp;
    if (stamp < 0) continue;
    const auto state = ls::read(library);
    // What the file said last time this instance read or wrote it: an entry
    // gone from it since was deleted or moved elsewhere (another machine,
    // another host) and goes here too. Without one (first read), nothing is
    // taken as deleted.
    const auto before = stateSnapshot_.count(path) != 0 ? stateSnapshot_[path] : juce::var();
    stateSnapshot_[path] = state;
    auto skipped = juce::var(new juce::DynamicObject());  // entries left in the file for later
    const auto skip = [&](const char* section, const juce::NamedValueSet::NamedValue& entry) {
      if (!skipped[section].isObject()) skipped.getDynamicObject()->setProperty(section, juce::var(new juce::DynamicObject()));
      skipped[section].getDynamicObject()->setProperty(entry.name, entry.value);
    };
    // Paths outside the Library are yours only (someone else's are theirs).
    const auto readable = [&](const juce::String& key) { return library == own || !juce::File::isAbsolutePath(key); };
    const auto sourceOf = [&](const juce::var& from) -> juce::var {
      if (from["tone"].isObject()) return from;
      if (const auto at = ls::resolve(library, from["source"].toString()); at != juce::File()) return at.getFullPathName();
      if (const auto at = ls::resolve(base, from["source_root"].toString()); at != juce::File()) return at.getFullPathName();
      if (const auto path = from["source_path"].toString(); juce::File::isAbsolutePath(path)) return path;
      return {};
    };
    if (const auto* links = state["kept"].getDynamicObject())
      for (const auto& link : links->getProperties()) {
        if (!readable(link.name.toString())) {
          skip("kept", link);
          continue;
        }
        const auto copy = ls::resolve(library, link.name.toString());
        const auto source = sourceOf(link.value);
        if (copy == juce::File() || copy == library || source.isVoid()) continue;
        const juce::Identifier id(copy.getFullPathName());
        if (same(kept->getProperty(id), source)) continue;
        kept->setProperty(id, source);
        keptChanged = true;
      }
    if (const auto* gone = before["kept"].getDynamicObject())
      for (const auto& link : gone->getProperties())
        if (!state["kept"].hasProperty(link.name) && readable(link.name.toString()))
          if (const auto copy = ls::resolve(library, link.name.toString()); copy != juce::File())
            if (kept->hasProperty(juce::Identifier(copy.getFullPathName()))) {
              kept->removeProperty(juce::Identifier(copy.getFullPathName()));
              keptChanged = true;
            }
    if (const auto* folders = state["pictures"].getDynamicObject())
      for (const auto& entry : folders->getProperties()) {
        const auto key = readable(entry.name.toString()) ? ls::lowerResolve(library, entry.name.toString()) : juce::String();
        const auto name = entry.value.toString();
        const auto picture = ls::picturesOf(library).getChildFile(name);
        // Not there yet (a synced folder can bring the file after the state):
        // left in the file for when it is.
        if (key.isEmpty() || !plainPictureName(name) || !picture.isAChildOf(ls::picturesOf(library)) ||
            !picture.existsAsFile()) {
          skip("pictures", entry);
          continue;
        }
        const juce::Identifier id(key);
        if (pictures->getProperty(id).toString() == picture.getFullPathName()) continue;
        pictures->setProperty(id, picture.getFullPathName());
        picturesChanged = true;
      }
    if (const auto* gone = before["pictures"].getDynamicObject())
      for (const auto& entry : gone->getProperties())
        if (!state["pictures"].hasProperty(entry.name) && readable(entry.name.toString()))
          if (const auto key = ls::lowerResolve(library, entry.name.toString()); key.isNotEmpty())
            if (pictures->hasProperty(juce::Identifier(key))) {
              pictures->removeProperty(juce::Identifier(key));
              picturesChanged = true;
            }
    // Folder orders: a folder's path (relative, or absolute in yours) -> its
    // folders' names.
    if (const auto* orders = state["folders"].getDynamicObject())
      for (const auto& entry : orders->getProperties()) {
        const auto key = readable(entry.name.toString()) ? ls::lowerResolve(library, entry.name.toString()) : juce::String();
        if (key.isEmpty() || !entry.value.isArray()) {
          skip("folders", entry);
          continue;
        }
        const juce::Identifier id(key);
        if (same(folderOrder->getProperty(id), entry.value)) continue;
        folderOrder->setProperty(id, entry.value);
        foldersChanged = true;
      }
    if (const auto* gone = before["folders"].getDynamicObject())
      for (const auto& entry : gone->getProperties())
        if (!state["folders"].hasProperty(entry.name) && readable(entry.name.toString()))
          if (const auto key = ls::lowerResolve(library, entry.name.toString()); key.isNotEmpty())
            if (folderOrder->hasProperty(juce::Identifier(key))) {
              folderOrder->removeProperty(juce::Identifier(key));
              foldersChanged = true;
            }
    stateSkipped_[path] = skipped;
    if (library != own) continue;
    // Yours: the keep folder, the order and the links. These are this
    // machine's (a linked folder's path is), so a file only brings them back
    // to a machine that has none (a reinstall, a restored backup), never
    // over its own.
    const juce::ScopedValueSetter<bool> quiet(loadingState_, true);
    if (const auto keep = state["keep"].toString(); keep.isNotEmpty() && prefs_.get(kKeepPref).isEmpty())
      if (const auto folder = ls::resolve(base, keep); folder != juce::File()) {
        prefs_.set(kKeepPref, folder.getFullPathName());
        keepChanged = true;
      }
    if (const auto* order = state["order"].getArray(); order != nullptr && prefs_.getJson(kOrderPref).size() == 0) {
      juce::Array<juce::var> paths;
      for (const auto& entry : *order) {
        const auto name = entry.toString();
        if (name.startsWith("tone3000:") || juce::File::isAbsolutePath(name)) paths.add(name);
        else if (const auto at = ls::resolve(base, name); at != juce::File()) paths.add(at.getFullPathName());
      }
      if (!paths.isEmpty()) {
        prefs_.setJson(kOrderPref, juce::var(paths));
        orderChanged = true;
      }
    }
    if (const auto* links = state["links"].getArray(); links != nullptr && prefs_.getJson(kLinksPref).size() == 0) {
      juce::Array<juce::var> paths;
      for (const auto& entry : *links)
        if (juce::File::isAbsolutePath(entry.toString())) paths.add(entry.toString());
      if (!paths.isEmpty()) {
        prefs_.setJson(kLinksPref, juce::var(paths));
        linksChanged = true;
      }
    }
  }
  const juce::ScopedValueSetter<bool> quiet(loadingState_, true);
  if (keptChanged) prefs_.setJson(kKeptPref, juce::var(kept.get()));
  if (picturesChanged) prefs_.setJson(kPicturesPref, juce::var(pictures.get()));
  if (foldersChanged) prefs_.setJson(kFolderOrderPref, juce::var(folderOrder.get()));
  if (keptChanged || keepChanged) keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  if ((orderChanged || foldersChanged) && loaded_) injectFavorites();  // re-sorted
  if (linksChanged && loaded_) refresh();
  return keptChanged || picturesChanged || keepChanged || orderChanged || linksChanged || foldersChanged;
}

void LibraryStore::saveState(bool reread) {
  stateSave_.cancel();
  const juce::ScopedLock fileLock(ls::fileLock());
  // A file changed on disk is read before it is written over (not when the
  // editor is closing: then only what is pending is written).
  if (reread && loadState() && loaded_) notify();
  const auto base = root();
  const auto own = base.getChildFile(owner());
  const auto libraries = stateLibraries();
  const auto linked = links();
  std::map<juce::String, juce::var> states;  // library path -> its state
  const auto stateOf = [&](const juce::File& library) -> juce::DynamicObject& {
    auto& state = states[library.getFullPathName()];
    if (!state.isObject()) state = juce::var(new juce::DynamicObject());
    return *state.getDynamicObject();
  };
  const auto section = [&](const juce::File& library, const char* name) -> juce::DynamicObject& {
    auto& state = stateOf(library);
    if (!state.getProperty(name).isObject()) state.setProperty(name, juce::var(new juce::DynamicObject()));
    return *state.getProperty(name).getDynamicObject();
  };
  for (const auto& library : libraries) {
    auto& state = stateOf(library);  // one with nothing left loses its file
    // What this instance left in the file unread (someone else's paths, a
    // picture not synced yet) stays in it.
    const auto& skipped = stateSkipped_[library.getFullPathName()];
    for (const char* name : {"kept", "pictures", "folders"})
      if (const auto* entries = skipped[name].getDynamicObject())
        for (const auto& entry : entries->getProperties()) section(library, name).setProperty(entry.name, entry.value);
    // Someone else's library: its owner's keep folder, order and links too.
    if (library != own) {
      const auto file = ls::read(library);
      for (const char* name : {"keep", "order", "links"})
        if (file.hasProperty(name)) state.setProperty(name, file[name]);
    }
  }

  if (const auto index = prefs_.getJson(kKeptPref); const auto* links = index.getDynamicObject())
    for (const auto& link : links->getProperties()) {
      const juce::File copy(link.name.toString());
      const auto home = stateHome(copy.getFullPathName(), false, libraries, linked);
      if (home == juce::File()) continue;
      const auto key = home == own && ls::relative(copy, own).isEmpty() ? copy.getFullPathName() : ls::relative(copy, home);
      juce::var from;
      if (link.value.isObject()) {
        from = link.value;
      } else {
        // Its original: in this library, elsewhere in the Library (both
        // follow a move of the Library), or outside it.
        auto* o = new juce::DynamicObject();
        const juce::File source(link.value.toString());
        if (const auto rel = ls::relative(source, home); rel.isNotEmpty() && rel != ".") o->setProperty("source", rel);
        else if (const auto inRoot = ls::relative(source, base); inRoot.isNotEmpty() && inRoot != ".")
          o->setProperty("source_root", inRoot);
        else o->setProperty("source_path", source.getFullPathName());
        from = juce::var(o);
      }
      section(home, "kept").setProperty(juce::Identifier(key), from);
    }

  // Pictures live beside the state that names them: one kept elsewhere
  // (an older build's app-data copy, a folder moved to another library)
  // moves there.
  auto pictures = copyOf(prefs_.getJson(kPicturesPref));
  bool picturesMoved = false;
  for (const auto& entry : pictures->getProperties()) {
    const auto key = entry.name.toString();
    const juce::File picture(entry.value.toString());
    if (!picture.existsAsFile()) continue;
    const auto home = stateHome(key, true, libraries, linked);
    if (home == juce::File()) continue;
    auto at = picture;
    const auto folder = ls::picturesOf(home);
    if (picture.getParentDirectory() != folder) {
      const auto moved = folder.getChildFile(picture.getFileName());
      if (!folder.createDirectory() || !picture.copyFileTo(moved)) continue;
      if ((picture.isAChildOf(legacyPicturesDir()) || picture.getParentDirectory().getFileName() == ls::kPicturesFolder) &&
          !pictureInUse(picture, key))
        picture.deleteFile();
      at = moved;
      pictures->setProperty(entry.name, moved.getFullPathName());
      picturesMoved = true;
    }
    const auto rel = ls::lowerRelative(key, home);
    section(home, "pictures").setProperty(juce::Identifier(rel.isNotEmpty() ? rel : key), at.getFileName());
  }

  // Folder orders, in the library holding the folder (keys relative to it).
  if (const auto orders = prefs_.getJson(kFolderOrderPref); const auto* all = orders.getDynamicObject())
    for (const auto& entry : all->getProperties()) {
      const auto key = entry.name.toString();
      const auto home = stateHome(key, true, libraries, linked);
      if (home == juce::File() || !entry.value.isArray()) continue;
      const auto rel = ls::lowerRelative(key, home);
      section(home, "folders").setProperty(juce::Identifier(rel.isNotEmpty() ? rel : key), entry.value);
    }

  // Yours: the keep folder, the order, the links.
  if (own.isDirectory()) {
    auto& state = stateOf(own);
    if (const auto keep = keepTarget(); keep.isNotEmpty()) {
      const auto rel = ls::relative(juce::File(keep), base);
      state.setProperty("keep", rel.isNotEmpty() && rel != "." ? rel : keep);
    }
    juce::Array<juce::var> order;
    if (const auto stored = prefs_.getJson(kOrderPref); const auto* paths = stored.getArray())
      for (const auto& p : *paths) {
        const auto path = p.toString();
        if (path.startsWith("tone3000:")) order.add(path);
        else if (juce::File::isAbsolutePath(path) && juce::File(path).getParentDirectory() == base)
          order.add(juce::File(path).getFileName());
        else if (juce::File::isAbsolutePath(path) && linked.contains(juce::File(path))) order.add(path);
      }
    if (!order.isEmpty()) state.setProperty("order", order);
    juce::Array<juce::var> paths;
    for (const auto& link : linked) paths.add(link.getFullPathName());
    if (!paths.isEmpty()) state.setProperty("links", paths);
  }

  for (const auto& [path, state] : states) {
    const juce::File library(path);
    ls::write(library, state);
    stateSeen_[path] = ls::stamp(library);
    stateSnapshot_[path] = ls::read(library);
  }
  if (picturesMoved) {
    const juce::ScopedValueSetter<bool> quiet(loadingState_, true);
    prefs_.setJson(kPicturesPref, juce::var(pictures.get()));
  }
}

// Missing links

namespace {
// Where `path` went under `folder`: the longest end of it that is there,
// `folder` itself taking the place of a folder of its name. `bare`: found by
// its own name alone (a weak match: "DI.nam" is in many folders).
struct Found {
  juce::String path;
  bool bare = false;
};
Found foundUnder(const juce::String& path, const juce::File& folder, bool isFolder) {
  const auto parts = juce::StringArray::fromTokens(path, "\\/", "");
  juce::StringArray kept;
  for (const auto& part : parts)
    if (part.isNotEmpty()) kept.add(part);
  const auto there = [isFolder](const juce::File& f) { return isFolder ? f.isDirectory() : f.existsAsFile(); };
  for (int i = 1; i < kept.size(); ++i) {
    const bool last = i == kept.size() - 1;
    if (const auto at = folder.getChildFile(kept.joinIntoString("/", i)); there(at)) return {at.getFullPathName(), last};
    if (kept[i].equalsIgnoreCase(folder.getFileName())) {
      const auto at = i + 1 < kept.size() ? folder.getChildFile(kept.joinIntoString("/", i + 1)) : folder;
      if (there(at)) return {at.getFullPathName(), false};
    }
  }
  return {};
}

// A folder named by a lower-cased path is there. On a case-sensitive disk
// the lower-cased path names nothing: its folders are matched by name from
// `scope` (real case) down.
bool folderThere(const juce::String& lowPath, const juce::File& scope) {
  if (!juce::File::areFileNamesCaseSensitive()) return juce::File(lowPath).isDirectory();
  const auto rel = ls::lowerRelative(lowPath, scope);
  if (rel == ".") return scope.isDirectory();
  auto at = scope;
  for (const auto& part : juce::StringArray::fromTokens(rel, "/", "")) {
    juce::File next;
    for (const auto& dir : at.findChildFiles(juce::File::findDirectories, false))
      if (dir.getFileName().equalsIgnoreCase(part)) next = dir;
    if (next == juce::File()) return false;
    at = next;
  }
  return true;
}

// Only pictures the Library made (a .t3kpictures folder, the older app-data
// one) are ever deleted with their link.
bool ourPicture(const juce::File& picture) {
  return picture.getParentDirectory().getFileName() == ls::kPicturesFolder ||
         picture.getParentDirectory().getFileName() == "LibraryPictures";
}
}  // namespace

void LibraryStore::checkMissing() {
  // Only where the Library looks and what is there to look at: a linked
  // folder on a drive that isn't plugged in (an offline share, a volume not
  // mounted) is away, not missing.
  juce::Array<juce::File> scopes;
  if (root().isDirectory()) scopes.add(root());
  for (const auto& link : links())
    if (link.isDirectory()) scopes.add(link);
  const auto scopeOf = [&](const juce::String& path, bool lowerCased) -> juce::File {
    for (const auto& scope : scopes)
      if (lowerCased ? ls::lowerRelative(path, scope).isNotEmpty() : ls::relative(juce::File(path), scope).isNotEmpty())
        return scope;
    return {};
  };
  std::map<juce::String, Missing> missing;  // by path: listed in path order, each once
  using Kind = Missing::Kind;
  const auto isMissing = [&](const juce::String& path, Kind kind) {
    if (!juce::File::isAbsolutePath(path)) return false;
    const bool lowerCased = kind == Kind::picture;
    const auto scope = scopeOf(path, lowerCased);
    if (scope == juce::File()) return false;
    if (lowerCased) return !folderThere(path, scope);
    return kind == Kind::keepFolder ? !juce::File(path).isDirectory() : !juce::File(path).existsAsFile();
  };
  const auto check = [&](const juce::String& path, Kind kind, const juce::String& other) {
    if (missing.count(path) == 0 && isMissing(path, kind)) missing[path] = {path, kind, other};
  };
  // A link only worked out from matching bytes (learnKept) is no promise
  // anyone made: with either side gone it just goes.
  const auto learned = prefs_.getJson(kLearnedPref);
  juce::StringArray unlearn;
  if (const auto index = prefs_.getJson(kKeptPref); const auto* links = index.getDynamicObject())
    for (const auto& link : links->getProperties()) {
      const auto copy = link.name.toString();
      const auto source = link.value.isString() ? link.value.toString() : link.value["tone"]["title"].toString();
      if (learned.hasProperty(link.name)) {
        if (isMissing(copy, Kind::copy) || isMissing(source, Kind::original)) unlearn.add(copy);
        continue;
      }
      check(copy, Kind::copy, source);
      if (link.value.isObject())
        if (const auto it = missing.find(copy); it != missing.end()) it->second.site = true;
      if (link.value.isString()) check(source, Kind::original, copy);
    }
  if (!unlearn.isEmpty()) forgetLinks(unlearn);
  if (const auto all = prefs_.getJson(kPicturesPref); const auto* pictures = all.getDynamicObject())
    for (const auto& entry : pictures->getProperties())
      if (juce::File(entry.value.toString()).existsAsFile()) check(entry.name.toString(), Kind::picture, {});
  if (const auto keep = prefs_.get(kKeepPref); keep.isNotEmpty()) check(keep, Kind::keepFolder, {});
  missing_.clear();
  for (auto& [path, entry] : missing) missing_.push_back(std::move(entry));
  // Hidden ones that are back (or forgotten) aren't remembered: if they go
  // missing again, the notice says so.
  if (const auto hidden = prefs_.getJson(kMissingHiddenPref); const auto* paths = hidden.getDynamicObject()) {
    juce::DynamicObject::Ptr still = new juce::DynamicObject();
    for (const auto& entry : missing_)
      if (paths->hasProperty(juce::Identifier(entry.path))) still->setProperty(juce::Identifier(entry.path), true);
    if (still->getProperties().size() != paths->getProperties().size())
      prefs_.setJson(kMissingHiddenPref, juce::var(still.get()));
  }
}

void LibraryStore::forgetLinks(const juce::StringArray& copies) {
  auto links = prefs_.getJson(kKeptPref);
  auto learned = prefs_.getJson(kLearnedPref);
  juce::DynamicObject::Ptr keptNow = new juce::DynamicObject(), learnedNow = new juce::DynamicObject();
  if (const auto* old = links.getDynamicObject())
    for (const auto& link : old->getProperties())
      if (!copies.contains(link.name.toString())) keptNow->setProperty(link.name, link.value);
  if (const auto* old = learned.getDynamicObject())
    for (const auto& entry : old->getProperties())
      if (!copies.contains(entry.name.toString())) learnedNow->setProperty(entry.name, entry.value);
  prefs_.setJson(kKeptPref, juce::var(keptNow.get()));
  prefs_.setJson(kLearnedPref, juce::var(learnedNow.get()));
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
}

void LibraryStore::relink(const std::map<juce::String, juce::String>& moved) {
  const auto to = [&moved](const juce::String& path) {
    const auto it = moved.find(path);
    return it == moved.end() ? path : it->second;
  };
  if (const auto index = prefs_.getJson(kKeptPref); const auto* old = index.getDynamicObject()) {
    juce::DynamicObject::Ptr links = new juce::DynamicObject();
    for (const auto& link : old->getProperties()) {
      // A copy found where another link's copy already is stays as it was
      // (it is still missing): one link never replaces another.
      auto copy = to(link.name.toString());
      if (copy != link.name.toString() && old->hasProperty(juce::Identifier(copy))) copy = link.name.toString();
      links->setProperty(juce::Identifier(copy), link.value.isString() ? juce::var(to(link.value.toString())) : link.value);
    }
    prefs_.setJson(kKeptPref, juce::var(links.get()));
  }
  if (const auto all = prefs_.getJson(kPicturesPref); const auto* old = all.getDynamicObject()) {
    juce::DynamicObject::Ptr pictures = new juce::DynamicObject();
    for (const auto& entry : old->getProperties())
      pictures->setProperty(juce::Identifier(to(entry.name.toString()).toLowerCase()), entry.value);
    prefs_.setJson(kPicturesPref, juce::var(pictures.get()));
  }
  if (const auto keep = prefs_.get(kKeepPref); keep.isNotEmpty() && to(keep) != keep) prefs_.set(kKeepPref, to(keep));
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
}

int LibraryStore::findMissing(const juce::File& folder, const juce::String& asked) {
  checkMissing();  // as it is now, not as the last scan had it
  using Kind = Missing::Kind;
  const auto index = prefs_.getJson(kKeptPref);
  std::map<juce::String, juce::String> moved;
  // Matched by name alone, by the folder they were in: trusted only for the
  // one asked about, or when more than one from a folder turn up together
  // (a folder renamed and picked).
  std::map<juce::String, std::vector<std::pair<juce::String, juce::String>>> bare;
  for (const auto& entry : missing_) {
    const bool isFolder = entry.kind == Kind::picture || entry.kind == Kind::keepFolder;
    const auto found = foundUnder(entry.path, folder, isFolder);
    if (found.path.isEmpty()) continue;
    // Not onto the file it stands for (a copy found as its own original),
    // nor onto a file another link already holds as its copy.
    if (found.path == entry.other || (entry.kind == Kind::copy && index.hasProperty(juce::Identifier(found.path)))) continue;
    if (found.bare && entry.path != asked)
      bare[juce::File(entry.path).getParentDirectory().getFullPathName()].emplace_back(entry.path, found.path);
    else
      moved[entry.path] = found.path;
  }
  for (const auto& [from, matches] : bare)
    if (matches.size() > 1)
      for (const auto& [path, at] : matches) moved[path] = at;
  // Two found as one file: neither is trusted.
  std::map<juce::String, int> targets;
  for (const auto& [from, to] : moved) ++targets[to];
  for (auto it = moved.begin(); it != moved.end();) it = targets[it->second] > 1 ? moved.erase(it) : std::next(it);
  // A folder that went missing renamed (picked itself, its old name nowhere
  // in its new path) is told by the files found in it: the folders above
  // each found file pair up, its old ones gone, up to the first that is
  // still there. A missing folder (a picture's, the keep folder) or one
  // inside it takes that.
  std::map<juce::String, juce::File> folders;  // old folder (lower case) -> where it is now
  for (const auto& [from, to] : moved) {
    auto was = juce::File(from).getParentDirectory(), now = juce::File(to).getParentDirectory();
    // Not above the folder picked: what is up there wasn't found.
    for (int depth = 0; depth < 64 && !was.isDirectory() && (now == folder || now.isAChildOf(folder)) &&
                        was != was.getParentDirectory();
         ++depth) {
      folders.emplace(was.getFullPathName().toLowerCase(), now);
      was = was.getParentDirectory();
      now = now.getParentDirectory();
    }
  }
  for (const auto& entry : missing_) {
    const auto& path = entry.path;
    if (moved.count(path) != 0) continue;
    const auto low = path.toLowerCase();
    // The deepest folder that holds it first.
    for (auto it = folders.rbegin(); it != folders.rend(); ++it) {
      const auto& [was, now] = *it;
      if (low != was && !low.startsWith(was + juce::File::getSeparatorString())) continue;
      const auto at = low == was ? now : now.getChildFile(path.substring(was.length() + 1));
      if (at.exists()) {
        moved[path] = at.getFullPathName();
        break;
      }
    }
  }
  const int wanted = static_cast<int>(missing_.size()), found = static_cast<int>(moved.size());
  if (found == 0) {
    fail("No missing files found in " + folder.getFileName());
    return 0;
  }
  relink(moved);
  checkMissing();
  // Blocks playing them dress again (their kept links and pictures are back).
  same_.clear();
  syncShown();
  notify();
  note(found == wanted ? "Found " + juce::String(found) + (found == 1 ? " missing file" : " missing files")
                       : "Found " + juce::String(found) + " of " + juce::String(wanted) + " missing files");
  return found;
}

void LibraryStore::findMissingDialog(const juce::String& path) {
  // Where it was: the nearest folder of its old path still there.
  auto start = path.isNotEmpty() ? juce::File(path).getParentDirectory() : root();
  for (int i = 0; i < 64 && !start.isDirectory() && start != start.getParentDirectory(); ++i)
    start = start.getParentDirectory();
  const auto title = path.isEmpty() ? juce::String("Find the missing files: pick the folder they are in now")
                                    : "Where is " + juce::File(path).getFileName() + " now? Pick the folder it is in";
  launch(std::make_unique<juce::FileChooser>(title, start),
         juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
         [this, path](const juce::FileChooser& fc) {
           if (fc.getResult() != juce::File()) findMissing(fc.getResult(), path);
         });
}

void LibraryStore::downloadMissing(const juce::StringArray& paths) {
  if (downloading_) return (void)fail("Already downloading");
  if (!session_.authenticated()) return (void)fail("Sign in to download TONE3000 tones");
  struct Run {
    std::vector<std::pair<juce::String, juce::var>> jobs;  // copy path, its { tone, model }
    size_t next = 0;
    int kept = 0, failed = 0;
    std::map<int, std::vector<Model>> models;  // per tone, listed once
  };
  auto run = std::make_shared<Run>();
  const auto index = prefs_.getJson(kKeptPref);
  for (const auto& entry : missing_)
    if (entry.site && (paths.isEmpty() || paths.contains(entry.path)))
      if (const auto ref = index[juce::Identifier(entry.path)]; ref.isObject()) run->jobs.emplace_back(entry.path, ref);
  if (run->jobs.empty()) return;
  downloading_ = true;
  const auto finish = [this](const std::shared_ptr<Run>& r) {
    downloading_ = false;
    missingStep_.reset();
    checkMissing();
      same_.clear();
    refresh();
    syncShown();
    const auto captures = [](int n) { return juce::String(n) + (n == 1 ? " capture" : " captures"); };
    if (r->kept == 0) return (void)fail("Couldn't download " + captures(r->failed) + " again");
    note("Downloaded " + captures(r->kept) + " again" +
         (r->failed > 0 ? " (" + juce::String(r->failed) + " failed)" : juce::String()));
  };
  auto step = std::make_shared<std::function<void(std::shared_ptr<Run>)>>();
  *step = [this, finish, weakStep = std::weak_ptr<std::function<void(std::shared_ptr<Run>)>>(step)](
              std::shared_ptr<Run> r) {
    if (r->next >= r->jobs.size()) return finish(r);
    const auto [path, ref] = r->jobs[r->next++];
    const juce::File was(path);
    if (was.existsAsFile()) return (*weakStep.lock())(r);  // back meanwhile: nothing to download
    progress("Downloading " + was.getFileNameWithoutExtension() + " (" + juce::String(static_cast<int>(r->next)) + " of " +
         juce::String(static_cast<int>(r->jobs.size())) + ")");
    const int toneId = ref["tone"]["id"], modelId = ref["model"]["id"];
    const auto format = ref["tone"]["format"].toString();
    // The model's file, by the tone's model list (listed once per tone).
    const auto fetch = [this, r, path, ref, was, modelId, format, weakStep](const std::vector<Model>& models) {
      const auto next = [r, weakStep] {
        if (const auto again = weakStep.lock()) (*again)(r);
      };
      juce::String url;
      for (const auto& m : models)
        if (m.id == modelId) url = m.modelUrl;
      // Where it was, its folder made again if that went too.
      if (url.isEmpty() || !was.getParentDirectory().createDirectory()) {
        ++r->failed;
        return next();
      }
      backend_.libraryDownloadModel(url, format == "ir", was.getParentDirectory(), was.getFileNameWithoutExtension(),
                                    scope_.wrap([this, r, path, ref, next](juce::File file) {
                                      if (file == juce::File()) {
                                        ++r->failed;
                                      } else {
                                        ++r->kept;
                                        // Its link carries on (under the name it got, if that differs).
                                        if (file.getFullPathName() != path) relink({{path, file.getFullPathName()}});
                                        rememberKept(file, ref);
                                      }
                                      next();
                                    }));
    };
    if (const auto known = r->models.find(toneId); known != r->models.end()) return fetch(known->second);
    session_.listToneModels(toneId, format, scope_.wrap([r, toneId, fetch](Result<std::vector<Model>> models) {
      r->models[toneId] = models ? *models : std::vector<Model>();
      fetch(r->models[toneId]);
    }));
  };
  missingStep_ = step;
  session_.ensureNativeAuth(scope_.wrap([this, run, step, finish](const juce::String& error) {
    if (error.isNotEmpty()) {
      downloading_ = false;
      missingStep_.reset();
      return (void)fail(error);
    }
    (*step)(run);
  }));
}

void LibraryStore::forgetMissing() {
  checkMissing();  // not what came back since the last scan
  std::set<juce::String> gone;
  for (const auto& entry : missing_) gone.insert(entry.path);
  if (gone.empty()) return;
  if (const auto index = prefs_.getJson(kKeptPref); const auto* old = index.getDynamicObject()) {
    juce::DynamicObject::Ptr links = new juce::DynamicObject();
    for (const auto& link : old->getProperties())
      if (gone.count(link.name.toString()) == 0 && !(link.value.isString() && gone.count(link.value.toString()) != 0))
        links->setProperty(link.name, link.value);
    prefs_.setJson(kKeptPref, juce::var(links.get()));
  }
  if (const auto all = prefs_.getJson(kPicturesPref); const auto* old = all.getDynamicObject()) {
    juce::DynamicObject::Ptr pictures = new juce::DynamicObject();
    for (const auto& entry : old->getProperties()) {
      if (gone.count(entry.name.toString()) == 0) pictures->setProperty(entry.name, entry.value);
      else if (const juce::File picture(entry.value.toString()); ourPicture(picture) && !pictureInUse(picture, entry.name.toString()))
        picture.deleteFile();
    }
    prefs_.setJson(kPicturesPref, juce::var(pictures.get()));
  }
  if (gone.count(prefs_.get(kKeepPref)) != 0) prefs_.remove(kKeepPref);
  const auto count = gone.size();
  missing_.clear();
  keepListeners_.call([](KeepListener& l) { l.keepChanged(); });
  notify();
  note("Forgot " + juce::String(static_cast<int>(count)) + (count == 1 ? " missing file" : " missing files"));
}

}  // namespace t3k::ui
