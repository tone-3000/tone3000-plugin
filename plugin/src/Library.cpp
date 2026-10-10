#include "Library.h"
#include "LibraryState.h"
#include "NamArchitecture.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <mutex>
#include <chrono>
#include <thread>
#include <optional>

namespace presetfile = t3k::presetfile;
namespace library_state = t3k::library_state;

namespace {

constexpr const char* kContentPrefix = "content/";
// The library state of what an archive holds (LibraryState.h), and the
// folder pictures it names.
constexpr const char* kStateEntry = "state.json";
constexpr const char* kPicturesPrefix = "pictures/";
// A picture bigger than this isn't one.
constexpr juce::int64 kMaxPictureBytes = juce::int64(64) << 20;
// Your library's identity (a hidden file in it): what tells your own backup
// from someone else's library with the same name ("My Library").
constexpr const char* kLibraryIdFile = ".t3klibrary-id";
// The most one archive entry may unpack to (a preset embeds its models).
constexpr juce::int64 kMaxImportEntryBytes = juce::int64(1) << 30;
constexpr const char* kArchiveFormat = "t3klibrary";
constexpr const char* kToneFormat = "t3ktone";
constexpr int kFormatVersion = 1;

bool naturalLess(const juce::String& a, const juce::String& b) {
  return a.compareNatural(b, false) < 0;
}

juce::var makeNode(const char* kind, const juce::String& name, const juce::File& file) {
  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  obj->setProperty("kind", kind);
  obj->setProperty("name", name);
  obj->setProperty("path", file.getFullPathName());
  return obj.get();
}

void set(juce::var& node, const juce::Identifier& key, const juce::var& value) {
  if (auto* obj = node.getDynamicObject())
    obj->setProperty(key, value);
}

// A zip entry name we are willing to write: relative, no "..", no drive or
// UNC prefix. The archive is someone else's file; it never gets to pick
// where on disk its bytes land.
bool safeEntryName(const juce::String& name) {
  if (name.isEmpty() || name.startsWithChar('/') || name.containsChar(':') || name.containsChar('\\'))
    return false;
  for (const auto& part : juce::StringArray::fromTokens(name, "/", ""))
    if (part == "..")
      return false;
  return true;
}

juce::String relativeZipPath(const juce::File& file, const juce::File& base) {
  return file.getRelativePathFrom(base).replaceCharacter('\\', '/');
}

}  // namespace

std::shared_ptr<LocalLibrary::ScanCache> LocalLibrary::processScanCache() {
  static const auto cache = std::make_shared<ScanCache>();
  return cache;
}

// Location

void LocalLibrary::setLocation(const juce::File& rootDir, const juce::String& ownerName,
                               const juce::Array<juce::File>& linkedDirs) {
  root = rootDir;
  const auto trimmed = ownerName.trim();
  owner = trimmed.isEmpty() ? juce::String(kDefaultOwner) : presetfile::sanitizeStem(trimmed);
  // A copy: the caller may pass our own list (linkedDirs()), cleared below.
  const juce::Array<juce::File> wanted = linkedDirs;
  links.clear();
  for (const auto& dir : wanted) {
    // A missing folder (an unplugged drive) stays, to be shown and unlinked;
    // anything overlapping the Library or another link is dropped.
    if (!dir.isDirectory() && dir.getFullPathName().isNotEmpty() && !links.contains(dir)) {
      links.add(dir);
      continue;
    }
    if (linkProblem(dir).isEmpty())
      links.add(dir);
  }
}

juce::String LocalLibrary::linkProblem(const juce::File& dir) const {
  if (!dir.isDirectory())
    return "Not a folder";
  const juce::File presetDir = presetsDir();
  if (dir == root || dir.isAChildOf(root) || root.isAChildOf(dir))
    return "It is part of the Library already";
  if (dir == presetDir || dir.isAChildOf(presetDir) || presetDir.isAChildOf(dir))
    return "It holds your Presets folder";
  for (const auto& link : links)
    if (dir == link || dir.isAChildOf(link) || link.isAChildOf(dir))
      return "It is linked already";
  return {};
}

bool LocalLibrary::isLinkRoot(const juce::File& dir) const { return links.contains(dir); }

juce::String LocalLibrary::folderType(const juce::File& dir) const {
  const juce::File presetDir = presetsDir();
  if (dir == presetDir || dir.isAChildOf(presetDir))
    return kPresetsType;
  const juce::File captures = capturesDir(), local = localDir();
  if (dir == captures || dir.isAChildOf(captures) || dir == local || dir.isAChildOf(local) || isLinkRoot(dir) ||
      insideLink(dir))
    return kCapturesType;
  if (!dir.isAChildOf(root))
    return {};
  // Another library: its top-level Captures/ or Presets/ decides.
  auto half = dir;
  while (half.getParentDirectory().getParentDirectory() != root && half.getParentDirectory() != root)
    half = half.getParentDirectory();
  if (half.getParentDirectory() == root)
    return {};  // the library itself
  if (half.getFileName().equalsIgnoreCase(kCapturesFolderName))
    return kCapturesType;
  if (half.getFileName().equalsIgnoreCase(kPresetsFolderName))
    return kPresetsType;
  return {};
}

bool LocalLibrary::accepts(const juce::File& folder, const juce::File& item) const {
  const juce::String type = folderType(folder);
  if (type.isEmpty())
    return true;
  if (item.isDirectory()) {
    const juce::String itemType = folderType(item);
    return itemType.isEmpty() || itemType == type;
  }
  const juce::String kind = kindOf(item);
  return type == kPresetsType ? kind == "preset" : (kind == "tone" || kind == "capture");
}

bool LocalLibrary::takesPresets(const juce::File& folder) const {
  return canWriteInto(folder) && folderType(folder) != kCapturesType;
}

bool LocalLibrary::insideLink(const juce::File& item) const {
  for (const auto& link : links)
    if (item.isAChildOf(link))
      return true;
  return false;
}

juce::File LocalLibrary::defaultRoot() {
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
      .getChildFile("TONE3000")
      .getChildFile("Library");
}

juce::String LocalLibrary::kindOf(const juce::File& file) {
  if (file.isDirectory())
    return "folder";
  if (file.hasFileExtension(PresetManager::kFileExtension))
    return "preset";
  if (file.hasFileExtension(kToneExtension))
    return "tone";
  if (file.hasFileExtension(".nam;.wav"))
    return "capture";
  return {};
}

juce::var LocalLibrary::readToneRef(const juce::File& file) {
  if (!file.hasFileExtension(kToneExtension))
    return {};
  const juce::var parsed = juce::JSON::parse(file.loadFileAsString());
  if (parsed.getProperty("format", "").toString() != kToneFormat)
    return {};
  if (static_cast<int>(parsed["tone"].getProperty("id", 0)) <= 0)
    return {};
  return parsed;
}

// Permissions

bool LocalLibrary::canEdit(const juce::File& item) const {
  const juce::File presetDir = presetsDir();
  const juce::File factory = presetDir.getChildFile("Factory");
  // The fixed roots stay put (Captures here; Presets and links aren't
  // children of anything editable).
  if (item == factory || item.isAChildOf(factory) || item == capturesDir() || item == localDir())
    return false;
  return item.isAChildOf(ownDir()) || item.isAChildOf(presetDir) || insideLink(item);
}

bool LocalLibrary::canWriteInto(const juce::File& folder) const {
  // Your library's own top takes nothing: things go into its two halves.
  if (folder == capturesDir() || folder == localDir() || folder == presetsDir() ||
      (isLinkRoot(folder) && folder.isDirectory()))
    return true;
  return canEdit(folder) && folder.isDirectory();
}

