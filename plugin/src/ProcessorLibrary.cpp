#include "Processor.h"

#include <mutex>
#include <optional>

// #############################
// LIBRARY
// #############################
//
// The local Library (Library.h, plugin/docs/library.md) is a file layer; the
// processor wraps its edits for one reason: a preset loaded from the Library
// is the active preset by path ("file:<path>"), so an edit that renames,
// moves or removes that file (or a folder above it) re-points or clears the
// active preset. The pill keeps the right name, Save keeps writing to the
// right folder and folder stepping keeps walking the right setlist.

void TONE3000Processor::setLibraryLocation(const juce::File& root, const juce::String& owner,
                                           const juce::Array<juce::File>& linkedDirs) {
  const juce::ScopedLock lock(libraryLock);
  library.setLocation(root, owner, linkedDirs);
}

void TONE3000Processor::prewarmLibrary() {
  // The editor's per-machine prefs (LibraryStore's kRootPref / kOwnerPref /
  // kLinksPref). Nothing linked and no Library folder yet: nothing to warm.
  juce::PropertiesFile prefs(uiPreferencesOptions());
  const juce::String rootPath = prefs.getValue("t3k.libraryRoot");
  const juce::File root = juce::File::isAbsolutePath(rootPath) ? juce::File(rootPath) : LocalLibrary::defaultRoot();
  juce::Array<juce::File> links;
  if (const auto* paths = juce::JSON::parse(prefs.getValue("t3k.libraryLinks")).getArray())
    for (const auto& path : *paths)
      if (juce::File::isAbsolutePath(path.toString()))
        links.add(juce::File(path.toString()));
  if (links.isEmpty() && !root.isDirectory())
    return;
  setLibraryLocation(root, prefs.getValue("t3k.libraryOwner", LocalLibrary::kDefaultOwner), links);
  libraryPrewarm = std::thread([this] {
    juce::Thread::setCurrentThreadName("TONE3000 Library prewarm");
    getLibrary(/*fresh=*/false);  // fills the process-wide listing cache
  });
}

namespace {

// The saved listing (see getSavedLibrary), one per process like the scan
// cache. File: the location key on the first line, the listing's JSON after.
struct SavedListing {
  std::mutex lock;
  juce::String checkedKey;  // the location this process has scanned ("" = none yet)
  juce::String savedKey;    // the location `saved` / `json` are for
  juce::var saved;          // last session's listing, parsed (until checked)
  juce::var latest;         // the newest listing this process scanned (prewarm, a drawer)
  juce::String latestKey;   // the location `latest` is for
  juce::String json;        // what the file holds for savedKey (skips unchanged writes)
  bool read = false;        // the file was read for savedKey
};

SavedListing& savedListing() {
  static SavedListing listing;
  return listing;
}

juce::File savedListingFile() {
#if HEADLESS
  // The test build: never the machine's real listing.
  return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-test-library-listing.cache");
#endif
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("library-listing.cache");
}

// Read the file once per location (caller holds the lock).
void readSavedListing(SavedListing& listing, const juce::String& key) {
  if (listing.read && listing.savedKey == key)
    return;
  listing.read = true;
  listing.savedKey = key;
  listing.saved = juce::var();
  listing.json = {};
  const juce::String text = savedListingFile().loadFileAsString();
  if (text.upToFirstOccurrenceOf("\n", false, false) != key)
    return;
  listing.json = text.fromFirstOccurrenceOf("\n", false, false);
}

}  // namespace

void TONE3000Processor::forgetLibraryScanForTesting() {
  auto& listing = savedListing();
  const std::lock_guard<std::mutex> lock(listing.lock);
  listing.latest = juce::var();
  listing.latestKey = {};
  listing.checkedKey = {};
  listing.savedKey = {};
  listing.saved = juce::var();
  listing.json = {};
  listing.read = false;
}

