#include "ToneLibrary.h"

#include <algorithm>

namespace {

/** Same app-data root as PresetManager and the local-model stash. */
juce::File defaultLibraryRoot() {
  juce::File base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile("TONE3000").getChildFile("Library");
}

juce::var errorResult(const juce::String& message) {
  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  obj->setProperty("error", message);
  return juce::var(obj.get());
}

/** NAM and IR files under `dir`, subfolders included, counted separately.
    Lazy and bounded: RangedDirectoryIterator yields as it walks, so the
    cap actually stops the walk (findChildFiles would build the whole
    listing first), and a folder someone pointed at their sample drive
    can't stall the browser. Symlinks are not followed — the count runs on
    the message thread and a link loop would never return. */
struct ModelCounts {
  int nam = 0;
  int ir = 0;
  /** Everything the plugin can load in here. */
  int total() const { return nam + ir; }
  /** What loading this folder would actually add: one tone per file of the
      majority extension (see loadLocalTonePath). */
  int loadable() const { return std::max(nam, ir); }
};

ModelCounts countModelFiles(const juce::File& dir) {
  constexpr int limit = 1000;
  ModelCounts counts;
  if (!dir.isDirectory())
    return counts;
  for (const auto& entry : juce::RangedDirectoryIterator(dir, true, "*", juce::File::findFiles,
                                                         juce::File::FollowSymlinks::no)) {
    const juce::File file = entry.getFile();
    if (file.getFileExtension().equalsIgnoreCase(".nam"))
      ++counts.nam;
    else if (file.getFileExtension().equalsIgnoreCase(".wav"))
      ++counts.ir;
    if (counts.total() >= limit)
      break;
  }
  return counts;
}

/** Natural name order ("amp 2" before "amp 10"), the order the local-load
    pipeline puts a folder's models in. */
void sortByName(juce::Array<juce::File>& files) {
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
    return a.getFileName().compareNatural(b.getFileName()) < 0;
  });
}

}  // namespace

ToneLibrary::ToneLibrary() : root(defaultLibraryRoot()) {}

ToneLibrary::ToneLibrary(const juce::File& rootDirectory) : root(rootDirectory) {}

bool ToneLibrary::isModelFile(const juce::File& file) {
  const juce::String extension = file.getFileExtension().toLowerCase();
  return extension == ".nam" || extension == ".wav";
}

juce::String ToneLibrary::kindOf(const juce::File& file) {
  return file.getFileExtension().equalsIgnoreCase(".nam") ? "nam" : "ir";
}

juce::String ToneLibrary::sanitizeName(const juce::String& name) {
  // createLegalFileName strips separators and the platform's reserved
  // characters; the rest guards against names that are legal but would make
  // an entry unreachable (leading dots hide it, trailing dots/spaces are
  // silently dropped by Windows).
  // Truncate *before* stripping: cutting to length last can put a trailing
  // dot back on a long name, and Windows drops those silently, so the file
  // would land under a different name than the one handed back to the UI.
  juce::String clean = juce::File::createLegalFileName(name).trim().substring(0, 120);
  while (clean.startsWithChar('.'))
    clean = clean.substring(1).trim();
  while (clean.endsWithChar('.'))
    clean = clean.dropLastCharacters(1).trim();
  return clean;
}

juce::File ToneLibrary::uniqueChild(const juce::File& dir, const juce::String& base,
                                    const juce::String& extension) {
  juce::File candidate = dir.getChildFile(base + extension);
  for (int n = 2; candidate.exists() && n < 1000; ++n)
    candidate = dir.getChildFile(base + " (" + juce::String(n) + ")" + extension);
  // Out of suffixes: hand back nothing rather than a name that is taken.
  // The callers write with replaceWithData / copyFileTo, which would
  // otherwise destroy one of the user's own captures to report success.
  return candidate.exists() ? juce::File() : candidate;
}