bool LocalLibrary::canRemove(const juce::File& item) const {
  if (canEdit(item))
    return true;
  // A whole imported library (never yours, never one holding the presets).
  const juce::File presetDir = presetsDir();
  return item.isDirectory() && item.getParentDirectory() == root && item != ownDir() &&
         item != presetDir && !presetDir.isAChildOf(item);
}

bool LocalLibrary::hidden(const juce::File& file) const {
  const juce::String name = file.getFileName();
  // __MACOSX: what a zip made on a Mac leaves beside its files.
  if (name.startsWithChar('.') || name == kManifestName || name == "__MACOSX")
    return true;
  const juce::File presetDir = presetsDir();
  return file == presetDir.getChildFile("Factory") || file == presetDir.getChildFile("order.json");
}

// Gear

namespace {

// The catalog gear id for a trainer-written `gear_type` (the same map as
// localGearFromNamMetadata); "" when it isn't one of the common spellings.
juce::String gearFromGearType(const juce::String& raw) {
  const auto type = raw.trim().toLowerCase();
  if (type == "amp" || type == "pedal_amp" || type == "preamp") return "amp";
  if (type == "amp_cab" || type == "amp_pedal_cab" || type == "amp-cab") return "amp-cab";
  if (type == "studio" || type == "outboard") return "outboard";
  if (type == "pedal") return "pedal";
  if (type == "cab") return "cab";
  return {};
}

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

// A .nam's gear from its metadata, read from the head of the file (the
// trainer writes `metadata` before the weights).
juce::String namGear(const juce::File& file) {
  juce::FileInputStream in(file);
  if (!in.openedOk()) return {};
  juce::MemoryBlock head;
  in.readIntoMemoryBlock(head, 64 * 1024);
  const juce::String text = juce::String::fromUTF8(static_cast<const char*>(head.getData()),
                                                   static_cast<int>(head.getSize()));
  const int key = text.indexOf("\"gear_type\"");
  if (key < 0) return {};
  const auto rest = text.substring(key + 11).trimStart();
  if (!rest.startsWithChar(':')) return {};
  const auto value = rest.substring(1).trimStart();
  if (!value.startsWithChar('"')) return {};
  return gearFromGearType(value.substring(1).upToFirstOccurrenceOf("\"", false, false));
}

// An IR's: short is a cab, long (a room, a reverb) a space (the loader's
// 1 s cutoff).
juce::String irGear(const juce::File& file) {
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::FileInputStream(file), true));
  if (reader == nullptr || reader->sampleRate <= 0.0) return {};
  return static_cast<double>(reader->lengthInSamples) / reader->sampleRate <= 1.0 ? "cab" : "space";
}

}  // namespace

// Scan

juce::String LocalLibrary::locationKey() const {
  juce::String key = root.getFullPathName() + "|" + owner + "|" + presetsDir().getFullPathName();
  for (const auto& link : links)
    key << "|" << link.getFullPathName();
  return key;
}

