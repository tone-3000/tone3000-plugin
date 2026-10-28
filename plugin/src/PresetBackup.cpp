#include "PresetManager.h"

#include <algorithm>

namespace {
constexpr int kMaxPresets = 4096;
constexpr juce::int64 kMaxPresetBytes = 256LL * 1024 * 1024;
constexpr juce::int64 kMaxBackupBytes = 2LL * 1024 * 1024 * 1024;
constexpr const char* kManifest = "backup.json";

struct ScratchDirectory {
  juce::File dir;
  bool keep = false;
  explicit ScratchDirectory(const juce::File& parent)
      : dir(parent.getChildFile("t3k-backup-" + juce::Uuid().toString())) {}
  ~ScratchDirectory() { if (!keep) dir.deleteRecursively(); }
};

bool validPreset(const juce::ValueTree& tree) {
  return tree.hasType(PresetManager::kPresetTag) &&
         static_cast<int>(tree.getProperty("schemaVersion", 1)) == 1 &&
         tree.getChildWithName("ChainSnapshot").isValid() &&
         tree.getChildWithName("Params").isValid();
}

juce::Result fail(const juce::String& message) { return juce::Result::fail(message); }
}

juce::Result PresetManager::exportBackup(const juce::File& archive) const {
  const auto all = entries();
  const auto count = std::count_if(all.begin(), all.end(), [](const Entry& e) { return !e.info.factory; });
  if (count == 0) return fail("No user presets to export. Save a preset first.");
  if (count > kMaxPresets) return fail("Too many presets for one backup.");
  // A damaged file must not silently disappear from a backup.
  if (userDir.findChildFiles(juce::File::findFiles, false, "*.t3kpreset").size() != count)
    return fail("A user preset could not be read. The backup was not created.");

  ScratchDirectory scratch(juce::File::getSpecialLocation(juce::File::tempDirectory));
  if (scratch.dir.createDirectory().failed()) return fail("Could not create the backup folder.");
  juce::ZipFile::Builder zip;
  juce::Array<juce::var> files;
  juce::int64 total = 0;
  for (const auto& entry : all) {
    if (entry.info.factory) continue;
    if (entry.file.getSize() > kMaxPresetBytes || (total += entry.file.getSize()) > kMaxBackupBytes)
      return fail("The presets are too large for one backup.");
    auto tree = t3k::presetfile::read(entry.file);
    if (!validPreset(tree)) return fail("Could not read preset: " + entry.info.name);
    // Pin legacy/duplicate-file ids before changing filenames inside the ZIP.
    tree.setProperty("id", entry.info.id.fromFirstOccurrenceOf("user:", false, false), nullptr);
    tree.setProperty("name", entry.info.name, nullptr);
    const auto filename = juce::String(files.size() + 1).paddedLeft('0', 4) + " " +
                          t3k::presetfile::sanitizeStem(entry.info.name) + kFileExtension;
    const auto file = scratch.dir.getChildFile(filename);
    if (!t3k::presetfile::write(file, tree)) return fail("Could not prepare preset: " + entry.info.name);
    files.add(filename);
    zip.addFile(file, 6, filename);
  }
  juce::DynamicObject::Ptr manifest = new juce::DynamicObject();
  manifest->setProperty("format", "TONE3000 user preset backup");
  manifest->setProperty("version", 1);
  manifest->setProperty("presets", files); // array order is the user/MIDI order
  const auto manifestFile = scratch.dir.getChildFile(kManifest);
  if (!manifestFile.replaceWithText(juce::JSON::toString(juce::var(manifest.get()))))
    return fail("Could not write the backup index.");
  zip.addFile(manifestFile, 6, kManifest);
  juce::TemporaryFile output(archive);
  {
    juce::FileOutputStream stream(output.getFile());
    if (!stream.openedOk() || !zip.writeToStream(stream, nullptr)) return fail("Could not write the backup ZIP.");
    stream.flush();
    if (stream.getStatus().failed()) return fail("Could not finish writing the backup ZIP.");
  }
  return output.overwriteTargetFileWithTemporary() ? juce::Result::ok() : fail("Could not save the backup ZIP.");
}

