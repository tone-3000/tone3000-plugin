#include "PresetManager.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {

constexpr const char* kUserPrefix = "user:";
constexpr const char* kFactoryPrefix = "factory:";

// Magic prefix for the binary ValueTree preset format.
constexpr char kPresetMagic[] = {'T', '3', 'K', 'B'};

juce::File presetsRootDir() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("Presets");
}

}  // namespace

PresetManager::PresetManager() : PresetManager(presetsRootDir(), defaultSystemFactoryDir()) {}

PresetManager::PresetManager(const juce::File& baseDir, const juce::File& systemFactory)
    : userDir(baseDir),
      factoryDir(baseDir.getChildFile("Factory")),
      systemFactoryDir(systemFactory) {}

juce::File PresetManager::defaultSystemFactoryDir() {
  // Shared all-users location the installers write to. A missing dir just
  // means no shipped presets; scans treat it as empty.
#if JUCE_MAC
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

juce::ValueTree PresetManager::readPresetFile(const juce::File& file) {
  if (!file.existsAsFile())
    return {};

  juce::FileInputStream in(file);
  char magic[sizeof(kPresetMagic)]{};
  if (!in.openedOk() || in.read(magic, sizeof(magic)) != static_cast<int>(sizeof(magic)) ||
      std::memcmp(magic, kPresetMagic, sizeof(magic)) != 0)
    return {};

  juce::ValueTree tree = juce::ValueTree::readFromStream(in);
  return tree.hasType(kPresetTag) ? tree : juce::ValueTree();
}

bool PresetManager::writePresetFile(const juce::File& file, const juce::ValueTree& preset) {
  // Write-then-rename so a crash or full disk mid-write can't clobber an
  // existing preset (the XML writer used to provide this via writeTo).
  juce::TemporaryFile temp(file);
  {
    juce::FileOutputStream out(temp.getFile());
    if (!out.openedOk())
      return false;
    out.write(kPresetMagic, sizeof(kPresetMagic));
    preset.writeToStream(out);
    if (out.getStatus().failed())
      return false;
  }
  return temp.overwriteTargetFileWithTemporary();
}

juce::File PresetManager::fileForId(const juce::String& id) const {
  if (id.startsWith(kUserPrefix))
    return userDir.getChildFile(id.fromFirstOccurrenceOf(kUserPrefix, false, false) +
                                kFileExtension);
  if (id.startsWith(kFactoryPrefix)) {
    const juce::String stem =
        id.fromFirstOccurrenceOf(kFactoryPrefix, false, false) + kFileExtension;
    // User Factory overrides the installer-shipped copy when both exist.
    const juce::File local = factoryDir.getChildFile(stem);
    if (local.existsAsFile())
      return local;
    if (systemFactoryDir != juce::File())
      return systemFactoryDir.getChildFile(stem);
    return {};
  }
  return {};
}

std::vector<PresetManager::Info> PresetManager::list() const {
  auto scan = [](const juce::File& dir, const char* prefix, bool factory) {
    std::vector<Info> out;
    if (!dir.isDirectory())
      return out;
    for (const auto& file :
         dir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kFileExtension))) {
      const juce::ValueTree preset = readPresetFile(file);
      if (!preset.isValid())
        continue;
      Info info;
      info.id = prefix + file.getFileNameWithoutExtension();
      info.name = preset.getProperty("name", file.getFileNameWithoutExtension()).toString();
      info.category = preset.getProperty("category", "").toString();
      info.favorite = static_cast<bool>(preset.getProperty("favorite", false));
      info.factory = factory;
      out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
      return a.name.compareIgnoreCase(b.name) < 0;
    });
    return out;
  };

  // Factory section: system Factory with the user Factory overlaid on top (a
  // local file with the same stem replaces the shipped one), re-sorted by
  // name so the merged section reads like a single folder.
  std::vector<Info> factory = scan(systemFactoryDir, kFactoryPrefix, true);
  for (const auto& info : scan(factoryDir, kFactoryPrefix, true)) {
    const auto it = std::find_if(factory.begin(), factory.end(),
                                 [&info](const Info& existing) { return existing.id == info.id; });
    if (it != factory.end())
      *it = info;
    else
      factory.push_back(info);
  }
  std::sort(factory.begin(), factory.end(), [](const Info& a, const Info& b) {
    return a.name.compareIgnoreCase(b.name) < 0;
  });

  // User presets lead so they own the low MIDI program-change numbers; the
  // factory section follows.
  std::vector<Info> presets = scan(userDir, kUserPrefix, false);
  presets.insert(presets.end(), std::make_move_iterator(factory.begin()),
                 std::make_move_iterator(factory.end()));

  // Apply the custom order: within each section, ordered ids first (in file
  // order), then everything else. The sort is stable over the name-sorted
  // scan above, so presets missing from the order file (new saves, ids from
  // another machine) stay alphabetical after the ordered block, and a
  // missing/empty order file leaves the classic ordering untouched.
  const juce::StringArray order = readOrder();
  if (!order.isEmpty()) {
    auto rank = [&order](const Info& info) {
      const int index = order.indexOf(info.id);
      return index < 0 ? std::numeric_limits<int>::max() : index;
    };
    std::stable_sort(presets.begin(), presets.end(), [&](const Info& a, const Info& b) {
      if (a.factory != b.factory)
        return !a.factory;  // user section always first
      return rank(a) < rank(b);
    });
  }

  const juce::StringArray favs = readFavorites();
  for (auto& p : presets) {
    if (favs.contains(p.id))
      p.favorite = true;
  }

  return presets;
}

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