juce::var LocalLibrary::scan(bool useCache, const std::atomic<bool>* stop) const {
  ScanBudget budget;
  budget.useCache = useCache;
  budget.cancelled = cancelled.get();
  budget.stop = stop;
  // One scan at a time per process: a second waits, then finds the cache
  // warm. A wait for another instance's scan gives up when this one is cut
  // short (its instance or editor going), so a destructor never waits it out.
  static std::mutex scanning;
  std::unique_lock<std::mutex> one(scanning, std::defer_lock);
  while (!one.try_lock()) {
    if (budget.stopped()) {
      juce::DynamicObject::Ptr out = new juce::DynamicObject();
      out->setProperty("cancelled", true);
      out->setProperty("libraries", juce::Array<juce::var>());
      return out.get();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  // Listings carry location-dependent bits (types, edit rights): a new
  // location starts the cache over.
  const juce::String key = locationKey();
  {
    const juce::ScopedLock lock(scanCache->lock);
    if (scanCache->key != key) {
      scanCache->key = key;
      scanCache->dirs.clear();
    }
  }

  juce::Array<juce::var> libraries;
  libraries.add(libraryNode(ownDir(), true, budget));

  if (root.isDirectory()) {
    const juce::File presetDir = presetsDir();
    auto dirs = root.findChildFiles(juce::File::findDirectories | juce::File::ignoreHiddenFiles, false);
    std::sort(dirs.begin(), dirs.end(), [](const juce::File& a, const juce::File& b) {
      return naturalLess(a.getFileName(), b.getFileName());
    });
    for (const auto& dir : dirs) {
      // A root that also holds the presets folder (someone pointed the
      // Library at the app-data folder) must not list it as a library.
      if (dir == ownDir() || hidden(dir) || dir == presetDir || presetDir.isAChildOf(dir))
        continue;
      libraries.add(libraryNode(dir, false, budget));
    }
  }

  {
    // Folders gone since the last scan leave the cache (a truncated scan
    // saw less than there is: keep everything then).
    const juce::ScopedLock lock(scanCache->lock);
    if (budget.nodes <= kMaxNodes && !budget.stopped())
      for (auto it = scanCache->dirs.begin(); it != scanCache->dirs.end();) {
        const bool gone = budget.seen.count(it->first) == 0;
        budget.rescanned += gone ? 1 : 0;
        it = gone ? scanCache->dirs.erase(it) : std::next(it);
      }
  }

  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  out->setProperty("root", root.getFullPathName());
  out->setProperty("owner", owner);
  out->setProperty("libraries", libraries);
  if (budget.nodes > kMaxNodes)
    out->setProperty("truncated", true);
  // Cut short (cancelScans: the instance is going): it saw less than there
  // is, so it is never kept as the saved listing.
  if (budget.stopped())
    out->setProperty("cancelled", true);
  out->setProperty("rescanned", budget.rescanned);
  return out.get();
}

juce::var LocalLibrary::libraryNode(const juce::File& dir, bool mine, ScanBudget& budget) const {
  juce::var node = makeNode("library", dir.getFileName(), dir);
  set(node, "mine", mine);
  set(node, "editable", false);
  set(node, "removable", canRemove(dir));
  set(node, "writable", false);  // items go into its halves

  juce::Array<juce::var> kids;
  if (mine) {
    // Captures and Local (each created on first use), the linked folders
    // last in Local.
    juce::var captures = folderNode(capturesDir(), kCapturesFolderName, true, 1, budget);
    juce::var local = folderNode(localDir(), kLocalFolderName, true, 1, budget);
    set(local, "local", true);
    auto sorted = links;
    std::sort(sorted.begin(), sorted.end(), [](const juce::File& a, const juce::File& b) {
      return naturalLess(a.getFileName(), b.getFileName());
    });
    if (auto* inside = local["children"].getArray()) {
      for (const auto& link : sorted) {
        juce::var linked = folderNode(link, link.getFileName(), true, 2, budget);
        set(linked, "linked", true);
        set(linked, "missing", !link.isDirectory());
        inside->add(linked);
      }
    }
    kids.add(captures);
    kids.add(local);
    // The presets mount, unless the presets folder already sits inside your
    // library (it then lists as itself).
    const juce::File presetDir = presetsDir();
    if (presetDir != dir && !presetDir.isAChildOf(dir))
      kids.add(folderNode(presetDir, kPresetsFolderName, true, 1, budget));
  }
  kids.addArray(children(dir, 1, budget));
  set(node, "children", kids);
  return node;
}

juce::var LocalLibrary::folderNode(const juce::File& dir, const juce::String& name, bool mount,
                                   int depth, ScanBudget& budget) const {
  ++budget.nodes;
  juce::var node = makeNode("folder", name, dir);
  set(node, "mount", mount);
  set(node, "type", folderType(dir));
  set(node, "editable", !mount && canEdit(dir));
  set(node, "removable", !mount && canRemove(dir));
  set(node, "writable", canWriteInto(dir));
  int notA2 = 0;
  const auto kids = children(dir, depth + 1, budget, &notA2);
  set(node, "children", kids);
  // Only A1 folders in it: the folder goes with them (its parent leaves it
  // out). A folder that was empty anyway stays.
  if (!mount && kids.isEmpty() && notA2 > 0)
    set(node, "notA2", true);
  return node;
}

juce::Array<juce::var> LocalLibrary::children(const juce::File& dir, int depth,
                                              ScanBudget& budget, int* notA2Out) const {
  juce::Array<juce::var> out;
  if (depth > kMaxDepth || !dir.isDirectory() || budget.nodes > kMaxNodes)
    return out;
  if (budget.stopped())
    return out;

  const juce::String path = dir.getFullPathName();
  const juce::int64 modified = dir.getLastModificationTime().toMilliseconds();
  budget.seen.insert(path);
  juce::Array<juce::File> folders;
  juce::Array<juce::var> items;
  int notA2 = 0;
  bool cached = false;
  if (budget.useCache) {
    const juce::ScopedLock lock(scanCache->lock);
    const auto it = scanCache->dirs.find(path);
    if (it != scanCache->dirs.end() && it->second.modified == modified) {
      folders = it->second.folders;
      items = it->second.items;
      notA2 = it->second.notA2;
      cached = true;
    }
  }

  if (!cached) {
    ++budget.rescanned;
    // A typed folder lists only its kind; the other kind's files are left
    // alone like any unrelated file.
    const juce::String type = folderType(dir);
    const bool listPresets = type != kCapturesType;
    const bool listCaptures = type != kPresetsType;
    juce::Array<juce::File> others;
    for (const auto& file : dir.findChildFiles(juce::File::findFilesAndDirectories |
                                                   juce::File::ignoreHiddenFiles,
                                               false)) {
      // Your Captures and Local folders list at the top of your library, not again here.
      if (hidden(file) || file == capturesDir() || file == localDir())
        continue;
      const juce::String kind = kindOf(file);
      if (kind == "folder") {
        // A link could loop back on a parent; links are left out of the tree.
        // A folder named for A1 captures ("… (A1)", "xSTD") is left out too.
        if (nam_arch::namedNotA2(file.getFileName()))
          ++notA2;
        else if (!file.isSymbolicLink())
          folders.add(file);
      } else if ((kind == "tone" || kind == "capture") && listCaptures) {
        others.add(file);
      }
    }
    auto byName = [](const juce::File& a, const juce::File& b) {
      return naturalLess(a.getFileNameWithoutExtension(), b.getFileNameWithoutExtension());
    };
    std::sort(folders.begin(), folders.end(), byName);
    std::sort(others.begin(), others.end(), byName);

    // Presets in their folder's order: the user folder's custom order (the
    // preset browser's), natural name order anywhere else.
    if (listPresets) {
      for (const auto& entry : presets.listFolder(dir)) {
        juce::var node = makeNode("preset", entry.info.name, entry.file);
        set(node, "id", entry.info.id);
        set(node, "editable", canEdit(entry.file));
        set(node, "removable", canRemove(entry.file));
        items.add(node);
      }
    }
    // The folder's gear (the drawer's gear filter, the hint's icon): one
    // .nam and one IR read per folder, not every file. A linked collection on
    // a slow drive is tens of thousands of captures; a folder is nearly
    // always one piece of gear, and the listing (cached, saved) remembers it.
    std::optional<juce::String> namGearHere, irGearHere;
    for (const auto& file : others) {
      if (file.hasFileExtension(kToneExtension)) {
        const juce::var ref = readToneRef(file);
        if (ref.isVoid())
          continue;
        juce::var node = makeNode("tone", file.getFileNameWithoutExtension(), file);
        set(node, "ref", ref);
        set(node, "editable", canEdit(file));
        set(node, "removable", canRemove(file));
        items.add(node);
      } else {
        juce::var node = makeNode("capture", file.getFileNameWithoutExtension(), file);
        const bool nam = file.hasFileExtension(".nam");
        set(node, "format", nam ? "nam" : "ir");
        // The file's own name tag ("[AMP] …") says more than its folder's
        // first file's metadata, and costs no read.
        if (const auto tagged = gearFromName(file.getFileName()); tagged.isNotEmpty()) {
          set(node, "gear", tagged);
        } else {
          auto& gear = nam ? namGearHere : irGearHere;
          if (!gear) gear = nam ? namGear(file) : irGear(file);
          if (gear->isNotEmpty()) set(node, "gear", *gear);
        }
        set(node, "editable", canEdit(file));
        set(node, "removable", canRemove(file));
        items.add(node);
      }
    }
    const juce::ScopedLock lock(scanCache->lock);
    scanCache->dirs[path] = {modified, folders, items, notA2};
  }

  for (const auto& folder : folders) {
    if (budget.nodes > kMaxNodes)
      return out;
    auto node = folderNode(folder, folder.getFileName(), false, depth, budget);
    if (node.getProperty("notA2", false)) {
      ++notA2;  // emptied by leaving out the A1 folders
      continue;
    }
    out.add(node);
  }
  if (notA2Out != nullptr)
    *notA2Out = notA2;
  // Item nodes are shared with the cache: nobody writes to them after this.
  budget.nodes += items.size();
  out.addArray(items);
  return out;
}

// File helpers

juce::File LocalLibrary::uniqueChild(const juce::File& dir, const juce::String& stem,
                                     const juce::String& ext, const juce::File& self) {
  juce::File candidate = dir.getChildFile(stem + ext);
  for (int n = 2; candidate.exists() && candidate != self; ++n)
    candidate = dir.getChildFile(stem + " " + juce::String(n) + ext);
  return candidate;
}

bool LocalLibrary::moveAcross(const juce::File& from, const juce::File& to) {
  if (from.moveFileTo(to))
    return true;
  // Different volumes: a rename can't do it.
  if (from.isDirectory())
    return from.copyDirectoryTo(to) && from.deleteRecursively();
  return from.copyFileTo(to) && from.deleteFile();
}

bool LocalLibrary::mergeInto(const juce::File& src, const juce::File& dst) {
  if (!dst.createDirectory())
    return false;
  bool ok = true;
  for (const auto& child : src.findChildFiles(juce::File::findFilesAndDirectories, false)) {
    const juce::File target = dst.getChildFile(child.getFileName());
    if (child.isDirectory()) {
      if (target.isDirectory())
        ok = mergeInto(child, target) && ok;
      else
        ok = moveAcross(child, target.exists() ? uniqueChild(dst, child.getFileName(), {}) : target) && ok;
    } else if (!target.exists()) {
      ok = moveAcross(child, target) && ok;
    } else if (!target.hasIdenticalContentTo(child)) {
      ok = moveAcross(child, uniqueChild(dst, child.getFileNameWithoutExtension(),
                                         child.getFileExtension())) &&
           ok;
    }
  }
  return ok;
}

namespace {
// A preset that changed file name on the way in (a clash got " 2") takes
// the new name inside too, so two presets in one folder never read alike;
// a copy also gets its own id.
void fixPresetIdentity(const juce::File& file, const juce::String& previousStem, bool freshId) {
  if (!file.hasFileExtension(PresetManager::kFileExtension))
    return;
  const bool renamed = file.getFileNameWithoutExtension() != previousStem;
  if (!renamed && !freshId)
    return;
  juce::ValueTree preset = presetfile::read(file);
  if (!preset.isValid())
    return;
  if (renamed)
    preset.setProperty("name", file.getFileNameWithoutExtension(), nullptr);
  if (freshId)
    preset.setProperty("id", juce::Uuid().toString(), nullptr);
  presetfile::write(file, preset);
}
}  // namespace

// Edits

juce::File LocalLibrary::createFolder(const juce::File& parent, const juce::String& name) const {
  const auto trimmed = name.trim();
  if (trimmed.isEmpty() || nam_arch::namedNotA2(trimmed) || !canWriteInto(parent) || !parent.createDirectory())
    return {};
  const juce::File folder = uniqueChild(parent, presetfile::sanitizeStem(trimmed), {});
  return folder.createDirectory() ? folder : juce::File();
}

juce::File LocalLibrary::rename(const juce::File& item, const juce::String& newName) const {
  const auto trimmed = newName.trim();
  if (trimmed.isEmpty() || !item.exists() || (item.isDirectory() && nam_arch::namedNotA2(trimmed)))
    return {};

  if (item == ownDir()) {
    const juce::File target = root.getChildFile(presetfile::sanitizeStem(trimmed));
    if (target == item)
      return item.moveFileTo(target) ? target : item;  // a case-only change
    if (target.exists())
      return {};  // another library already has the name
    return item.moveFileTo(target) ? target : juce::File();
  }
  if (!canEdit(item))
    return {};
  if (kindOf(item) == "preset")
    return presets.renameFile(item, trimmed);

  const juce::String ext = item.isDirectory() ? juce::String() : item.getFileExtension();
  const juce::File target =
      uniqueChild(item.getParentDirectory(), presetfile::sanitizeStem(trimmed), ext, item);
  if (target == item && target.getFileName() == item.getFileName())
    return item;
  return item.moveFileTo(target) ? target : juce::File();
}

bool LocalLibrary::remove(const juce::File& item) const {
  if (!item.exists() || !canRemove(item))
    return false;
#if JUCE_IOS
  constexpr bool hasTrash = false;  // the UI asked before getting here
#else
  constexpr bool hasTrash = true;
#endif
  if (hasTrash && useTrash)
    return item.moveToTrash();
  return item.isDirectory() ? item.deleteRecursively() : item.deleteFile();
}

juce::File LocalLibrary::move(const juce::File& item, const juce::File& folder) const {
  if (!item.exists() || !canEdit(item) || !canWriteInto(folder) || !accepts(folder, item))
    return {};
  if (folder == item || folder.isAChildOf(item))
    return {};  // into itself
  if (item.getParentDirectory() == folder)
    return item;
  if (!folder.createDirectory())
    return {};
  const juce::String ext = item.isDirectory() ? juce::String() : item.getFileExtension();
  const juce::String stem = item.isDirectory() ? item.getFileName() : item.getFileNameWithoutExtension();
  const juce::File target = uniqueChild(folder, stem, ext);
  if (!moveAcross(item, target))
    return {};
  fixPresetIdentity(target, stem, false);
  return target;
}

juce::File LocalLibrary::copy(const juce::File& item, const juce::File& folder) const {
  if (!item.exists() || !canWriteInto(folder) || !accepts(folder, item))
    return {};
  if (folder == item || folder.isAChildOf(item))
    return {};
  if (!folder.createDirectory())
    return {};
  if (item.isDirectory()) {
    const juce::File target = uniqueChild(folder, item.getFileName(), {});
    return item.copyDirectoryTo(target) ? target : juce::File();
  }
  const juce::String stem = item.getFileNameWithoutExtension();
  const juce::File target = uniqueChild(folder, stem, item.getFileExtension());
  if (!item.copyFileTo(target))
    return {};
  fixPresetIdentity(target, stem, true);
  return target;
}

juce::File LocalLibrary::addToneRef(const juce::File& folder, const juce::var& ref) const {
  const juce::var tone = ref["tone"];
  const juce::var model = ref["model"];
  const int toneId = tone.getProperty("id", 0);
  const int modelId = model.getProperty("id", 0);
  if (toneId <= 0 || !canWriteInto(folder) || folderType(folder) == kPresetsType || !folder.createDirectory())
    return {};

  // Already here: hand back the existing reference.
  for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + kToneExtension)) {
    const juce::var existing = readToneRef(file);
    if (static_cast<int>(existing["tone"].getProperty("id", 0)) == toneId &&
        static_cast<int>(existing["model"].getProperty("id", 0)) == modelId)
      return file;
  }

  const juce::String title = tone.getProperty("title", "").toString().trim();
  const juce::String modelName = model.getProperty("name", "").toString().trim();
  juce::String stem = title.isNotEmpty() ? title : "Tone " + juce::String(toneId);
  if (modelName.isNotEmpty() && modelName.compareIgnoreCase(title) != 0)
    stem << " - " << modelName;

  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  obj->setProperty("format", kToneFormat);
  obj->setProperty("version", kFormatVersion);
  obj->setProperty("source", "tone3000");
  obj->setProperty("tone", tone);
  obj->setProperty("model", model);

  const juce::File target = uniqueChild(folder, presetfile::sanitizeStem(stem), kToneExtension);
  return target.replaceWithText(juce::JSON::toString(juce::var(obj.get()))) ? target : juce::File();
}

