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

PresetManager::PresetManager() : PresetManager(presetsRootDir()) {}

PresetManager::PresetManager(const juce::File& baseDir)
    : userDir(baseDir), factoryDir(baseDir.getChildFile("Factory")) {}

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
  if (id.startsWith(kFactoryPrefix))
    return factoryDir.getChildFile(id.fromFirstOccurrenceOf(kFactoryPrefix, false, false) +
                                   kFileExtension);
  return {};
}

std::vector<PresetManager::Info> PresetManager::list() const {
  auto scan = [](const juce::File& dir, const char* prefix, bool factory) {
    std::vector<Info> out;
    for (const auto& file :
         dir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kFileExtension))) {
      const juce::ValueTree preset = readPresetFile(file);
      if (!preset.isValid())
        continue;
      Info info;
      info.id = prefix + file.getFileNameWithoutExtension();
      info.name = preset.getProperty("name", file.getFileNameWithoutExtension()).toString();
      info.factory = factory;
      out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
      return a.name.compareIgnoreCase(b.name) < 0;
    });
    return out;
  };

  std::vector<Info> presets = scan(factoryDir, kFactoryPrefix, true);
  std::vector<Info> user = scan(userDir, kUserPrefix, false);
  presets.insert(presets.end(), std::make_move_iterator(user.begin()),
                 std::make_move_iterator(user.end()));

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
        return a.factory;  // factory section always first
      return rank(a) < rank(b);
    });
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

  const auto index = static_cast<size_t>(std::distance(presets.begin(), it));
  const auto target = delta < 0 ? index - 1 : index + 1;  // one step per call
  if (target >= presets.size() || presets[target].factory != it->factory)
    return false;  // already at its section's edge

  juce::StringArray ids;
  for (const Info& info : presets)
    ids.add(info.id);
  ids.getReference(static_cast<int>(index)).swapWith(ids.getReference(static_cast<int>(target)));
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
  return fileForId(id).deleteFile();
}