juce::var TONE3000Processor::getSavedLibrary() const {
  juce::String key;
  {
    const juce::ScopedLock lock(libraryLock);
    key = library.locationKey();
  }
  auto& listing = savedListing();
  const std::lock_guard<std::mutex> lock(listing.lock);
  // This session has scanned it (the prewarm at load, or a drawer): that
  // listing, at once, while the drawer's own walk checks it.
  if (listing.checkedKey == key)
    return listing.latestKey == key ? listing.latest : juce::var();
  readSavedListing(listing, key);
  if (!listing.saved.isObject() && listing.json.isNotEmpty())
    listing.saved = juce::JSON::parse(listing.json);
  return listing.saved;
}

juce::var TONE3000Processor::getLibrary(bool fresh, const std::atomic<bool>* stop) const {
  // A copy, so the scan (seconds for a big linked folder) holds no lock.
  std::optional<LocalLibrary> snapshot;
  {
    const juce::ScopedLock lock(libraryLock);
    snapshot.emplace(library);
  }
  juce::var tree = snapshot->scan(!fresh, stop);  // the snapshot shares the listing cache
  const auto key = snapshot->locationKey();
  const int rescanned = tree.getProperty("rescanned", 0);
  if (auto* obj = tree.getDynamicObject())
    obj->removeProperty("rescanned");
  if (tree.getProperty("truncated", false) || tree.getProperty("cancelled", false))
    return tree;  // not worth keeping a partial listing

  // Save it for the next session when it could differ from what is saved:
  // the first scan here, or one that listed a folder again.
  auto& listing = savedListing();
  {
    const std::lock_guard<std::mutex> lock(listing.lock);
    // The newest listing: what a drawer opened later shows at once.
    listing.latest = tree;
    listing.latestKey = key;
    if (listing.checkedKey == key && rescanned == 0)
      return tree;
  }
  const juce::String json = juce::JSON::toString(tree, true);  // a big listing: ~0.25 s, on this worker
  const std::lock_guard<std::mutex> lock(listing.lock);
  listing.checkedKey = key;
  readSavedListing(listing, key);
  listing.saved = juce::var();  // superseded: free it
  if (json == listing.json)
    return tree;
  listing.json = json;
  const auto file = savedListingFile();
  const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
  // "\n" as written (not the platform's CRLF): the first line is read back as the key.
  if (temp.replaceWithText(key + "\n" + json, false, false, "\n") && !temp.moveFileTo(file))
    temp.deleteFile();
  return tree;
}

juce::File TONE3000Processor::activePresetFile() const {
  juce::String id;
  {
    juce::ScopedLock lock(chainMutex);
    id = activePresetId;
  }
  return presetManager.fileForId(id);
}

void TONE3000Processor::relinkActivePreset(const juce::File& activeFile, const juce::File& from,
                                           const juce::File& to) {
  if (activeFile == juce::File() || (activeFile != from && !activeFile.isAChildOf(from)))
    return;

  juce::String id, name;
  if (to != juce::File()) {
    const juce::File moved =
        activeFile == from ? to : to.getChildFile(activeFile.getRelativePathFrom(from));
    // Its id in the new place: "user:" back in the user folder, else by path.
    for (const auto& entry : presetManager.listFolder(moved.getParentDirectory()))
      if (entry.file == moved) {
        id = entry.info.id;
        name = entry.info.name;
      }
  }

  juce::ScopedLock lock(chainMutex);
  activePresetId = id;
  if (id.isNotEmpty())
    activePresetName = name;
  else
    activePresetName.clear();
  bumpChainRevision();
}

void TONE3000Processor::libraryTouched(std::initializer_list<juce::File> files) {
  const juce::File userDir = presetManager.userPresetsDir();
  for (const auto& file : files)
    if (file != juce::File() && (file == userDir || file.getParentDirectory() == userDir)) {
      hostProgramInfoCache.clear();
      updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
      return;
    }
}

juce::File TONE3000Processor::libraryCreateFolder(const juce::File& parent, const juce::String& name) {
  return library.createFolder(parent, name);
}

