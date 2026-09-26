#include "PresetManager.h"
// For TONE3000Processor::ensureWritableDir: preset saves share the app-data
// folder whose permissions a sudo'd install script can mangle (github
// issue #76).
#include "Processor.h"
#include <algorithm>
#include <cstring>
#include <limits>

#if JUCE_ANDROID
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#endif

namespace {

constexpr const char* kUserPrefix = "user:";
constexpr const char* kFactoryPrefix = "factory:";

#if JUCE_ANDROID
// Android has no shared all-users install location the way the other
// platforms' installers write to (see defaultSystemFactoryDir below), and a
// security-scoped asset in the APK can't be exposed as a plain juce::File
// path directly - so factory presets ride as APK assets, populated straight
// from resources/factory-presets/ via an extra assets source dir in
// android/app/build.gradle.kts (that directory's *contents* land at the APK
// assets root, not nested under a subfolder - a Gradle Copy task into
// src/main/assets/ was tried first and rejected: AGP's own lint tasks read
// src/main/assets without an explicit task dependency on a Copy task
// writing into it, which fails a real "implicit dependency" build
// validation), and are copied out to ordinary internal storage once, the
// first time they're needed. After that they're just files like every
// other platform's Factory dir, including the "a user-Factory file with the
// same stem wins" override contract described in PresetManager.h.
// Re-extraction only happens if the destination hasn't been *fully*
// extracted before (e.g. a fresh install, the user cleared app data, or the
// process was killed mid-extraction) - an app update that ships new/changed
// factory presets does not currently refresh an already-populated
// destination. Completion is tracked with a marker file rather than "the
// directory is non-empty": the latter would treat a run cut short partway
// through the loop below (low memory, user swipe-away) as done, silently
// stranding a partial preset set with no retry short of clearing app data.
juce::File extractFactoryPresetsFromAssets() {
  const auto destDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("TONE3000")
                            .getChildFile("Presets")
                            .getChildFile("SystemFactory");
  const auto extractionCompleteMarker = destDir.getChildFile(".extraction-complete");

  if (extractionCompleteMarker.existsAsFile())
    return destDir;

  auto* env = juce::getEnv();
  auto jAssetManager = env->CallObjectMethod(juce::getAppContext().get(), juce::AndroidContext.getAssets);
  AAssetManager* assetManager = AAssetManager_fromJava(env, jAssetManager);
  if (assetManager == nullptr)
    return destDir;

  // "" lists the APK assets root, where resources/factory-presets/'s
  // contents land (see the Gradle-side comment above).
  AAssetDir* assetDir = AAssetManager_openDir(assetManager, "");
  if (assetDir == nullptr)
    return destDir;

  destDir.createDirectory();

  while (const char* name = AAssetDir_getNextFileName(assetDir)) {
    // The assets root could in principle carry non-preset files; only copy
    // what PresetManager actually scans for.
    if (!juce::String(name).endsWithIgnoreCase(".t3kpreset"))
      continue;

    AAsset* asset = AAssetManager_open(assetManager, name, AASSET_MODE_BUFFER);
    if (asset == nullptr)
      continue;

    const auto length = AAsset_getLength(asset);
    const void* data = AAsset_getBuffer(asset);
    if (data != nullptr) {
      juce::File outFile = destDir.getChildFile(name);
      outFile.replaceWithData(data, (size_t)length);
    }
    AAsset_close(asset);
  }

  AAssetDir_close(assetDir);
  extractionCompleteMarker.create();
  return destDir;
}
#endif

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
#elif JUCE_ANDROID
  // No installer and no shared all-users location on Android either, same
  // reasoning as iOS above - but unlike iOS's bundle (a real, directly
  // readable juce::File path), an Android APK asset needs AAssetManager to
  // read at all, so the presets are copied out to internal storage once
  // (see extractFactoryPresetsFromAssets) rather than read from the asset
  // path directly on every scan.
  return extractFactoryPresetsFromAssets();
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
  // ensureWritableDir rather than a bare createDirectory: the folder can
  // exist and still be unwritable (root-owned after a sudo'd script), and
  // that state used to fail every save with this same log line forever.
  if (!TONE3000Processor::ensureWritableDir(userDir)) {
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