juce::File LocalLibrary::addCapture(const juce::File& folder, const juce::File& source,
                                    const juce::String& name) const {
  if (!source.existsAsFile() || kindOf(source) != "capture" || !canWriteInto(folder) ||
      folderType(folder) == kPresetsType || !folder.createDirectory())
    return {};
  const juce::String ext = source.getFileExtension().toLowerCase();
  for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*" + ext))
    if (file.getSize() == source.getSize() && file.hasIdenticalContentTo(source))
      return file;
  const juce::String stem = name.trim().isNotEmpty() ? name.trim() : source.getFileNameWithoutExtension();
  const juce::File target = uniqueChild(folder, presetfile::sanitizeStem(stem), ext);
  return source.copyFileTo(target) ? target : juce::File();
}

juce::File LocalLibrary::importFolder(const juce::File& source, const juce::File& into) const {
  if (!source.isDirectory() || !canWriteInto(into) || folderType(into) == kPresetsType)
    return {};
  if (into == source || into.isAChildOf(source))
    return {};  // into itself
  // Listed before anything is written, so the copy never sees itself.
  juce::Array<juce::File> files;
  for (const auto& file : source.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true)) {
    if (file.getFullPathName().contains(juce::File::getSeparatorString() + "__MACOSX" + juce::File::getSeparatorString()))
      continue;  // a Mac zip's leftovers
    // Not what the Library would hide once copied (folders named for A1).
    bool notA2 = false;
    for (auto parent = file.getParentDirectory(); !notA2 && parent.isAChildOf(source); parent = parent.getParentDirectory())
      notA2 = nam_arch::namedNotA2(parent.getFileName());
    if (notA2)
      continue;
    const juce::String kind = kindOf(file);
    if (kind == "capture" || kind == "tone")
      files.add(file);
  }
  if (files.isEmpty() || !into.createDirectory())
    return {};

  const juce::File dest = uniqueChild(into, presetfile::sanitizeStem(source.getFileName()), {});
  int copied = 0;
  for (const auto& file : files) {
    const juce::File target = dest.getChildFile(file.getRelativePathFrom(source));
    if (target.getParentDirectory().createDirectory() && file.copyFileTo(target))
      ++copied;
  }
  if (copied == 0) {
    dest.deleteRecursively();
    return {};
  }
  return dest;
}

// Archives