juce::File ToneLibrary::resolve(const juce::String& relativePath) const {
  const juce::String path = relativePath.replaceCharacter('\\', '/').trim();
  if (path.isEmpty())
    return root;
  // Rejected outright rather than sanitized: these only ever come from our
  // own UI, so anything shaped like an escape is a bug or an attack, not a
  // typo to be helpful about.
  if (juce::File::isAbsolutePath(path) || path.startsWithChar('/'))
    return {};

  juce::File file = root;
  for (const auto& segment : juce::StringArray::fromTokens(path, "/", {})) {
    if (segment.isEmpty() || segment == "." || segment == "..")
      return {};
    file = file.getChildFile(segment);
  }
  // Belt and braces on the path itself: getChildFile takes an absolute
  // segment ("C:" on Windows) as the whole path, and this catches the
  // result landing outside the library. It is a path check, not a symlink
  // one: the library folder is the user's own, and a link they put inside
  // it pointing elsewhere is their business — but nothing derived from a
  // *string* can escape.
  return file.isAChildOf(root) ? file : juce::File();
}

juce::String ToneLibrary::relativePathOf(const juce::File& file) const {
  if (file == root)
    return {};
  if (!file.isAChildOf(root))
    return {};
  return file.getRelativePathFrom(root).replaceCharacter('\\', '/');
}

juce::var ToneLibrary::list(const juce::String& relativePath) const {
  const juce::File dir = resolve(relativePath);
  if (dir == juce::File())
    return errorResult("That folder isn't in the library");
  // The root is created on first listing so the folder exists for the user
  // to drop files into from Finder/Explorer before the plugin ever writes.
  if (dir == root && !dir.isDirectory())
    dir.createDirectory();  // best effort; a failure falls through as "gone"
  if (!dir.isDirectory())
    return errorResult("That folder is gone");

  juce::Array<juce::File> childFolders =
      dir.findChildFiles(juce::File::findDirectories, false);
  juce::Array<juce::File> childFiles = dir.findChildFiles(juce::File::findFiles, false);
  sortByName(childFolders);
  sortByName(childFiles);

  juce::Array<juce::var> folders;
  for (const auto& folder : childFolders) {
    if (folder.isHidden())
      continue;
    juce::DynamicObject::Ptr entry = new juce::DynamicObject();
    entry->setProperty("name", folder.getFileName());
    entry->setProperty("path", relativePathOf(folder));
    entry->setProperty("models", countModelFiles(folder).total());
    folders.add(juce::var(entry.get()));
  }

  juce::Array<juce::var> models;
  for (const auto& file : childFiles) {
    if (!isModelFile(file) || file.isHidden())
      continue;
    juce::DynamicObject::Ptr entry = new juce::DynamicObject();
    entry->setProperty("name", file.getFileNameWithoutExtension());
    entry->setProperty("path", relativePathOf(file));
    entry->setProperty("kind", kindOf(file));
    entry->setProperty("size", file.getSize());
    entry->setProperty("modified", file.getLastModificationTime().toMilliseconds());
    models.add(juce::var(entry.get()));
  }

  // What loading this folder as one block would add: its whole tree's
  // majority extension, not the files listed above (which are only its
  // direct children). The menu's "Load all" row says this number.
  const ModelCounts counts = countModelFiles(dir);

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  const juce::String path = relativePathOf(dir);
  result->setProperty("path", path);
  result->setProperty("name", dir == root ? juce::String("Library") : dir.getFileName());
  // Void (not "") at the root, so the UI can tell "no parent" from "the
  // parent is the root".
  result->setProperty("parent", dir == root ? juce::var()
                                            : juce::var(relativePathOf(dir.getParentDirectory())));
  result->setProperty("folders", folders);
  result->setProperty("models", models);
  result->setProperty("loadable", counts.loadable());
  return juce::var(result.get());
}

juce::var ToneLibrary::createFolder(const juce::String& parentPath, const juce::String& name,
                                    bool unique) const {
  const juce::File parent = resolve(parentPath);
  if (parent == juce::File())
    return errorResult("That folder isn't in the library");

  const juce::String clean = sanitizeName(name);
  if (clean.isEmpty())
    return errorResult("Enter a folder name");

  // `unique` is for the importer, which must never fail on a name it did
  // not choose; the New Folder action wants the collision reported.
  const juce::File folder = unique ? uniqueChild(parent, clean, {}) : parent.getChildFile(clean);
  if (folder == juce::File())
    return errorResult("Too many folders with that name in the library");
  if (folder.exists())
    return errorResult("A folder with that name already exists");
  if (!folder.createDirectory().wasOk())
    return errorResult("Couldn't create the folder");

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("path", relativePathOf(folder));
  result->setProperty("name", folder.getFileName());
  return juce::var(result.get());
}

