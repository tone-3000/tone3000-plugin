#include "PresetManager.h"
// For TONE3000Processor::ensureWritableDir: preset saves share the app-data
// folder whose permissions a sudo'd install script can mangle (github
// issue #76).
#include "Processor.h"
#include <algorithm>
#include <limits>

namespace presetfile = t3k::presetfile;

namespace {

constexpr const char* kUserPrefix = "user:";
constexpr const char* kFactoryPrefix = "factory:";

juce::File presetsRootDir() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("Presets");
}

juce::String stripPrefix(const juce::String& id, const char* prefix) {
  return id.fromFirstOccurrenceOf(prefix, false, false);
}

}  // namespace

PresetManager::PresetManager() : PresetManager(presetsRootDir(), defaultSystemFactoryDir()) {}

PresetManager::PresetManager(const juce::File& baseDir, const juce::File& systemFactory)
    : userDir(baseDir),
      factoryDir(baseDir.getChildFile("Factory")),
      systemFactoryDir(systemFactory) {}

PresetManager::PresetManager(const PresetManager& other)
    : userDir(other.userDir), factoryDir(other.factoryDir), systemFactoryDir(other.systemFactoryDir) {}

PresetManager& PresetManager::operator=(const PresetManager& other) {
  if (this == &other)
    return *this;
  const juce::ScopedLock lock(cacheLock);
  userDir = other.userDir;
  factoryDir = other.factoryDir;
  systemFactoryDir = other.systemFactoryDir;
  cache.clear();  // paths from the old root would only be pruned anyway
  return *this;
}

juce::File PresetManager::defaultSystemFactoryDir() {
  // Shared all-users location the installers write to. A missing dir just
  // means no shipped presets; scans treat it as empty.
#if JUCE_IOS
  // iOS has no installer and so no shared factory directory outside the app:
  // the same .t3kpreset files ride inside the bundle (plugin/CMakeLists.txt carries
  // resources/factory-presets as bundle resources; iOS bundles are flat, so
  // they land at TONE3000.app/FactoryPresets). The bundle is read-only, which
  // is exactly the contract this directory already has; a user Factory folder
  // still overlays it in list(), as on macOS and Windows. This is the
  // Standalone app: an AUv3 extension would resolve to its own .appex, which
  // carries no presets, so revisit this when AUv3 arrives.
  return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
      .getChildFile("FactoryPresets");
#elif JUCE_MAC
  return juce::File("/Library/Application Support/TONE3000/Presets/Factory");
#elif JUCE_WINDOWS
  // ProgramData; matches the Inno Setup {commonappdata} destination.
  return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
      .getChildFile("TONE3000")
      .getChildFile("Presets")
      .getChildFile("Factory");
#elif JUCE_LINUX
  // The tarball installs per-user (into factoryDir); this path is the hook
  // for system-wide/distro packaging.
  return juce::File("/usr/share/TONE3000/Presets/Factory");
#else
  return {};
#endif
}

// Filenames

juce::File PresetManager::writeUserPreset(const juce::File& dir, const juce::File& current,
                                          const juce::String& name, const juce::ValueTree& preset) {
  const juce::File target = presetfile::uniqueFile(dir, presetfile::sanitizeStem(name), current);
  const bool haveCurrent = current != juce::File() && current.existsAsFile();

  if (haveCurrent && current == target) {
    // Same file: rewrite in place, then fix a case-only spelling change.
    if (!presetfile::write(current, preset))
      return {};
    if (current.getFileName() != target.getFileName() && !current.moveFileTo(target))
      return current;  // content is saved; the old spelling is cosmetic
    return target;
  }

  // New file, or the filename no longer matches the name (a rename, or a
  // legacy <uuid> file getting its readable name): write the new file
  // first, then drop the old one, so a failure never loses the preset.
  if (!presetfile::write(target, preset))
    return {};
  if (haveCurrent) {
    if (!current.deleteFile())
      juce::Logger::writeToLog("[Presets] Could not remove superseded file: " +
                               current.getFullPathName());
    else
      juce::Logger::writeToLog("[Presets] Renamed file " + current.getFileName() + " -> " +
                               target.getFileName());
  }
  return target;
}

// Scanning

