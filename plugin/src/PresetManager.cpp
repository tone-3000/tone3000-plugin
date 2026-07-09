#include "PresetManager.h"
#include <algorithm>

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

}  // namespace

PresetManager::PresetManager()
    : userDir(presetsRootDir()), factoryDir(presetsRootDir().getChildFile("Factory")) {}

juce::ValueTree PresetManager::readPresetFile(const juce::File& file) {
  if (!file.existsAsFile())
    return {};
  std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
  if (xml == nullptr)
    return {};
  juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
  return tree.hasType(kPresetTag) ? tree : juce::ValueTree();
}

bool PresetManager::writePresetFile(const juce::File& file, const juce::ValueTree& preset) {
  std::unique_ptr<juce::XmlElement> xml(preset.createXml());
  if (xml == nullptr)
    return false;
  return xml->writeTo(file);
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
  return presets;
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

  // Same-name save overwrites that preset (keeps its id) — the update path.
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