juce::ValueTree PresetManager::load(const juce::String& id) const {
  return readPresetFile(fileForId(id));
}

PresetManager::Info PresetManager::save(const juce::String& name, juce::ValueTree preset) const {
  if (!userDir.createDirectory()) {
    juce::Logger::writeToLog("[Presets] Failed to create presets directory: " +
                             userDir.getFullPathName());
    return {};
  }

  // Same-name save overwrites that preset (keeps its id); this is the update path.
  juce::File file;
  for (const Info& existing : list())
    if (!existing.factory && existing.name.compareIgnoreCase(name) == 0)
      file = fileForId(existing.id);
  if (file == juce::File())
    file = userDir.getChildFile(juce::Uuid().toString() + kFileExtension);

  preset.setProperty("name", name, nullptr);
  if (!writePresetFile(file, preset)) {
    juce::Logger::writeToLog("[Presets] Failed to write preset file: " + file.getFullPathName());
    return {};
  }

  Info info;
  info.id = kUserPrefix + file.getFileNameWithoutExtension();
  info.name = name;
  info.factory = false;
  return info;
}

bool PresetManager::rename(const juce::String& id, const juce::String& newName) const {
  if (!id.startsWith(kUserPrefix) || newName.trim().isEmpty())
    return false;
  const juce::File file = fileForId(id);
  juce::ValueTree preset = readPresetFile(file);
  if (!preset.isValid())
    return false;
  preset.setProperty("name", newName.trim(), nullptr);
  return writePresetFile(file, preset);
}

bool PresetManager::remove(const juce::String& id) const {
  if (!id.startsWith(kUserPrefix))
    return false;
  juce::StringArray favs = readFavorites();
  if (favs.contains(id)) {
    favs.removeString(id);
    writeFavorites(favs);
  }
  return fileForId(id).deleteFile();
}

juce::File PresetManager::categoriesFile() const {
  return userDir.getChildFile("categories.json");
}

juce::StringArray PresetManager::readCategories() const {
  juce::StringArray out;
  const auto parsed = juce::JSON::parse(categoriesFile().loadFileAsString());
  if (const auto* arr = parsed.getArray()) {
    for (const auto& item : *arr) {
      const juce::String str = item.toString().trim();
      if (str.isNotEmpty())
        out.add(str);
    }
  }
  out.sort(true);
  return out;
}

bool PresetManager::writeCategories(const juce::StringArray& categories) const {
  if (!userDir.createDirectory())
    return false;
  juce::Array<juce::var> list;
  for (const auto& cat : categories)
    list.add(cat);
  return categoriesFile().replaceWithText(juce::JSON::toString(juce::var(list)));
}

juce::StringArray PresetManager::listCategories() const {
  return readCategories();
}

bool PresetManager::addCategory(const juce::String& rawName) const {
  const juce::String name = rawName.trim();
  if (name.isEmpty() || name.length() > kMaxCategoryNameLength)
    return false;

  juce::StringArray categories = readCategories();
  for (const auto& existing : categories) {
    if (existing.compareIgnoreCase(name) == 0)
      return false;
  }

  categories.add(name);
  categories.sort(true);
  return writeCategories(categories);
}