std::vector<PresetManager::Entry> PresetManager::scanDir(const juce::File& dir, const char* prefix,
                                                         bool factory, ScanState& state) const {
  std::vector<Entry> out;
  if (!dir.isDirectory())
    return out;

  // Stem order, so which of two files claiming one id keeps it (below) does
  // not depend on the OS's directory iteration order: "Original" beats
  // "Original copy".
  auto files = dir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kFileExtension));
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
    return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
  });

  std::set<juce::String> idsInDir;
  for (const auto& file : files) {
    // Same (mtime, size) as last time: the id/name are what we read then.
    // Every write goes through write-then-rename (presetfile::write), so any
    // change to a preset moves at least its mtime. A never-seen path gets a
    // default entry (mtime 0), which no real file matches.
    const juce::int64 modificationMs = file.getLastModificationTime().toMilliseconds();
    const juce::int64 size = file.getSize();
    const juce::String stem = file.getFileNameWithoutExtension();
    state.seen.insert(file.getFullPathName());
    auto& cached = cache[file.getFullPathName()];
    if (cached.modificationMs != modificationMs || cached.size != size) {
      ++state.parsed;
      // Header only for v2 files; a legacy v1 file is parsed in full (once).
      const presetfile::Header header = presetfile::readHeader(file);
      cached.modificationMs = modificationMs;
      cached.size = size;
      cached.valid = header.valid;
      if (cached.valid) {
        // Legacy files (and hand-renamed ones) carry no id: the stem is it.
        cached.rawId = header.id.isEmpty() ? stem : header.id;
        cached.name = header.name.isEmpty() ? stem : header.name;
      } else {
        cached.rawId.clear();
        cached.name.clear();
      }
    }
    if (!cached.valid)
      continue;

    // Two files claiming one id (a copied/imported file): the first keeps
    // it, the second falls back to its stem so both still list and load.
    juce::String id = prefix + cached.rawId;
    if (!idsInDir.insert(id).second) {
      id = prefix + stem;
      if (!idsInDir.insert(id).second)
        continue;  // stem taken too: nothing sane to call it
      juce::Logger::writeToLog("[Presets] Duplicate preset id in " + file.getFileName() +
                               "; listing it as " + id);
    }
    Entry entry;
    entry.info.id = id;
    entry.info.name = cached.name;
    entry.info.factory = factory;
    entry.file = file;
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    return a.info.name.compareIgnoreCase(b.info.name) < 0;
  });
  return out;
}

std::vector<PresetManager::Entry> PresetManager::entries() const {
  const auto startMs = juce::Time::getMillisecondCounterHiRes();
  ScanState state;
  // One lock for the whole scan: concurrent callers (host program API off
  // the message thread) would otherwise each pay to parse the same new file.
  const juce::ScopedLock lock(cacheLock);

  // Factory section: system Factory with the user Factory overlaid on top (a
  // local file with the same id replaces the shipped one), re-sorted by name
  // so the merged section reads like a single folder.
  std::vector<Entry> factory = scanDir(systemFactoryDir, kFactoryPrefix, true, state);
  for (auto& local : scanDir(factoryDir, kFactoryPrefix, true, state)) {
    const auto it = std::find_if(factory.begin(), factory.end(), [&local](const Entry& shipped) {
      return shipped.info.id == local.info.id;
    });
    if (it != factory.end())
      *it = std::move(local);
    else
      factory.push_back(std::move(local));
  }
  std::sort(factory.begin(), factory.end(), [](const Entry& a, const Entry& b) {
    return a.info.name.compareIgnoreCase(b.info.name) < 0;
  });

  // User presets lead so they own the low MIDI program-change numbers; the
  // factory section follows.
  std::vector<Entry> presets = scanDir(userDir, kUserPrefix, false, state);
  presets.insert(presets.end(), std::make_move_iterator(factory.begin()),
                 std::make_move_iterator(factory.end()));

  // Forget files that are gone (remove(), or deleted behind our back).
  for (auto it = cache.begin(); it != cache.end();)
    it = state.seen.count(it->first) != 0 ? std::next(it) : cache.erase(it);

  // Apply the custom order: within each section, ordered ids first (in file
  // order), then everything else. The sort is stable over the name-sorted
  // scan above, so presets missing from the order file (new saves, ids from
  // another machine) stay alphabetical after the ordered block, and a
  // missing/empty order file leaves the classic ordering untouched.
  const juce::StringArray order = readOrder();
  if (!order.isEmpty()) {
    auto rank = [&order](const Entry& entry) {
      const int index = order.indexOf(entry.info.id);
      return index < 0 ? std::numeric_limits<int>::max() : index;
    };
    std::stable_sort(presets.begin(), presets.end(), [&](const Entry& a, const Entry& b) {
      if (a.info.factory != b.info.factory)
        return !a.info.factory;  // user section always first
      return rank(a) < rank(b);
    });
  }

  // Category + star state from the side file. A category the file no longer
  // lists (hand-edited) reads as root rather than inventing a section.
  const Meta meta = readMeta();
  for (Entry& entry : presets) {
    entry.info.favorite = meta.favorites.contains(entry.info.id);
    const auto filed = meta.filed.find(entry.info.id);
    if (!entry.info.factory && filed != meta.filed.end()) {
      const int index = indexOfCategory(meta.categories, filed->second);
      if (index >= 0)
        entry.info.category = meta.categories[index];
    }
  }

  // Release-level line only when files were actually read and it was slow:
  // a warm scan never logs; a cold one on a slow disk (issue #169) says so
  // in the user's log next to whatever the host was doing at the time.
  const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
  if (state.parsed > 0 && elapsedMs >= 100.0)
    juce::Logger::writeToLog("[Presets] Scanned " + juce::String(presets.size()) + " presets (" +
                             juce::String(state.parsed) + " read from disk) in " +
                             juce::String(juce::roundToInt(elapsedMs)) + " ms");
  return presets;
}