bool LocalLibrary::exportArchive(const juce::File& item, const juce::File& archive) const {
  if (!item.exists() && item != ownDir())
    return false;

  juce::ZipFile::Builder zip;
  int fileCount = 0;
  // Library content only: presets, references and captures (no OS junk, no
  // order.json, no Factory presets).
  auto addTree = [&](const juce::File& dir, const juce::String& prefix) {
    for (const auto& file : dir.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true)) {
      bool skip = file.getFileName().startsWithChar('.');
      for (auto parent = file.getParentDirectory(); !skip && parent != dir && parent.isAChildOf(dir);
           parent = parent.getParentDirectory())
        skip = hidden(parent);
      if (skip || hidden(file) || kindOf(file).isEmpty())
        continue;
      zip.addFile(file, 9, kContentPrefix + prefix + relativeZipPath(file, dir));
      ++fileCount;
    }
  };

  juce::String kind, libraryOwner = owner;
  if (item == ownDir()) {
    // Linked folders stay out: they aren't the library's own files and can
    // be a whole capture collection. Export one on its own to share it.
    kind = "library";
    if (!presetsDir().isAChildOf(item))
      addTree(presetsDir(), juce::String(kPresetsFolderName) + "/");
    if (item.isDirectory())
      addTree(item, {});
  } else if (item.isDirectory() && item.getParentDirectory() == root) {
    kind = "library";
    libraryOwner = item.getFileName();
    addTree(item, {});
  } else {
    // Whose library the item is in: yours (including the Presets mount),
    // else the imported library it sits under.
    if (!canEdit(item) && item.isAChildOf(root)) {
      auto top = item;
      while (top.getParentDirectory() != root)
        top = top.getParentDirectory();
      libraryOwner = top.getFileName();
    }
    if (item.isDirectory()) {
      kind = "folder";
      addTree(item, item.getFileName() + "/");
    } else {
      kind = "item";
      zip.addFile(item, 9, kContentPrefix + item.getFileName());
      ++fileCount;
    }
  }

  exportState(zip, item, kind == "library");

  juce::DynamicObject::Ptr manifest = new juce::DynamicObject();
  manifest->setProperty("format", kArchiveFormat);
  manifest->setProperty("version", kFormatVersion);
  manifest->setProperty("kind", kind);
  // Which half a folder or item belongs in (an item's own kind decides).
  if (kind == "folder")
    manifest->setProperty("type", folderType(item));
  else if (kind == "item")
    manifest->setProperty("type", kindOf(item) == "preset" ? kPresetsType : kCapturesType);
  manifest->setProperty("owner", libraryOwner);
  if (libraryOwner == owner)
    if (const auto id = libraryId(true); id.isNotEmpty())
      manifest->setProperty("library_id", id);
  manifest->setProperty("name", item == ownDir() ? owner : item.getFileNameWithoutExtension());
  manifest->setProperty("files", fileCount);
  manifest->setProperty("exported", juce::Time::getCurrentTime().toISO8601(true));
  // Where the content came from: a TONE3000 site library would say so,
  // with its id, so a re-import can map back onto it (library.md).
  manifest->setProperty("source", "local");
  const juce::String manifestText = juce::JSON::toString(juce::var(manifest.get()));
  zip.addEntry(new juce::MemoryInputStream(manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true),
               9, kManifestName, juce::Time::getCurrentTime());

  juce::TemporaryFile temp(archive);
  {
    juce::FileOutputStream out(temp.getFile());
    if (!out.openedOk() || !zip.writeToStream(out, nullptr))
      return false;
    out.flush();
    if (out.getStatus().failed())
      return false;
  }
  return temp.overwriteTargetFileWithTemporary();
}

int LocalLibrary::emptyUnsharedBlocks(juce::ValueTree& preset, const std::set<juce::String>& trusted) {
  int emptied = 0;
  for (int i = 0; i < preset.getNumChildren(); ++i) {
    auto child = preset.getChild(i);
    if (!child.hasType("ChainBlock")) {
      emptied += emptyUnsharedBlocks(child, trusted);
      continue;
    }
    const auto clear = [&child] {
      for (int c = child.getNumChildren(); --c >= 0;)
        child.removeChild(c, nullptr);
    };
    if (child.getProperty("type").toString() == "insert") {
      clear();  // an empty slot carries nothing
      continue;
    }
    // Only what is known to be shareable stays (a whitelist): anything
    // unreadable or unexpected is emptied.
    juce::var tone = juce::JSON::parse(child.getProperty("toneJson").toString());
    std::set<juce::String> ids;  // the tone's own models
    if (const auto* models = tone["models"].getArray())
      for (const auto& model : *models)
        ids.insert(model.getProperty("id", "").toString());
    juce::ValueTree cache = child.getChildWithName("ModelCache");
    bool keep = false;
    if (tone.isObject() && !static_cast<bool>(tone.getProperty("local", false))) {
      // A TONE3000 tone: anyone can have it. Its own models' bytes only.
      const int toneId = child.getProperty("toneId", 0);
      keep = toneId > 0 && static_cast<int>(tone.getProperty("id", 0)) == toneId;
      for (int c = cache.getNumChildren(); keep && --c >= 0;)
        if (ids.count(cache.getChild(c).getProperty("modelId").toString()) == 0)
          cache.removeChild(c, nullptr);
    } else if (tone.isObject() && !ids.empty() && cache.getNumChildren() > 0) {
      // Local captures: every byte must be one downloaded from TONE3000.
      keep = true;
      for (int c = 0; keep && c < cache.getNumChildren(); ++c) {
        const juce::var data = cache.getChild(c).getProperty("data");
        const auto* block = data.getBinaryData();
        keep = block != nullptr && trusted.count(library_state::contentHash(*block)) != 0;
      }
      // Without the paths it was loaded from (your folders, your name).
      if (keep)
        if (auto* models = tone["models"].getArray()) {
          for (auto& model : *models)
            if (auto* o = model.getDynamicObject())
              for (const char* path : {"source_path", "model_url"})
                o->removeProperty(path);
          child.setProperty("toneJson", juce::JSON::toString(tone, true), nullptr);
        }
    }
    if (keep)
      continue;
    // An empty slot where it was: its settings and model bytes stay home.
    for (const char* property : {"toneId", "toneJson", "activeModelId"})
      child.removeProperty(property, nullptr);
    child.setProperty("type", "insert", nullptr);
    clear();
    ++emptied;
  }
  return emptied;
}

