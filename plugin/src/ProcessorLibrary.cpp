#include "Processor.h"

// The tone library's processor half: everything that needs the chain or the
// local-load pipeline. The file layer itself (paths, listing, copying,
// naming) is ToneLibrary; this file is the bridge between it and the rest
// of the plugin.
//
// The guiding rule is that the library adds a *place to keep tones*, not a
// second kind of tone: loading an entry hands its path to
// loadLocalTonePath, so what lands in the chain is the same local block a
// dropped file makes (background load, model cache, undo, presets, DAW
// state, all unchanged), and saving a block writes the bytes its model
// cache already holds, which is why filing a tone downloaded from TONE3000
// works exactly like filing a local one.

namespace {

juce::var libraryError(const juce::String& message) {
  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  obj->setProperty("error", message);
  return juce::var(obj.get());
}

// Sanity cap on one filed model, matching the local-load limit.
constexpr juce::int64 kMaxLibraryFileBytes = 50 * 1024 * 1024;

}  // namespace

juce::var TONE3000Processor::listLibrary(const juce::String& folderPath) const {
  return library.list(folderPath);
}

juce::var TONE3000Processor::loadLibraryTone(const juce::String& itemPath,
                                             const std::string& targetInsertId) {
  const juce::File item = library.resolve(itemPath);
  if (item == juce::File())
    return libraryError("That tone isn't in the library");
  if (!item.exists())
    return libraryError("That tone is no longer in the library");
  // A folder loads as one multi-model block by the same rules as a folder
  // drop; a file loads as a single-model one.
  return loadLocalTonePath(item, targetInsertId);
}

juce::var TONE3000Processor::saveBlockToLibrary(const std::string& blockId,
                                                const juce::String& folderPath) {
  juce::String toneTitle;
  juce::String modelName;
  bool isNam = true;
  std::vector<uint8_t> bytes;

  {
    // Copying the model bytes is O(model size) — up to kMaxLibraryFileBytes —
    // and it happens under chainMutex, which processBlock falls back to
    // *blocking* on when its ScopedTryLock misses. Fade the chain out first,
    // like every other message-thread hold of this lock (see ChainEditFade):
    // the render thread then takes the wait-free silent path instead of
    // stalling behind the copy.
    ChainEditFade fade(*this);
    juce::ScopedLock lock(chainMutex);
    const ChainBlock* block = findBlockById(blockId);
    if (block == nullptr || block->type == ChainBlockType::INSERT)
      return libraryError("Nothing to save here");

    // The bytes come from the block's own cache, so filing a tone never
    // downloads anything: a block that is still loading (or failed) simply
    // has nothing to file yet.
    const auto cached = block->modelCache.find(block->activeModelId);
    if (cached == block->modelCache.end() || cached->second.empty())
      return libraryError(block->loadFailed ? "This tone failed to load"
                                            : "This tone is still loading");
    bytes = cached->second;
    isNam = block->type == ChainBlockType::NAM;
    toneTitle = block->toneVar["title"].toString();
    if (const auto* models = block->toneVar["models"].getArray())
      for (const auto& model : *models)
        if (static_cast<int>(model["id"]) == block->activeModelId)
          modelName = model["name"].toString();
  }

  if (static_cast<juce::int64>(bytes.size()) > kMaxLibraryFileBytes)
    return libraryError("That model is too large to file");

  // "<tone> - <model>" is what makes a filed capture findable later (one
  // amp, many captures), but the two collapse to one name when the model
  // adds nothing (single-model tones, and local tones whose model name is
  // the file name the tone is titled after).
  juce::String name = toneTitle.trim();
  const juce::String model = modelName.trim();
  if (model.isNotEmpty() && !model.equalsIgnoreCase(name))
    name = name.isEmpty() ? model : name + " - " + model;
  if (name.isEmpty())
    name = isNam ? "Capture" : "IR";

  const juce::var result =
      library.write(folderPath, name + (isNam ? ".nam" : ".wav"), bytes.data(), bytes.size());
  if (result["error"].isVoid())
    juce::Logger::writeToLog("[Library] Saved '" + result["name"].toString() + "' to " +
                             (folderPath.isEmpty() ? juce::String("the library root")
                                                   : folderPath));
  return result;
}