std::vector<PresetManager::Info> PresetManager::list() const {
  std::vector<Info> out;
  for (auto& entry : entries())
    out.push_back(std::move(entry.info));
  return out;
}

juce::File PresetManager::fileForId(const juce::String& id) const {
  if (id.isEmpty())
    return {};
  for (const auto& entry : entries())
    if (entry.info.id == id)
      return entry.file;
  return {};
}

// Order

bool PresetManager::move(const juce::String& id, int delta) const {
  if (delta == 0)
    return false;

  const std::vector<Info> presets = list();
  const auto it = std::find_if(presets.begin(), presets.end(),
                               [&id](const Info& info) { return info.id == id; });
  if (it == presets.end())
    return false;

  const int index = static_cast<int>(std::distance(presets.begin(), it));
  const bool factory = it->factory;

  int sectionStart = index;
  while (sectionStart > 0 && presets[static_cast<size_t>(sectionStart - 1)].factory == factory)
    --sectionStart;
  int sectionEnd = index + 1;
  while (sectionEnd < static_cast<int>(presets.size()) &&
         presets[static_cast<size_t>(sectionEnd)].factory == factory)
    ++sectionEnd;

  const int target = std::clamp(index + delta, sectionStart, sectionEnd - 1);
  if (target == index)
    return false;  // already at its section's edge (or a no-op clamp)

  juce::StringArray ids;
  for (const Info& info : presets)
    ids.add(info.id);
  const juce::String moving = ids[index];
  ids.remove(index);
  ids.insert(target, moving);
  return writeOrder(ids);
}

juce::File PresetManager::orderFile() const {
  // Beside the preset files (the *.t3kpreset scan never picks it up).
  return userDir.getChildFile("order.json");
}

juce::StringArray PresetManager::readOrder() const {
  juce::StringArray out;
  const auto parsed = juce::JSON::parse(orderFile().loadFileAsString());
  if (const auto* ids = parsed.getArray())
    for (const auto& id : *ids)
      out.add(id.toString());
  return out;
}

bool PresetManager::writeOrder(const juce::StringArray& ids) const {
  if (!userDir.createDirectory())
    return false;
  juce::Array<juce::var> list;
  for (const auto& id : ids)
    list.add(id);
  return orderFile().replaceWithText(juce::JSON::toString(juce::var(list)));
}

// CRUD

juce::ValueTree PresetManager::load(const juce::String& id) const {
  return presetfile::read(fileForId(id));
}

PresetManager::Info PresetManager::save(const juce::String& name, juce::ValueTree preset) const {
  // ensureWritableDir rather than a bare createDirectory: the folder can
  // exist and still be unwritable (root-owned after a sudo'd script), and
  // that state used to fail every save with this same log line forever.
  if (!TONE3000Processor::ensureWritableDir(userDir)) {
    juce::Logger::writeToLog("[Presets] Failed to create presets directory: " +
                             userDir.getFullPathName());
    return {};
  }

  // Same-name save overwrites that preset (keeps its id); this is the update
  // path. A legacy file picks up its stem as the stored id here, so the id
  // the UI and any DAW project already hold stays valid.
  juce::File current;
  juce::String rawId;
  for (const Entry& existing : entries()) {
    if (!existing.info.factory && existing.info.name.compareIgnoreCase(name) == 0) {
      current = existing.file;
      rawId = stripPrefix(existing.info.id, kUserPrefix);
      break;
    }
  }
  if (rawId.isEmpty())
    rawId = juce::Uuid().toString();

  preset.setProperty("name", name, nullptr);
  preset.setProperty("id", rawId, nullptr);
  if (writeUserPreset(userDir, current, name, preset) == juce::File()) {
    juce::Logger::writeToLog("[Presets] Failed to write preset file for: " + name);
    return {};
  }

  Info info;
  info.id = kUserPrefix + rawId;
  info.name = name;
  info.factory = false;
  return info;
}