juce::var LocalLibrary::shareArchive(const juce::File& item, const juce::File& archive, const juce::var& siteRefs) const {
  if (!item.exists() && item != ownDir())
    return {};
  juce::ZipFile::Builder zip;
  std::vector<std::unique_ptr<juce::TemporaryFile>> temps;  // emptied presets, until the zip is written
  int linksOut = 0, leftOut = 0, presetsOut = 0, emptied = 0, fileCount = 0;
  // folder (zip path) -> tone id -> its reference: one link per tone per folder
  std::map<juce::String, std::map<int, juce::var>> tones;
  // Names a folder already has in the zip (lower case): links never repeat one.
  std::map<juce::String, std::set<juce::String>> taken;
  // The bytes a preset block may keep: captures downloaded from TONE3000
  // (their kept link's hash). A folder match alone never counts.
  std::set<juce::String> trusted;
  if (const auto* refs = siteRefs.getDynamicObject())
    for (const auto& ref : refs->getProperties())
      if (const auto hash = ref.value.getProperty("hash", "").toString(); hash.isNotEmpty())
        trusted.insert(hash);
  // A reference as it is; a preset with its unshared blocks emptied.
  const auto addItem = [&](const juce::File& file, const juce::String& at, const juce::String& kind) {
    if (kind == "tone") {
      // As a reference, written fresh: nothing else the file might hold.
      const juce::var ref = readToneRef(file);
      if (!ref.isObject())
        return;
      const juce::String text = juce::JSON::toString(ref);
      zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9, at,
                   juce::Time::getCurrentTime());
      taken[at.upToLastOccurrenceOf("/", true, false)].insert(at.fromLastOccurrenceOf("/", false, false).toLowerCase());
      ++fileCount;
      return;
    }
    auto preset = presetfile::read(file);
    if (!preset.isValid())
      return;
    emptied += emptyUnsharedBlocks(preset, trusted);
    auto temp = std::make_unique<juce::TemporaryFile>(file.getFileName());
    if (!presetfile::write(temp->getFile(), preset))
      return;
    zip.addFile(temp->getFile(), 9, at);
    temps.push_back(std::move(temp));
    ++presetsOut;
    ++fileCount;
  };
  const auto addTree = [&](const juce::File& dir, const juce::String& prefix) {
    if (!dir.isDirectory())
      return;
    for (const auto& file : dir.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true)) {
      bool skip = file.getFileName().startsWithChar('.');
      for (auto parent = file.getParentDirectory(); !skip && parent != dir && parent.isAChildOf(dir);
           parent = parent.getParentDirectory())
        skip = hidden(parent) || nam_arch::namedNotA2(parent.getFileName());
      const juce::String kind = skip || hidden(file) ? juce::String() : kindOf(file);
      const juce::String at = kContentPrefix + prefix + relativeZipPath(file, dir);
      if (kind == "capture") {
        const juce::var ref = siteRefs[juce::Identifier(file.getFullPathName())];
        if (!ref.isObject() || static_cast<int>(ref["tone"].getProperty("id", 0)) <= 0) {
          ++leftOut;
          continue;
        }
        tones[at.upToLastOccurrenceOf("/", true, false)].emplace(static_cast<int>(ref["tone"]["id"]), ref);
      } else if (kind == "tone" || kind == "preset") {
        addItem(file, at, kind);
      }
    }
  };

  juce::String kind = "folder", libraryOwner = owner;
  if (item == ownDir()) {
    kind = "library";
    if (!presetsDir().isAChildOf(item))
      addTree(presetsDir(), juce::String(kPresetsFolderName) + "/");
    addTree(item, {});
    // Linked folders come along too (as links): under Local, by name.
    std::set<juce::String> linkNames;
    for (const auto& link : links)
      if (link.isDirectory()) {
        juce::String name = link.getFileName();
        for (int n = 2; linkNames.count(name.toLowerCase()) != 0 || localDir().getChildFile(name).exists(); ++n)
          name = link.getFileName() + " " + juce::String(n);
        linkNames.insert(name.toLowerCase());
        addTree(link, juce::String(kLocalFolderName) + "/" + name + "/");
      }
  } else if (item.isDirectory() && item.getParentDirectory() == root) {
    kind = "library";
    libraryOwner = item.getFileName();
    addTree(item, {});
  } else if (item.isDirectory()) {
    addTree(item, item.getFileName() + "/");
  } else {
    // One item: a capture as its link, a reference or a preset as above.
    kind = "item";
    const juce::String itemKind = kindOf(item);
    const juce::var ref = siteRefs[juce::Identifier(item.getFullPathName())];
    if (itemKind == "capture" && ref.isObject() && static_cast<int>(ref["tone"].getProperty("id", 0)) > 0)
      tones[kContentPrefix].emplace(static_cast<int>(ref["tone"]["id"]), ref);
    else if (itemKind == "capture")
      ++leftOut;
    else if (itemKind == "tone" || itemKind == "preset")
      addItem(item, kContentPrefix + item.getFileName(), itemKind);
  }

  // The links: a .t3ktone per tone per folder, named after the tone.
  for (const auto& [folder, byTone] : tones) {
    auto& names = taken[folder];
    for (const auto& [toneId, ref] : byTone) {
      juce::DynamicObject::Ptr obj = new juce::DynamicObject();
      obj->setProperty("format", kToneFormat);
      obj->setProperty("version", kFormatVersion);
      obj->setProperty("source", "tone3000");
      obj->setProperty("tone", ref["tone"]);
      obj->setProperty("model", ref["model"].isObject() ? ref["model"] : juce::var(new juce::DynamicObject()));
      const juce::String title = ref["tone"].getProperty("title", "").toString().trim();
      const juce::String base = presetfile::sanitizeStem(title.isNotEmpty() ? title : "Tone " + juce::String(toneId));
      juce::String stem = base;
      for (int n = 2; names.count((stem + kToneExtension).toLowerCase()) != 0; ++n)
        stem = base + " " + juce::String(n);
      names.insert((stem + kToneExtension).toLowerCase());
      const juce::String text = juce::JSON::toString(juce::var(obj.get()));
      zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9,
                   folder + stem + kToneExtension, juce::Time::getCurrentTime());
      ++linksOut;
      ++fileCount;
    }
  }

  juce::DynamicObject::Ptr manifest = new juce::DynamicObject();
  manifest->setProperty("format", kArchiveFormat);
  manifest->setProperty("version", kFormatVersion);
  manifest->setProperty("kind", kind);
  if (kind == "folder")
    manifest->setProperty("type", folderType(item));
  else if (kind == "item")
    manifest->setProperty("type", kindOf(item) == "preset" ? kPresetsType : kCapturesType);
  manifest->setProperty("owner", libraryOwner);
  manifest->setProperty("name", item == ownDir() ? owner : item.getFileNameWithoutExtension());
  manifest->setProperty("files", fileCount);
  manifest->setProperty("exported", juce::Time::getCurrentTime().toISO8601(true));
  manifest->setProperty("source", "local");
  manifest->setProperty("shared", true);  // not a backup: never anyone's own library coming home
  if (fileCount == 0) {
    // Nothing that may go: no archive (it would import as nothing).
    juce::DynamicObject::Ptr none = new juce::DynamicObject();
    none->setProperty("empty", true);
    none->setProperty("leftOut", leftOut);
    return juce::var(none.get());
  }
  const juce::String manifestText = juce::JSON::toString(juce::var(manifest.get()));
  zip.addEntry(new juce::MemoryInputStream(manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true),
               9, kManifestName, juce::Time::getCurrentTime());

  juce::TemporaryFile temp(archive);
  {
    juce::FileOutputStream out(temp.getFile());
    if (!out.openedOk() || !zip.writeToStream(out, nullptr))
      return {};
    out.flush();
    if (out.getStatus().failed())
      return {};
  }
  if (!temp.overwriteTargetFileWithTemporary())
    return {};
  juce::DynamicObject::Ptr summary = new juce::DynamicObject();
  summary->setProperty("links", linksOut);
  summary->setProperty("leftOut", leftOut);
  summary->setProperty("presets", presetsOut);
  summary->setProperty("emptied", emptied);
  return juce::var(summary.get());
}

juce::File LocalLibrary::libraryOf(const juce::File& file) const {
  if (file.getParentDirectory() == root)
    return file;
  if (!file.isAChildOf(root))
    return {};
  auto top = file;
  while (top.getParentDirectory() != root)
    top = top.getParentDirectory();
  return top;
}