juce::File TONE3000Processor::libraryRename(const juce::File& item, const juce::String& newName) {
  const juce::File active = activePresetFile();
  const bool ownLibrary = item == library.ownDir();
  const juce::File renamed = library.rename(item, newName);
  if (renamed == juce::File())
    return {};
  if (ownLibrary) {
    const juce::ScopedLock lock(libraryLock);
    library.setLocation(library.rootDir(), renamed.getFileName(), library.linkedDirs());
  }
  relinkActivePreset(active, item, renamed);
  relinkLocalFiles(item, renamed);
  libraryTouched({item, renamed});
  return renamed;
}

bool TONE3000Processor::libraryRemove(const juce::File& item) {
  const juce::File active = activePresetFile();
  if (!library.remove(item))
    return false;
  relinkActivePreset(active, item, {});
  libraryTouched({item});
  return true;
}

juce::File TONE3000Processor::libraryMove(const juce::File& item, const juce::File& folder) {
  const juce::File active = activePresetFile();
  const juce::File moved = library.move(item, folder);
  if (moved == juce::File() || moved == item)
    return moved;
  relinkActivePreset(active, item, moved);
  relinkLocalFiles(item, moved);
  libraryTouched({item, moved});
  return moved;
}

juce::File TONE3000Processor::libraryCopy(const juce::File& item, const juce::File& folder) {
  const juce::File copied = library.copy(item, folder);
  libraryTouched({copied});
  return copied;
}

juce::File TONE3000Processor::libraryAddTone(const juce::File& folder, const juce::var& ref) {
  return library.addToneRef(folder, ref);
}

juce::File TONE3000Processor::libraryAddCapture(const juce::File& folder, const juce::File& source,
                                                const juce::String& name) {
  return library.addCapture(folder, source, name);
}

juce::File TONE3000Processor::libraryWriteCapture(const juce::File& folder, const std::vector<uint8_t>& bytes,
                                                  bool ir, const juce::String& name) {
  if (bytes.empty())
    return {};
  // Only a real model: a download can be an error page.
  if (const auto problem = localModelProblem(ir ? "model.wav" : "model.nam", bytes.data(), bytes.size());
      problem.isNotEmpty()) {
    juce::Logger::writeToLog("[Library] Not keeping " + name + ": " + problem);
    return {};
  }
  // Through a temporary file, so it lands the way any capture does (the
  // same one already there is handed back, a taken name numbered).
  const juce::TemporaryFile temp(ir ? ".wav" : ".nam");
  if (!temp.getFile().replaceWithData(bytes.data(), bytes.size()))
    return {};
  const juce::File added = library.addCapture(folder, temp.getFile(), name);
  libraryTouched({added});
  return added;
}

juce::File TONE3000Processor::libraryKeepModel(const std::string& blockId, const juce::File& folder,
                                               const juce::String& name) {
  std::vector<uint8_t> bytes;
  bool ir = false;
  {
    juce::ScopedLock lock(chainMutex);
    const ChainBlock* block = findBlockById(blockId);
    if (block == nullptr || block->type == ChainBlockType::INSERT)
      return {};
    const auto cached = block->modelCache.find(block->activeModelId);
    if (cached == block->modelCache.end())
      return {};
    bytes = cached->second;
    ir = block->type == ChainBlockType::IR;  // the playing model's engine
  }
  return libraryWriteCapture(folder, bytes, ir, name);
}

void TONE3000Processor::libraryDownloadModel(const juce::String& modelUrl, bool ir, const juce::File& folder,
                                             const juce::String& name, std::function<void(juce::File)> done) {
  libraryDownloads.addJob([this, alive = downloadsAlive, modelUrl, ir, folder, name, done = std::move(done)] {
    if (!alive->load())
      return;  // the instance is going: no fetch
    const auto bytes = fetchModelFromUrl(modelUrl);
    // Written on the message thread: the Library's edits all happen there.
    // Not when the instance has gone meanwhile (`alive`).
    juce::MessageManager::callAsync([this, alive, bytes, ir, folder, name, done] {
      if (alive->load() && done)
        done(libraryWriteCapture(folder, bytes, ir, name));
    });
  });
}