bool PresetManager::rename(const juce::String& id, const juce::String& newName) const {
  const juce::String trimmed = newName.trim();
  if (!id.startsWith(kUserPrefix) || trimmed.isEmpty())
    return false;
  const juce::File file = fileForId(id);
  juce::ValueTree preset = presetfile::read(file);
  if (!preset.isValid())
    return false;
  preset.setProperty("name", trimmed, nullptr);
  // Legacy file: the stem was its id; pin it inside before the file moves.
  preset.setProperty("id", stripPrefix(id, kUserPrefix), nullptr);
  return writeUserPreset(userDir, file, trimmed, preset) != juce::File();
}

bool PresetManager::remove(const juce::String& id) const {
  if (!id.startsWith(kUserPrefix))
    return false;
  const juce::File file = fileForId(id);
  if (file == juce::File() || !file.deleteFile())
    return false;

  // Drop the preset's side-file state so a stale entry cannot resurface.
  bool metaOk = true;
  Meta meta = readMeta(&metaOk);
  const bool hadState = metaOk && (meta.filed.erase(id) > 0 || meta.favorites.contains(id));
  if (hadState) {
    meta.favorites.removeString(id);
    if (!writeMeta(meta))
      juce::Logger::writeToLog("[Presets] Could not clear category/star state for " + id);
  }
  return true;
}

// Categories, favourites, bulk operations

juce::File PresetManager::metaFile() const {
  return userDir.getChildFile("presets-meta.json");
}

int PresetManager::indexOfCategory(const juce::StringArray& categories, const juce::String& name) {
  for (int i = 0; i < categories.size(); ++i)
    if (categories[i].compareIgnoreCase(name) == 0)
      return i;
  return -1;
}

PresetManager::Meta PresetManager::readMeta(bool* ok) const {
  Meta meta;
  const juce::String text = metaFile().loadFileAsString();
  const auto parsed = juce::JSON::parse(text);
  if (ok != nullptr) *ok = text.trim().isEmpty() || parsed.getDynamicObject() != nullptr;
  if (const auto* categories = parsed["categories"].getArray())
    for (const auto& item : *categories) {
      const juce::String name = item.toString().trim();
      if (name.isNotEmpty() && indexOfCategory(meta.categories, name) < 0)
        meta.categories.add(name);
    }
  meta.categories.sort(true);
  if (const auto* filed = parsed["filed"].getDynamicObject())
    for (const auto& property : filed->getProperties())
      meta.filed[property.name.toString()] = property.value.toString();
  if (const auto* favorites = parsed["favorites"].getArray())
    for (const auto& item : *favorites)
      meta.favorites.addIfNotAlreadyThere(item.toString());
  return meta;
}