void LocalLibrary::exportState(juce::ZipFile::Builder& zip, const juce::File& item, bool whole) const {
  const juce::File library = libraryOf(item);
  if (library == juce::File())
    return;
  // Keys relative to the content: the library itself for a whole one, else
  // the folder the item sits in (the content holds the item by its name).
  const juce::File base = whole ? item : item.getParentDirectory();
  const auto inItem = [&item](const juce::File& f) { return f == item || f.isAChildOf(item); };
  const juce::var state = library_state::read(library);
  juce::DynamicObject::Ptr out = new juce::DynamicObject(), kept = new juce::DynamicObject(),
                           pictures = new juce::DynamicObject();
  if (const auto* links = state["kept"].getDynamicObject())
    for (const auto& link : links->getProperties()) {
      const juce::String key = link.name.toString();
      // Something outside the library (in a linked folder): only a backup of
      // your whole library carries it, as it is.
      if (juce::File::isAbsolutePath(key)) {
        if (whole && library == ownDir())
          kept->setProperty(link.name, link.value);
        continue;
      }
      const juce::File copy = library_state::resolve(library, key);
      if (copy == juce::File() || !inItem(copy))
        continue;
      juce::var from = link.value;
      if (const juce::String source = link.value["source"].toString(); source.isNotEmpty()) {
        const juce::File at = library_state::resolve(library, source);
        if (at == juce::File())
          continue;
        auto* o = new juce::DynamicObject();
        if (inItem(at))
          o->setProperty("source", library_state::relative(at, base));
        else
          o->setProperty("source_path", at.getFullPathName());
        from = juce::var(o);
      }
      kept->setProperty(juce::Identifier(library_state::relative(copy, base)), from);
    }
  if (const auto* folders = state["pictures"].getDynamicObject())
    for (const auto& entry : folders->getProperties()) {
      const juce::String key = entry.name.toString(), name = entry.value.toString();
      const juce::File picture = library_state::picturesOf(library).getChildFile(name);
      if (name.isEmpty() || name.containsAnyOf("/\\") || !picture.existsAsFile())
        continue;
      juce::String at = key;
      if (!juce::File::isAbsolutePath(key)) {
        const juce::String low = library_state::lowerResolve(library, key);
        if (library_state::lowerRelative(low, item).isEmpty())
          continue;
        at = library_state::lowerRelative(low, base);
      } else if (!(whole && library == ownDir())) {
        continue;
      }
      if (at.isEmpty())
        continue;
      pictures->setProperty(juce::Identifier(at), name);
      zip.addFile(picture, 0, kPicturesPrefix + name);
    }
  // Folder orders: re-keyed like the pictures (no files of their own).
  juce::DynamicObject::Ptr folderOrder = new juce::DynamicObject();
  if (const auto* orders = state["folders"].getDynamicObject())
    for (const auto& entry : orders->getProperties()) {
      const juce::String key = entry.name.toString();
      if (!entry.value.isArray())
        continue;
      juce::String at = key;
      if (!juce::File::isAbsolutePath(key)) {
        const juce::String low = library_state::lowerResolve(library, key);
        if (library_state::lowerRelative(low, item).isEmpty())
          continue;
        at = library_state::lowerRelative(low, base);
      } else if (!(whole && library == ownDir())) {
        continue;
      }
      if (at.isNotEmpty())
        folderOrder->setProperty(juce::Identifier(at), entry.value);
    }
  out->setProperty("kept", juce::var(kept.get()));
  out->setProperty("pictures", juce::var(pictures.get()));
  out->setProperty("folders", juce::var(folderOrder.get()));
  if (whole && library == ownDir())
    for (const char* own : {"keep", "order", "links"})
      if (state.hasProperty(own))
        out->setProperty(own, state[own]);
  if (!library_state::holdsAnything(juce::var(out.get())))
    return;
  out->setProperty("version", library_state::kVersion);
  const juce::String text = juce::JSON::toString(juce::var(out.get()));
  zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9, kStateEntry,
               juce::Time::getCurrentTime());
}

void LocalLibrary::importState(juce::ZipFile& zip, const juce::var& state, const juce::File& library,
                               const std::function<juce::File(const juce::String&)>& landed, bool whole) const {
  if (!state.isObject() || library == juce::File() || !library.isDirectory())
    return;
  // Paths outside the Library mean something on the machine they came from:
  // only your own backup brings them (home again).
  const bool yoursWhole = whole && library == ownDir();
  const juce::ScopedLock fileLock(library_state::fileLock());
  juce::var merged = library_state::read(library);
  auto& into = *merged.getDynamicObject();
  const auto section = [&into](const char* name) -> juce::DynamicObject& {
    if (!into.getProperty(name).isObject())
      into.setProperty(name, juce::var(new juce::DynamicObject()));
    return *into.getProperty(name).getDynamicObject();
  };
  if (const auto* links = state["kept"].getDynamicObject())
    for (const auto& link : links->getProperties()) {
      const juce::String key = link.name.toString();
      juce::String at;
      if (juce::File::isAbsolutePath(key)) {
        if (yoursWhole)
          at = key;
      } else if (const juce::File copy = landed(key); copy != juce::File()) {
        at = library_state::relative(copy, library);
      }
      if (at.isEmpty() || at == "." || section("kept").hasProperty(juce::Identifier(at)))
        continue;
      juce::var from = link.value;
      if (const juce::String source = link.value["source"].toString(); source.isNotEmpty()) {
        const juce::File file = landed(source);
        if (file == juce::File())
          continue;
        auto* o = new juce::DynamicObject();
        if (const auto rel = library_state::relative(file, library); rel.isNotEmpty() && rel != ".")
          o->setProperty("source", rel);
        else
          o->setProperty("source_path", file.getFullPathName());
        from = juce::var(o);
      }
      section("kept").setProperty(juce::Identifier(at), from);
    }
  const juce::File picturesDir = library_state::picturesOf(library);
  if (const auto* folders = state["pictures"].getDynamicObject())
    for (const auto& entry : folders->getProperties()) {
      const juce::String key = entry.name.toString(), name = entry.value.toString();
      // A plain name only: nothing that climbs or roots ("C:x.png").
      if (name.isEmpty() || name.containsAnyOf("/\\:") || name.startsWithChar('.') ||
          name != juce::File::createLegalFileName(name))
        continue;
      juce::String at;
      if (juce::File::isAbsolutePath(key)) {
        if (yoursWhole)
          at = key.toLowerCase();
      } else if (const juce::File folder = landed(key); folder != juce::File()) {
        at = library_state::lowerRelative(folder.getFullPathName().toLowerCase(), library);
      }
      if (at.isEmpty() || section("pictures").hasProperty(juce::Identifier(at)))
        continue;
      // The picture itself: a whole library's (a backup's) by its name, one
      // of that name already here being the same; a folder or item import
      // under a name of its own (the same folder imported twice must not
      // share one file: deleting one picture would take the other's).
      const juce::File picture =
          whole ? picturesDir.getChildFile(name) : picturesDir.getChildFile(juce::Uuid().toString() + juce::File(name).getFileExtension());
      if (!picture.isAChildOf(picturesDir))
        continue;
      if (!picture.existsAsFile()) {
        const int index = zip.getIndexOfFileName(kPicturesPrefix + name);
        const auto* zipped = index >= 0 ? zip.getEntry(index) : nullptr;
        if (zipped == nullptr || zipped->uncompressedSize > kMaxPictureBytes || !picturesDir.createDirectory())
          continue;
        std::unique_ptr<juce::InputStream> in{zip.createStreamForEntry(index)};
        bool ok = false;
        {
          juce::FileOutputStream out(picture);
          ok = in != nullptr && out.openedOk() && out.writeFromInputStream(*in, -1) >= 0;
        }
        if (!ok) {
          picture.deleteFile();
          continue;
        }
      }
      section("pictures").setProperty(juce::Identifier(at), picture.getFileName());
    }
  if (const auto* orders = state["folders"].getDynamicObject())
    for (const auto& entry : orders->getProperties()) {
      const juce::String key = entry.name.toString();
      if (!entry.value.isArray())
        continue;
      juce::String at;
      if (juce::File::isAbsolutePath(key)) {
        if (yoursWhole)
          at = key.toLowerCase();
      } else if (const juce::File folder = landed(key); folder != juce::File()) {
        at = library_state::lowerRelative(folder.getFullPathName().toLowerCase(), library);
      }
      if (at.isEmpty() || section("folders").hasProperty(juce::Identifier(at)))
        continue;
      section("folders").setProperty(juce::Identifier(at), entry.value);
    }
  // Your whole library home again: its keep folder, order and links, where
  // it has none of its own.
  if (yoursWhole)
    for (const char* own : {"keep", "order", "links"})
      if (state.hasProperty(own) && !into.hasProperty(own))
        into.setProperty(own, state[own]);
  library_state::write(library, merged);
}

juce::String LocalLibrary::libraryId(bool create) const {
  const juce::File file = ownDir().getChildFile(kLibraryIdFile);
  if (const auto id = file.loadFileAsString().trim(); id.isNotEmpty())
    return id;
  if (!create || !ownDir().createDirectory())
    return {};
  const auto id = juce::Uuid().toString();
  return file.replaceWithText(id) ? id : juce::String();
}