juce::var ToneLibrary::rename(const juce::String& relativePath,
                              const juce::String& newName) const {
  const juce::File file = resolve(relativePath);
  if (file == juce::File() || file == root)
    return errorResult("That item isn't in the library");
  if (!file.exists())
    return errorResult("That item is gone");

  const juce::String clean = sanitizeName(newName);
  if (clean.isEmpty())
    return errorResult("Enter a name");

  // Files keep their extension: it decides NAM vs IR downstream, so it's
  // ours to manage, not something a rename should be able to break.
  const juce::String extension = file.isDirectory() ? juce::String() : file.getFileExtension();
  const juce::File target = file.getParentDirectory().getChildFile(clean + extension);
  // Renaming to the same name is a no-op, not a collision (the browser
  // commits on blur as well as Enter).
  if (target == file) {
    juce::DynamicObject::Ptr unchanged = new juce::DynamicObject();
    unchanged->setProperty("path", relativePathOf(file));
    unchanged->setProperty("name", file.isDirectory() ? file.getFileName()
                                                      : file.getFileNameWithoutExtension());
    return juce::var(unchanged.get());
  }
  if (target.exists())
    return errorResult("A " + juce::String(file.isDirectory() ? "folder" : "file") +
                       " with that name already exists");
  if (!file.moveFileTo(target))
    return errorResult("Couldn't rename that");

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("path", relativePathOf(target));
  result->setProperty("name", target.isDirectory() ? target.getFileName()
                                                   : target.getFileNameWithoutExtension());
  return juce::var(result.get());
}

juce::var ToneLibrary::move(const juce::String& relativePath,
                             const juce::String& destFolderPath) const {
  const juce::File item = resolve(relativePath);
  if (item == juce::File() || item == root)
    return errorResult("That item isn't in the library");
  if (!item.exists())
    return errorResult("That item is gone");

  const juce::File dest = resolve(destFolderPath);
  if (dest == juce::File())
    return errorResult("That folder isn't in the library");
  if (!dest.isDirectory())
    return errorResult("That folder is gone");

  // Dropped on the folder it is already in: leave it be. Falling through
  // would treat the item as its own name collision and rename it "(2)".
  if (item.getParentDirectory() == dest) {
    juce::DynamicObject::Ptr unchanged = new juce::DynamicObject();
    unchanged->setProperty("path", relativePathOf(item));
    return juce::var(unchanged.get());
  }
  // isAChildOf walks real ancestors, so "Pack 2" is not mistaken for being
  // inside "Pack". Stated here rather than left to the OS refusing the
  // rename, whose behaviour (and JUCE's copy fallback) differs per platform.
  if (item.isDirectory() && (dest == item || dest.isAChildOf(item)))
    return errorResult("A folder can't be moved into itself");

  // A folder has no extension to keep, even when its name has a dot in it
  // ("Twin v1.2"), so the " (2)" suffix goes on the whole name.
  const bool isFolder = item.isDirectory();
  const juce::File target =
      uniqueChild(dest, isFolder ? item.getFileName() : item.getFileNameWithoutExtension(),
                  isFolder ? juce::String() : item.getFileExtension());
  if (target == juce::File())
    return errorResult("Too many items with that name in that folder");
  if (!item.moveFileTo(target))
    return errorResult("Couldn't move that");

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("path", relativePathOf(target));
  return juce::var(result.get());
}

bool ToneLibrary::remove(const juce::String& relativePath) const {
  const juce::File file = resolve(relativePath);
  if (file == juce::File() || file == root || !file.exists())
    return false;
  // The user's own captures: prefer the trash so a mis-click is recoverable
  // from the OS, and only really delete where there is no trash to move to.
  if (file.moveToTrash())
    return true;
  return file.isDirectory() ? file.deleteRecursively() : file.deleteFile();
}

