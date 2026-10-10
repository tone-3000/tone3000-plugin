#pragma once
// What the Library knows about a library's files beyond the files: kept
// links (a copy -> what it was kept from) and folder pictures, plus, in your
// own library, the keep folder, the library order and the linked folders.
// Kept in the library folder itself (kFileName, the pictures beside it in
// kPicturesFolder) with paths relative to it, so it goes wherever the
// folder goes: a backup, a copy to another machine, a move of the Library.
// The UI's prefs hold the working copy (LibraryStore mirrors them here);
// export and import carry it along (LocalLibrary). See library.md "Library
// state".
//
//   { "version": 1,
//     "kept":     { "<copy>": { "tone": ..., "model": ... }      a TONE3000 tone's model
//                             | { "source": "<file>" }           a file in this library
//                             | { "source_path": "<absolute>" }, a file elsewhere
//     "pictures": { "<folder, lower case>": "<file in kPicturesFolder>" },
//     "keep":  "<folder>",           your own library only: relative to the Library
//     "order": [ "<library>" ],      folder (a library's name) unless absolute
//     "links": [ "<absolute>" ] }    or a "tone3000:" one
//
// A key is relative to the library ('/'-separated, "." for the library
// itself) or, for something outside every library (a linked collection),
// absolute: those live in your own library's file.
#include <juce_core/juce_core.h>

namespace t3k::library_state {

inline constexpr const char* kFileName = ".t3klibrary.json";
inline constexpr const char* kPicturesFolder = ".t3kpictures";
inline constexpr int kVersion = 1;

// `file`'s path inside `dir` ("." for `dir`), '/'-separated; empty when it
// isn't inside.
inline juce::String relative(const juce::File& file, const juce::File& dir) {
  if (file == dir) return ".";
  if (!file.isAChildOf(dir)) return {};
  return file.getRelativePathFrom(dir).replaceCharacter('\\', '/');
}

// A key back to a file: absolute as it is, else inside `dir` (nothing that
// climbs out of it).
inline juce::File resolve(const juce::File& dir, const juce::String& key) {
  if (key.isEmpty()) return {};
  if (juce::File::isAbsolutePath(key)) return juce::File(key);
  if (key == ".") return dir;
  const auto file = dir.getChildFile(key);
  return file.isAChildOf(dir) ? file : juce::File();
}

// The same for the lower-cased paths pictures are keyed by: plain string
// work, since a lower-cased path names no file on a case-sensitive disk.
inline juce::String lowerRelative(const juce::String& lowPath, const juce::File& dir) {
  auto base = dir.getFullPathName().toLowerCase().replaceCharacter('\\', '/');
  if (base.endsWithChar('/')) base = base.dropLastCharacters(1);  // a drive's root ("e:/")
  const auto path = lowPath.replaceCharacter('\\', '/');
  if (path == base) return ".";
  return path.startsWith(base + "/") ? path.substring(base.length() + 1) : juce::String();
}

inline juce::String lowerResolve(const juce::File& dir, const juce::String& key) {
  if (key.isEmpty()) return {};
  if (juce::File::isAbsolutePath(key)) return key.toLowerCase();
  if (juce::StringArray::fromTokens(key, "/\\", "").contains("..")) return {};
  const auto base = dir.getFullPathName().toLowerCase();
  if (key == ".") return base;
  return base + juce::File::getSeparatorString() +
         key.toLowerCase().replaceCharacter('/', juce::File::getSeparatorChar());
}

// A capture's bytes, as a short string (64-bit FNV-1a and the size): what a
// copy kept from TONE3000 records, so a shared preset's block can be told to
// hold exactly those bytes (LocalLibrary::shareArchive). Not cryptographic:
// it guards against mistakes (a file replaced under the same name), not
// against someone forging a match on purpose.
inline juce::String contentHash(const void* data, size_t size) {
  juce::uint64 hash = 14695981039346656037ull;
  const auto* bytes = static_cast<const juce::uint8*>(data);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
  return juce::String::toHexString(static_cast<juce::int64>(hash)).paddedLeft('0', 16) + ":" + juce::String(static_cast<juce::int64>(size));
}
inline juce::String contentHash(const juce::MemoryBlock& block) { return contentHash(block.getData(), block.getSize()); }

// The id a local model gets from its bytes (positive, content-stable; see
// stashLocalBytes): what tells a file found by its name from another one
// with that name. Its FNV-1a starts from the processor's own basis,
// 1469598103934665603 (not FNV's standard 14695981039346656037, which
// contentHash uses): every id ever saved in a session or preset was made
// with it, so it must not change.
inline int localModelId(const void* data, size_t size) {
  juce::uint64 hash = 1469598103934665603ull;
  const auto* bytes = static_cast<const juce::uint8*>(data);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
  return static_cast<int>(hash % 0x7ffffffe) + 1;
}

inline juce::File fileOf(const juce::File& dir) { return dir.getChildFile(kFileName); }
inline juce::File picturesOf(const juce::File& dir) { return dir.getChildFile(kPicturesFolder); }

// Changes when the file does (-1: none).
inline juce::int64 stamp(const juce::File& dir) {
  const auto file = fileOf(dir);
  return file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() * 1000003 + file.getSize() : -1;
}

// The most a state file may be (thousands of links are a few MB).
inline constexpr juce::int64 kMaxBytes = juce::int64(32) << 20;

// Held across a read-merge-write of a state file: the UI's saves (message
// thread) and an import's merge (a library job) never interleave.
inline juce::CriticalSection& fileLock() {
  static juce::CriticalSection lock;
  return lock;
}

// Always an object (empty when there is no file, it can't be read, or it is
// too big to be one).
inline juce::var read(const juce::File& dir) {
  const auto file = fileOf(dir);
  auto parsed = file.existsAsFile() && file.getSize() <= kMaxBytes ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
  return parsed.isObject() ? parsed : juce::var(new juce::DynamicObject());
}

inline bool holdsAnything(const juce::var& state) {
  for (const char* section : {"kept", "pictures", "folders"})
    if (const auto* o = state[section].getDynamicObject(); o != nullptr && !o->getProperties().isEmpty()) return true;
  for (const char* list : {"order", "links"})
    if (const auto* a = state[list].getArray(); a != nullptr && !a->isEmpty()) return true;
  return state["keep"].toString().isNotEmpty();
}

// Written whole, through a temporary file; nothing to hold removes the
// file. Unchanged content isn't rewritten. False when it couldn't be.
inline bool write(const juce::File& dir, const juce::var& state) {
  const auto file = fileOf(dir);
  if (!holdsAnything(state)) return !file.exists() || file.deleteFile();
  if (!dir.isDirectory()) return false;
  if (auto* o = state.getDynamicObject()) o->setProperty("version", kVersion);
  const auto text = juce::JSON::toString(state);
  if (file.existsAsFile() && file.loadFileAsString() == text) return true;
  // Through a temporary file of its own, replacing the old one in one step.
  const auto temp = file.getSiblingFile(file.getFileName() + "." + juce::Uuid().toString() + ".tmp");
  if (temp.replaceWithText(text) && temp.replaceFileIn(file)) return true;
  temp.deleteFile();
  return false;
}

}  // namespace t3k::library_state