juce::File LocalLibrary::importArchive(const juce::File& archive) const {
  juce::ZipFile zip(archive);
  const int manifestIndex = zip.getIndexOfFileName(kManifestName);
  if (manifestIndex < 0)
    return {};
  juce::var manifest;
  if (std::unique_ptr<juce::InputStream> in{zip.createStreamForEntry(manifestIndex)})
    manifest = juce::JSON::parse(in->readEntireStreamAsString());
  if (manifest.getProperty("format", "").toString() != kArchiveFormat)
    return {};
  juce::var state;
  if (const int stateIndex = zip.getIndexOfFileName(kStateEntry);
      stateIndex >= 0 && zip.getEntry(stateIndex)->uncompressedSize <= library_state::kMaxBytes)
    if (std::unique_ptr<juce::InputStream> in{zip.createStreamForEntry(stateIndex)})
      state = juce::JSON::parse(in->readEntireStreamAsString());

  // Unpack into a hidden staging folder beside the libraries (same volume,
  // so the final moves are renames), then move into place.
  if (!root.createDirectory())
    return {};
  const juce::File staging = root.getChildFile(".import-" + juce::Uuid().toString());
  if (!staging.createDirectory())
    return {};
  struct Cleanup {
    juce::File dir;
    ~Cleanup() { dir.deleteRecursively(); }
  } cleanup{staging};

  for (int i = 0; i < zip.getNumEntries(); ++i) {
    const auto* entry = zip.getEntry(i);
    const juce::String name = entry->filename;
    if (!name.startsWith(kContentPrefix) || name.endsWithChar('/'))
      continue;
    const juce::String relative = name.substring(juce::String(kContentPrefix).length());
    if (!safeEntryName(relative))
      return {};  // a crafted archive: refuse the whole thing
    const juce::File target = staging.getChildFile(relative);
    if (!target.isAChildOf(staging) || !target.getParentDirectory().createDirectory())
      return {};
    // Library content only (what an export writes), and nothing absurd.
    if (kindOf(target).isEmpty() || hidden(target))
      continue;
    if (entry->uncompressedSize > kMaxImportEntryBytes)
      return {};
    target.deleteFile();  // a repeated entry replaces, never appends
    std::unique_ptr<juce::InputStream> in{zip.createStreamForEntry(i)};
    juce::FileOutputStream out(target);
    if (in == nullptr || !out.openedOk() || out.writeFromInputStream(*in, -1) < 0)
      return {};
  }

  const juce::String kind = manifest.getProperty("kind", "").toString();
  juce::String from = manifest.getProperty("owner", "").toString().trim();
  if (from.isEmpty())
    from = archive.getFileNameWithoutExtension();
  // Yours (a backup coming home): by the library id when both have one. A
  // library with none yet (a fresh install restoring its backup) goes by the
  // owner name and takes the backup's id. An archive with no id (an older
  // export) goes by the name, but never the default "My Library", which
  // every signed-out library is called.
  const juce::String archiveId = manifest.getProperty("library_id", "").toString();
  const juce::String ourId = libraryId(false);
  const bool sameName = presetfile::sanitizeStem(from).equalsIgnoreCase(owner);
  // A share (shareArchive) is never a backup coming home, whoever made it.
  const bool shared = static_cast<bool>(manifest.getProperty("shared", false));
  // A fresh install restoring a backup signed out (still "My Library", no id
  // yet, nothing in it): yours, so it never lands as someone else's
  // read-only library (signing in names it then).
  const auto ownEmpty = [this] {
    for (const auto& half : {capturesDir(), localDir()})
      if (!half.findChildFiles(juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, true).isEmpty())
        return false;
    return true;
  };
  const bool freshInstall = kind == "library" && owner.equalsIgnoreCase(kDefaultOwner) && ownEmpty();
  const bool yours = shared                                         ? false
                     : archiveId.isNotEmpty() && ourId.isNotEmpty() ? archiveId == ourId
                     : archiveId.isNotEmpty()                       ? sameName || freshInstall
                                                                    : sameName && !owner.equalsIgnoreCase(kDefaultOwner);
  if (yours && archiveId.isNotEmpty() && ourId.isEmpty() && ownDir().createDirectory())
    ownDir().getChildFile(kLibraryIdFile).replaceWithText(archiveId);
  juce::File base = yours ? ownDir() : root.getChildFile(presetfile::sanitizeStem(from));
  // Someone else's under a name that is yours (or the presets folder's, or
  // anything else this library can't replace): beside it, under its own.
  // (Your library's own folder too, before it exists: it would become yours.)
  if (!yours && (base == ownDir() || (base.exists() && !canRemove(base))))
    base = uniqueChild(root, presetfile::sanitizeStem(from) + " (imported)", {});

  if (kind == "library") {
    // Where each file of the content lands: under the library, but not a
    // file of a backup that clashed with a different one already here (the
    // merge renames it; what is said about the one here stays).
    std::set<juce::String> clashed;
    if (yours && state.isObject())
      if (const auto* kept = state["kept"].getDynamicObject())
        for (const auto& link : kept->getProperties()) {
          const auto staged = library_state::resolve(staging, link.name.toString());
          const auto here = library_state::resolve(base, link.name.toString());
          if (staged.existsAsFile() && here.existsAsFile() && !here.hasIdenticalContentTo(staged))
            clashed.insert(link.name.toString());
        }
    const auto landed = [&base, &clashed](const juce::String& key) {
      return clashed.count(key) != 0 ? juce::File() : library_state::resolve(base, key);
    };
    if (yours) {
      // A backup coming home: merge, keeping what is already here.
      const juce::File stagedPresets = staging.getChildFile(kPresetsFolderName);
      if (stagedPresets.isDirectory() && !(mergeInto(stagedPresets, presetsDir()) &&
                                           stagedPresets.deleteRecursively()))
        return {};
      if (!mergeInto(staging, base))
        return {};
      importState(zip, state, base, landed, true);
      return base;
    }
    // Someone else's library: this import is its current state. The old
    // one goes to the trash; when it can't, the import stops rather than
    // deleting it outright.
    if (base.exists() && !(useTrash ? base.moveToTrash() : base.deleteRecursively()))
      return {};
    if (!moveAcross(staging, base))
      return {};
    importState(zip, state, base, landed, true);
    return base;
  }

  // A folder or a single item: into the matching half, beside whatever is
  // there (an untyped one at the library's top).
  const juce::String type = manifest.getProperty("type", "").toString();
  juce::File into = base;
  if (type == kCapturesType)
    into = yours ? capturesDir() : base.getChildFile(kCapturesFolderName);
  else if (type == kPresetsType)
    into = yours ? presetsDir() : base.getChildFile(kPresetsFolderName);
  if (!into.createDirectory())
    return {};
  juce::File landed;
  std::map<juce::String, juce::File> tops;  // a top entry's name (lower case) -> where it went
  for (const auto& top : staging.findChildFiles(juce::File::findFilesAndDirectories, false)) {
    const bool dir = top.isDirectory();
    const juce::File target = uniqueChild(into, dir ? top.getFileName() : top.getFileNameWithoutExtension(),
                                          dir ? juce::String() : top.getFileExtension());
    if (!moveAcross(top, target))
      return {};
    tops[top.getFileName().toLowerCase()] = target;
    landed = target;
  }
  importState(zip, state, libraryOf(into), [&tops](const juce::String& key) {
    if (juce::File::isAbsolutePath(key))
      return juce::File();
    const auto it = tops.find(key.upToFirstOccurrenceOf("/", false, false).toLowerCase());
    if (it == tops.end())
      return juce::File();
    const auto rest = key.fromFirstOccurrenceOf("/", false, false);
    return rest.isEmpty() ? it->second : library_state::resolve(it->second, rest);
  }, false);
  return landed;
}