juce::var ToneLibrary::importFrom(const juce::String& folderPath,
                                  const juce::File& source) const {
  const juce::File folder = resolve(folderPath);
  if (folder == juce::File())
    return errorResult("That folder isn't in the library");
  if (!folder.createDirectory().wasOk())
    return errorResult("Couldn't open the library folder");
  if (!source.exists())
    return errorResult("That file is gone");
  if (source.isAChildOf(root) || source == root)
    return errorResult("That's already in the library");

  if (!source.isDirectory()) {
    if (!isModelFile(source))
      return errorResult("Only .nam and .wav files are supported");
    const juce::File target =
        uniqueChild(folder, source.getFileNameWithoutExtension(), source.getFileExtension());
    if (target == juce::File())
      return errorResult("Too many files with that name in the library");
    if (!source.copyFileTo(target))
      return errorResult("Couldn't copy that file");

    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("path", relativePathOf(target));
    result->setProperty("name", target.getFileNameWithoutExtension());
    result->setProperty("copied", 1);
    return juce::var(result.get());
  }

  // A folder comes in whole (its own subfolders included), minus the files
  // the plugin can't load: the tree is the structure the user already made,
  // and flattening it here would throw that away.
  juce::Array<juce::File> files;
  for (const auto& entry : juce::RangedDirectoryIterator(source, true, "*", juce::File::findFiles,
                                                         juce::File::FollowSymlinks::no))
    if (isModelFile(entry.getFile()))
      files.add(entry.getFile());
  if (files.isEmpty())
    return errorResult("No .nam or .wav files in that folder");

  const juce::File target = uniqueChild(folder, source.getFileName(), {});
  if (target == juce::File())
    return errorResult("Too many folders with that name in the library");
  int copied = 0;
  for (const auto& file : files) {
    const juce::File destination =
        target.getChildFile(file.getRelativePathFrom(source).replaceCharacter('\\', '/'));
    if (destination.getParentDirectory().createDirectory().wasOk() && file.copyFileTo(destination))
      ++copied;
  }
  if (copied == 0) {
    target.deleteRecursively();
    return errorResult("Couldn't copy those files");
  }

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("path", relativePathOf(target));
  result->setProperty("name", target.getFileName());
  result->setProperty("copied", copied);
  return juce::var(result.get());
}

juce::var ToneLibrary::write(const juce::String& folderPath, const juce::String& name,
                             const void* data, size_t size) const {
  // `name` may carry a relative subpath ("Marshall/JCM800.nam"): a dropped
  // folder arrives as a flat list of files whose shape lives in these
  // names, and it should land in the library with that shape intact, the
  // same as one copied in through the picker. Each segment is sanitized and
  // resolved, so the subpath can no more escape the library than any other.
  // Split on the last separator by index: JUCE's upToLastOccurrenceOf and
  // fromLastOccurrenceOf both return the *whole* string when the separator
  // isn't there, which for a plain "amp.nam" would file it under a folder
  // of its own name.
  const int slash = name.lastIndexOfChar('/');
  const juce::String subfolder = slash >= 0 ? name.substring(0, slash) : juce::String();
  const juce::String filename = slash >= 0 ? name.substring(slash + 1) : name;

  juce::String targetPath = folderPath;
  for (const auto& segment : juce::StringArray::fromTokens(subfolder, "/", {})) {
    const juce::String clean = sanitizeName(segment);
    if (clean.isEmpty())
      continue;
    targetPath = targetPath.isEmpty() ? clean : targetPath + "/" + clean;
  }

  const juce::File folder = resolve(targetPath);
  if (folder == juce::File())
    return errorResult("That folder isn't in the library");
  if (!folder.createDirectory().wasOk())
    return errorResult("Couldn't open the library folder");

  const juce::String extension = filename.fromLastOccurrenceOf(".", true, false).toLowerCase();
  const juce::String base = sanitizeName(filename.upToLastOccurrenceOf(".", false, false));
  if (base.isEmpty() || (extension != ".nam" && extension != ".wav"))
    return errorResult("Only .nam and .wav files are supported");

  const juce::File target = uniqueChild(folder, base, extension);
  if (target == juce::File())
    return errorResult("Too many files with that name in the library");
  if (!target.replaceWithData(data, size))
    return errorResult("Couldn't write to the library folder");

  juce::DynamicObject::Ptr result = new juce::DynamicObject();
  result->setProperty("path", relativePathOf(target));
  result->setProperty("name", target.getFileNameWithoutExtension());
  return juce::var(result.get());
}