bool TONE3000Processor::libraryExport(const juce::File& item, const juce::File& archive) const {
  return library.exportArchive(item, archive);
}

juce::File TONE3000Processor::libraryImport(const juce::File& archive) {
  const juce::File imported = library.importArchive(archive);
  libraryImported(imported);
  return imported;
}

void TONE3000Processor::libraryImported(const juce::File& imported) {
  if (imported == library.ownDir()) {
    hostProgramInfoCache.clear();
    updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  }
}

void TONE3000Processor::libraryMoveAsync(const juce::File& item, const juce::File& folder,
                                         std::function<void(juce::File)> done) {
  const juce::File active = activePresetFile();
  runLibraryJob([item, folder](const LocalLibrary& library) { return juce::var(library.move(item, folder).getFullPathName()); },
                [this, item, active, done = std::move(done)](juce::var result) {
                  const juce::File moved = result.toString().isEmpty() ? juce::File() : juce::File(result.toString());
                  if (moved != juce::File() && moved != item) {
                    relinkActivePreset(active, item, moved);
                    relinkLocalFiles(item, moved);
                    libraryTouched({item, moved});
                  }
                  if (done) done(moved);
                });
}

void TONE3000Processor::libraryCopyAsync(const juce::File& item, const juce::File& folder,
                                         std::function<void(juce::File)> done) {
  runLibraryJob([item, folder](const LocalLibrary& library) { return juce::var(library.copy(item, folder).getFullPathName()); },
                [this, done = std::move(done)](juce::var result) {
                  const juce::File copied = result.toString().isEmpty() ? juce::File() : juce::File(result.toString());
                  if (copied != juce::File())
                    libraryTouched({copied});
                  if (done) done(copied);
                });
}

void TONE3000Processor::libraryRemoveAsync(const juce::File& item, std::function<void(bool)> done) {
  const juce::File active = activePresetFile();
  runLibraryJob([item](const LocalLibrary& library) { return juce::var(library.remove(item)); },
                [this, item, active, done = std::move(done)](juce::var result) {
                  const bool removed = static_cast<bool>(result);
                  if (removed) {
                    relinkActivePreset(active, item, {});
                    libraryTouched({item});
                  }
                  if (done) done(removed);
                });
}

void TONE3000Processor::libraryCopyFilesAsync(const juce::Array<juce::File>& files, const juce::File& folder,
                                              std::function<void(juce::var)> done) {
  runLibraryJob(
      [files, folder](const LocalLibrary& library) {
        int copied = 0, skipped = 0;
        juce::String last;
        for (const auto& file : files)
          if (const auto to = library.copy(file, folder); to != juce::File()) {
            ++copied;
            last = to.getFullPathName();
          } else {
            ++skipped;
          }
        auto* o = new juce::DynamicObject();
        o->setProperty("copied", copied);
        o->setProperty("skipped", skipped);
        o->setProperty("last", last);
        return juce::var(o);
      },
      [this, done = std::move(done)](juce::var result) {
        if (const auto last = result["last"].toString(); last.isNotEmpty())
          libraryTouched({juce::File(last).getParentDirectory()});
        if (done) done(result);
      });
}

void TONE3000Processor::runLibraryJob(std::function<juce::var(const LocalLibrary&)> work,
                                      std::function<void(juce::var)> done) {
  std::shared_ptr<LocalLibrary> snapshot;
  {
    const juce::ScopedLock lock(libraryLock);
    snapshot = std::make_shared<LocalLibrary>(library);
  }
  libraryDownloads.addJob([alive = downloadsAlive, snapshot, work = std::move(work), done = std::move(done)] {
    if (!alive->load())
      return;
    const juce::var result = work(*snapshot);
    juce::MessageManager::callAsync([alive, result, done] {
      if (alive->load() && done)
        done(result);
    });
  });
}