juce::var TONE3000Processor::importFilesToLibrary(const juce::String& folderPath,
                                                  const juce::var& files) {
  const auto* fileArray = files.getArray();
  if (fileArray == nullptr || fileArray->isEmpty())
    return libraryError("Nothing to add");

  int copied = 0;
  juce::String firstError;
  juce::String lastPath;
  for (const auto& file : *fileArray) {
    const juce::String filename = file["name"].toString();
    auto note = [&](const juce::String& message) {
      if (firstError.isEmpty())
        firstError = message;
      juce::Logger::writeToLog("[Library] " + filename + ": " + message);
    };

    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, file["data"].toString()) ||
        decoded.getDataSize() == 0) {
      note("Couldn't read the file");
      continue;
    }
    if (static_cast<juce::int64>(decoded.getDataSize()) > kMaxLibraryFileBytes) {
      note("File is too large");
      continue;
    }
    // Validated exactly like a drop on a tile: the library is a place tones
    // load *from*, so a file that could never load has no business in it.
    if (const juce::String problem = validateLocalModelBytes(filename, decoded);
        problem.isNotEmpty()) {
      note(problem);
      continue;
    }

    const juce::var written =
        library.write(folderPath, filename, decoded.getData(), decoded.getDataSize());
    if (!written["error"].isVoid()) {
      note(written["error"].toString());
      continue;
    }
    lastPath = written["path"].toString();
    ++copied;
  }

  // A mixed drop still files what it can; only a drop where nothing landed
  // reports the failure (which for a single file is exactly that file's).
  if (copied == 0)
    return libraryError(firstError.isNotEmpty() ? firstError : "Couldn't add those files");

  juce::Logger::writeToLog("[Library] Added " + juce::String(copied) + " of " +
                           juce::String(fileArray->size()) + " dropped file(s)");
  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("copied", copied);
  result->setProperty("skipped", fileArray->size() - copied);
  result->setProperty("path", lastPath);
  return juce::var(result.get());
}

juce::var TONE3000Processor::importPathToLibrary(const juce::String& folderPath,
                                                 const juce::File& source) {
  const juce::var result = library.importFrom(folderPath, source);
  if (result["error"].isVoid())
    juce::Logger::writeToLog("[Library] Imported '" + source.getFileName() + "' (" +
                             result["copied"].toString() + " file(s))");
  return result;
}

juce::var TONE3000Processor::createLibraryFolder(const juce::String& parentPath,
                                                 const juce::String& name, bool unique) {
  return library.createFolder(parentPath, name, unique);
}

juce::var TONE3000Processor::renameLibraryItem(const juce::String& itemPath,
                                               const juce::String& newName) {
  return library.rename(itemPath, newName);
}

juce::var TONE3000Processor::moveLibraryItem(const juce::String& itemPath,
                                             const juce::String& destFolderPath) {
  return library.move(itemPath, destFolderPath);
}

bool TONE3000Processor::removeLibraryItem(const juce::String& itemPath) {
  // Blocks already in the chain keep playing: they load from the local
  // stash (and carry their own model bytes in presets and DAW state), so
  // removing the library copy never pulls a tone out from under a rig.
  const bool removed = library.remove(itemPath);
  if (removed)
    juce::Logger::writeToLog("[Library] Removed " + itemPath);
  return removed;
}

juce::String TONE3000Processor::revealLibraryFolder(const juce::String& folderPath) {
  juce::File folder = library.resolve(folderPath);
  if (folder == juce::File())
    return {};
  if (!folder.isDirectory() && !folder.createDirectory().wasOk())
    return {};
  // startAsProcess opens the folder itself in Finder/Explorer; revealToUser
  // would only select it inside its parent.
  if (!folder.startAsProcess())
    return {};
  return folder.getFullPathName();
}