bool PresetManager::deleteCategory(const juce::String& rawName) const {
  const juce::String name = rawName.trim();
  if (name.isEmpty())
    return false;

  juce::StringArray categories = readCategories();
  int foundIndex = -1;
  for (int i = 0; i < categories.size(); ++i) {
    if (categories[i].compareIgnoreCase(name) == 0) {
      foundIndex = i;
      break;
    }
  }
  if (foundIndex < 0)
    return false;

  categories.remove(foundIndex);
  writeCategories(categories);

  // If a category has existing presets within it, these presets go to the root "Your presets" instead of being deleted.
  for (const auto& file : userDir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kFileExtension))) {
    juce::ValueTree preset = readPresetFile(file);
    if (!preset.isValid())
      continue;
    const juce::String cat = preset.getProperty("category", "").toString();
    if (cat.compareIgnoreCase(name) == 0) {
      preset.removeProperty("category", nullptr);
      writePresetFile(file, preset);
    }
  }

  return true;
}

bool PresetManager::setPresetCategory(const juce::String& id, const juce::String& category) const {
  if (!id.startsWith(kUserPrefix))
    return false;

  const juce::File file = fileForId(id);
  juce::ValueTree preset = readPresetFile(file);
  if (!preset.isValid())
    return false;

  const juce::String trimmed = category.trim();
  if (trimmed.isEmpty())
    preset.removeProperty("category", nullptr);
  else
    preset.setProperty("category", trimmed, nullptr);

  return writePresetFile(file, preset);
}

bool PresetManager::movePresetsToCategory(const juce::StringArray& ids, const juce::String& category) const {
  bool anyOk = false;
  for (const auto& id : ids) {
    if (setPresetCategory(id, category))
      anyOk = true;
  }
  return anyOk;
}

juce::File PresetManager::favoritesFile() const {
  return userDir.getChildFile("favorites.json");
}

juce::StringArray PresetManager::readFavorites() const {
  juce::StringArray out;
  const auto parsed = juce::JSON::parse(favoritesFile().loadFileAsString());
  if (const auto* arr = parsed.getArray()) {
    for (const auto& item : *arr) {
      const juce::String str = item.toString().trim();
      if (str.isNotEmpty())
        out.add(str);
    }
  }
  return out;
}

bool PresetManager::writeFavorites(const juce::StringArray& ids) const {
  if (!userDir.createDirectory())
    return false;
  juce::Array<juce::var> list;
  for (const auto& id : ids)
    list.add(id);
  return favoritesFile().replaceWithText(juce::JSON::toString(juce::var(list)));
}

bool PresetManager::setPresetFavorite(const juce::String& id, bool isFavorite) const {
  return setPresetsFavorite(juce::StringArray{id}, isFavorite);
}

bool PresetManager::setPresetsFavorite(const juce::StringArray& ids, bool isFavorite) const {
  if (ids.isEmpty())
    return true;

  juce::StringArray favs = readFavorites();
  for (const auto& id : ids) {
    if (isFavorite) {
      if (!favs.contains(id))
        favs.add(id);
    } else {
      favs.removeString(id);
    }

    if (id.startsWith(kUserPrefix)) {
      const juce::File file = fileForId(id);
      juce::ValueTree preset = readPresetFile(file);
      if (preset.isValid()) {
        preset.setProperty("favorite", isFavorite, nullptr);
        writePresetFile(file, preset);
      }
    }
  }
  writeFavorites(favs);
  return true;
}

PresetManager::Info PresetManager::duplicatePreset(const juce::String& id) const {
  if (!id.startsWith(kUserPrefix))
    return {};

  const juce::File srcFile = fileForId(id);
  juce::ValueTree preset = readPresetFile(srcFile);
  if (!preset.isValid())
    return {};

  if (!userDir.createDirectory())
    return {};

  const juce::String originalName = preset.getProperty("name", "").toString();
  const juce::String copyName = "Copy-" + originalName;
  preset.setProperty("name", copyName, nullptr);

  const juce::File dstFile = userDir.getChildFile(juce::Uuid().toString() + kFileExtension);
  if (!writePresetFile(dstFile, preset))
    return {};

  Info info;
  info.id = kUserPrefix + dstFile.getFileNameWithoutExtension();
  info.name = copyName;
  info.category = preset.getProperty("category", "").toString();
  info.favorite = static_cast<bool>(preset.getProperty("favorite", false));
  info.factory = false;
  return info;
}

std::vector<PresetManager::Info> PresetManager::duplicatePresets(const juce::StringArray& ids) const {
  std::vector<Info> out;
  for (const auto& id : ids) {
    const Info dup = duplicatePreset(id);
    if (dup.id.isNotEmpty())
      out.push_back(dup);
  }
  return out;
}

bool PresetManager::removePresets(const juce::StringArray& ids) const {
  bool anyOk = false;
  for (const auto& id : ids) {
    if (remove(id))
      anyOk = true;
  }
  return anyOk;
}