bool PresetManager::writeMeta(const Meta& meta) const {
  if (!TONE3000Processor::ensureWritableDir(userDir))
    return false;
  juce::Array<juce::var> categories;
  for (const auto& name : meta.categories)
    categories.add(name);
  juce::Array<juce::var> favorites;
  for (const auto& id : meta.favorites)
    favorites.add(id);
  juce::DynamicObject::Ptr filed = new juce::DynamicObject();
  for (const auto& [id, category] : meta.filed)
    filed->setProperty(juce::Identifier(id), category);
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("categories", categories);
  root->setProperty("filed", juce::var(filed.get()));
  root->setProperty("favorites", favorites);
  return metaFile().replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

juce::StringArray PresetManager::listCategories() const {
  return readMeta().categories;
}

bool PresetManager::addCategory(const juce::String& rawName) const {
  const juce::String name = rawName.trim();
  if (name.isEmpty() || name.length() > kMaxCategoryNameLength)
    return false;
  bool metaOk = true;
  Meta meta = readMeta(&metaOk);
  if (!metaOk) return false;
  if (indexOfCategory(meta.categories, name) >= 0)
    return false;
  meta.categories.add(name);
  meta.categories.sort(true);
  return writeMeta(meta);
}

bool PresetManager::deleteCategory(const juce::String& rawName) const {
  bool metaOk = true;
  Meta meta = readMeta(&metaOk);
  if (!metaOk) return false;
  const int index = indexOfCategory(meta.categories, rawName.trim());
  if (index < 0)
    return false;
  const juce::String name = meta.categories[index];
  meta.categories.remove(index);
  // One write: the category and every filing under it go together, so there
  // is no window where presets point at a deleted category.
  for (auto it = meta.filed.begin(); it != meta.filed.end();)
    it = it->second.compareIgnoreCase(name) == 0 ? meta.filed.erase(it) : std::next(it);
  return writeMeta(meta);
}

bool PresetManager::setPresetCategory(const juce::String& id, const juce::String& category) const {
  return movePresetsToCategory(juce::StringArray{id}, category);
}

bool PresetManager::movePresetsToCategory(const juce::StringArray& ids,
                                          const juce::String& category) const {
  bool metaOk = true;
  Meta meta = readMeta(&metaOk);
  if (!metaOk) return false;
  const juce::String trimmed = category.trim();
  juce::String canonical;
  if (trimmed.isNotEmpty()) {
    const int index = indexOfCategory(meta.categories, trimmed);
    if (index < 0)
      return false;
    canonical = meta.categories[index];
  }

  std::set<juce::String> userIds;
  for (const Info& info : list())
    if (!info.factory)
      userIds.insert(info.id);
  for (const auto& id : ids)
    if (userIds.count(id) == 0)
      return false;  // all-or-nothing: no partial moves on a bad id

  for (const auto& id : ids) {
    if (canonical.isEmpty())
      meta.filed.erase(id);
    else
      meta.filed[id] = canonical;
  }
  return writeMeta(meta);
}

bool PresetManager::setPresetFavorite(const juce::String& id, bool isFavorite) const {
  return setPresetsFavorite(juce::StringArray{id}, isFavorite);
}

bool PresetManager::setPresetsFavorite(const juce::StringArray& ids, bool isFavorite) const {
  std::set<juce::String> known;
  for (const Info& info : list())
    known.insert(info.id);
  for (const auto& id : ids)
    if (known.count(id) == 0)
      return false;

  bool metaOk = true;
  Meta meta = readMeta(&metaOk);
  if (!metaOk) return false;
  for (const auto& id : ids) {
    if (isFavorite)
      meta.favorites.addIfNotAlreadyThere(id);
    else
      meta.favorites.removeString(id);
  }
  return writeMeta(meta);
}

PresetManager::Info PresetManager::duplicatePreset(const juce::String& id) const {
  if (!id.startsWith(kUserPrefix))
    return {};
  const std::vector<Entry> all = entries();
  const auto source = std::find_if(all.begin(), all.end(),
                                   [&id](const Entry& entry) { return entry.info.id == id; });
  if (source == all.end())
    return {};
  juce::ValueTree preset = presetfile::read(source->file);
  if (!preset.isValid() || !TONE3000Processor::ensureWritableDir(userDir))
    return {};

  // Never reuse an existing user name: save() treats a same-name preset as an
  // update, and a duplicate must not overwrite an earlier copy.
  const juce::String base = "Copy-" + source->info.name;
  juce::String name = base;
  for (int n = 2;; ++n) {
    const bool taken = std::any_of(all.begin(), all.end(), [&name](const Entry& entry) {
      return !entry.info.factory && entry.info.name.compareIgnoreCase(name) == 0;
    });
    if (!taken)
      break;
    name = base + " " + juce::String(n);
  }

  const juce::String rawId = juce::Uuid().toString();
  preset.setProperty("name", name, nullptr);
  preset.setProperty("id", rawId, nullptr);
  if (writeUserPreset(userDir, juce::File(), name, preset) == juce::File())
    return {};

  Info info;
  info.id = kUserPrefix + rawId;
  info.name = name;
  info.category = source->info.category;
  if (info.category.isNotEmpty()) {
    bool metaOk = true;
    Meta meta = readMeta(&metaOk);
    meta.filed[info.id] = info.category;
    if (!metaOk || !writeMeta(meta))
      info.category.clear();
  }
  return info;
}

std::vector<PresetManager::Info> PresetManager::duplicatePresets(const juce::StringArray& ids) const {
  std::vector<Info> out;
  for (const auto& id : ids) {
    Info copy = duplicatePreset(id);
    if (copy.id.isNotEmpty())
      out.push_back(std::move(copy));
  }
  return out;
}

juce::StringArray PresetManager::removePresets(const juce::StringArray& ids) const {
  juce::StringArray removed;
  for (const auto& id : ids)
    if (remove(id))
      removed.add(id);
  return removed;
}