juce::Result PresetManager::importBackup(const juce::File& archive, t3k::PresetImportMode mode) const {
  const bool replaceAll = mode == t3k::PresetImportMode::replaceAll;
  juce::ZipFile zip(archive);
  if (zip.getNumEntries() < 2 || zip.getNumEntries() > kMaxPresets + 1)
    return fail("This is not a TONE3000 preset backup.");
  std::set<juce::String> names;
  juce::int64 total = 0;
  for (int i = 0; i < zip.getNumEntries(); ++i) {
    const auto* entry = zip.getEntry(i);
    const auto& name = entry->filename;
    // No paths or symlinks are ever extracted. Only our flat file format.
    if (entry->isSymbolicLink || name.isEmpty() || name.containsAnyOf("/\\:") ||
        name == "." || name == ".." || !names.insert(name).second ||
        (name != kManifest && !name.endsWith(kFileExtension)) || entry->uncompressedSize <= 0 ||
        entry->uncompressedSize > (name == kManifest ? 1024 * 1024 : kMaxPresetBytes) ||
        (total += entry->uncompressedSize) > kMaxBackupBytes)
      return fail("The backup contains invalid or oversized files.");
  }
  const auto manifestIndex = zip.getIndexOfFileName(kManifest);
  if (manifestIndex < 0) return fail("The backup index is missing.");
  std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(manifestIndex));
  if (!manifestStream) return fail("Could not read the backup index.");
  juce::MemoryOutputStream manifestBytes;
  const auto expectedManifestBytes = zip.getEntry(manifestIndex)->uncompressedSize;
  if (manifestBytes.writeFromInputStream(*manifestStream, expectedManifestBytes + 1) != expectedManifestBytes)
    return fail("The backup index is truncated or corrupt.");
  const auto json = manifestBytes.toUTF8();
  const auto manifest = juce::JSON::parse(json);
  const auto presetFiles = manifest.getProperty("presets", {});
  const auto* files = presetFiles.getArray();
  if (manifest.getProperty("format", "").toString() != "TONE3000 user preset backup" ||
      static_cast<int>(manifest.getProperty("version", 0)) != 1 || files == nullptr ||
      files->isEmpty() || files->size() != zip.getNumEntries() - 1)
    return fail("The backup index is invalid or uses an unsupported version.");

  // Keep staging on the store's volume so commits are renames. Nothing in
  // the live store is touched until every preset has been read and validated.
  if (userDir.createDirectory().failed()) return fail("Could not open the presets folder.");
  ScratchDirectory scratch(userDir);
  if (scratch.dir.createDirectory().failed()) return fail("Could not create the import folder.");
  std::set<juce::String> usedNames;
  juce::StringArray order;
  juce::StringArray factoryOrder;
  for (const auto& entry : entries()) {
    if (entry.info.factory) factoryOrder.add(entry.info.id);
    if (!replaceAll) {
      order.add(entry.info.id);
      if (!entry.info.factory) usedNames.insert(entry.info.name.toLowerCase());
    }
  }
  std::set<juce::String> imported;
  std::vector<juce::File> staged;
  for (const auto& item : *files) {
    const auto filename = item.toString();
    const int index = zip.getIndexOfFileName(filename);
    if (!item.isString() || filename == kManifest || index < 0 || !imported.insert(filename).second)
      return fail("The backup index refers to missing or repeated presets.");
    std::unique_ptr<juce::InputStream> input(zip.createStreamForEntry(index));
    if (!input) return fail("Could not read a preset from the ZIP.");
    const auto source = scratch.dir.getChildFile("source.tmp");
    {
      juce::FileOutputStream stream(source);
      const auto size = zip.getEntry(index)->uncompressedSize;
      if (!stream.openedOk() || stream.writeFromInputStream(*input, size + 1) != size)
        return fail("A preset in the backup is truncated or corrupt.");
      stream.flush();
      if (stream.getStatus().failed()) return fail("Could not unpack the backup.");
    }
    auto tree = t3k::presetfile::read(source);
    source.deleteFile();
    if (!validPreset(tree)) return fail("The backup contains an invalid preset.");
    const auto original = tree.getProperty("name", "Preset").toString().trim();
    const auto baseName = original.isEmpty() ? juce::String("Preset") : original;
    auto name = baseName;
    for (int n = 2; usedNames.count(name.toLowerCase()) != 0; ++n)
      name = baseName + " (" + juce::String(n) + ")";
    usedNames.insert(name.toLowerCase());
    const auto id = juce::Uuid().toString();
    tree.setProperty("id", id, nullptr);
    tree.setProperty("name", name, nullptr);
    const auto file = t3k::presetfile::uniqueFile(scratch.dir, t3k::presetfile::sanitizeStem(name));
    if (!t3k::presetfile::write(file, tree)) return fail("Could not stage the imported presets.");
    staged.push_back(file);
    order.add("user:" + id);
  }

  // Keep an independent ZIP after a successful replacement, too. Create it
  // only after validating every incoming preset and before touching the store.
  const auto oldFiles = userDir.findChildFiles(juce::File::findFiles, false, "*.t3kpreset");
  if (replaceAll && orderFile().exists() && !orderFile().existsAsFile())
    return fail("Could not replace the preset order. No presets were replaced.");
  if (replaceAll && !oldFiles.isEmpty()) {
    const auto backups = userDir.getSiblingFile("PresetBackups");
    if (backups.createDirectory().failed()) return fail("Could not create the recovery backup folder. No presets were replaced.");
    const auto recovery = backups.getNonexistentChildFile(
        "TONE3000-before-restore-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"), ".zip");
    const auto result = exportBackup(recovery);
    if (result.failed()) return fail("Could not back up existing presets. No presets were replaced. " + result.getErrorMessage());
  }

  std::vector<juce::File> committed;
  std::vector<std::pair<juce::File, juce::File>> originals;
  auto rollback = [&]() {
    bool ok = true;
    for (const auto& file : committed)
      if (file.exists() && !file.deleteFile()) ok = false;
    for (auto it = originals.rbegin(); it != originals.rend(); ++it)
      if (!it->first.moveFileTo(it->second)) ok = false;
    if (!ok) scratch.keep = true; // never delete the only copy on rollback failure
    return ok;
  };
  auto restoreFailure = [&](const juce::String& message) {
    return rollback() ? fail(message) : fail(message + " Recovery files were kept in " + scratch.dir.getFullPathName());
  };
  if (replaceAll) {
    const auto previous = scratch.dir.getChildFile("previous");
    if (previous.createDirectory().failed()) return fail("Could not prepare the restore. No presets were replaced.");
    auto preserve = [&](const juce::File& original) {
      const auto saved = previous.getChildFile(original.getFileName());
      if (!original.moveFileTo(saved)) return false;
      originals.emplace_back(saved, original);
      return true;
    };
    for (const auto& original : oldFiles)
      if (!preserve(original)) return restoreFailure("Could not replace existing presets.");
    if (orderFile().exists() && !preserve(orderFile()))
      return restoreFailure("Could not replace the preset order.");
    // Retain the factory order but put the restored user section first.
    order.addArray(factoryOrder);
  }
  for (const auto& file : staged) {
    const auto target = t3k::presetfile::uniqueFile(userDir, file.getFileNameWithoutExtension());
    if (!file.moveFileTo(target)) {
      return restoreFailure("Could not save the imported presets.");
    }
    committed.push_back(target);
  }
  // Atomic order update; writeOrder's normal replaceWithText is not a
  // transaction with our files. Roll back the additions on a write failure.
  juce::Array<juce::var> ids;
  for (const auto& id : order) ids.add(id);
  juce::TemporaryFile orderTemp(orderFile());
  if (!orderTemp.getFile().replaceWithText(juce::JSON::toString(juce::var(ids))) ||
      !orderTemp.overwriteTargetFileWithTemporary()) {
    return restoreFailure("Could not save the imported preset order.");
  }
  return juce::Result::ok();
}
