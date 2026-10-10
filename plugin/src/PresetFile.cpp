#include "PresetFile.h"

#include <cstring>

namespace t3k::presetfile {

namespace {

constexpr char kMagicV2[] = {'T', '3', 'K', 'H'};
constexpr char kMagicV1[] = {'T', '3', 'K', 'B'};
// A header is two short strings; anything claiming more is not ours.
constexpr int kMaxHeaderBytes = 64 * 1024;

enum class Framing { none, v1, v2 };

Framing readMagic(juce::InputStream& in) {
  char magic[4]{};
  if (in.read(magic, 4) != 4)
    return Framing::none;
  if (std::memcmp(magic, kMagicV2, 4) == 0)
    return Framing::v2;
  if (std::memcmp(magic, kMagicV1, 4) == 0)
    return Framing::v1;
  return Framing::none;
}

// Windows refuses these as filenames regardless of case or of anything after
// the first dot ("CON.t3kpreset" and "con.backup.t3kpreset" are both the
// console device).
bool isWindowsReservedName(const juce::String& stem) {
  static const char* const reserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3",
                                         "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1",
                                         "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
                                         "LPT9"};
  const juce::String base = stem.upToFirstOccurrenceOf(".", false, false).trimEnd();
  for (const char* name : reserved)
    if (base.equalsIgnoreCase(name))
      return true;
  return false;
}

bool isForbiddenFilenameChar(juce::juce_wchar c) {
  return c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
         c == '"' || c == '<' || c == '>' || c == '|';
}

}  // namespace

Header readHeader(const juce::File& file) {
  Header header;
  if (!file.existsAsFile())
    return header;
  juce::FileInputStream in(file);
  if (!in.openedOk())
    return header;

  switch (readMagic(in)) {
    case Framing::v2: {
      const int headerBytes = in.readInt();
      if (headerBytes <= 0 || headerBytes > kMaxHeaderBytes)
        return header;
      juce::MemoryBlock bytes(static_cast<size_t>(headerBytes));
      if (in.read(bytes.getData(), headerBytes) != headerBytes)
        return header;
      const juce::ValueTree tree = juce::ValueTree::readFromData(bytes.getData(), bytes.getSize());
      if (!tree.hasType(kHeaderTag))
        return header;
      header.valid = true;
      header.id = tree.getProperty("id").toString().trim();
      header.name = tree.getProperty("name").toString();
      return header;
    }
    case Framing::v1: {
      const juce::ValueTree tree = juce::ValueTree::readFromStream(in);
      if (!tree.hasType(kTag))
        return header;
      header.valid = true;
      header.legacy = true;
      header.id = tree.getProperty("id").toString().trim();
      header.name = tree.getProperty("name").toString();
      return header;
    }
    case Framing::none:
      return header;
  }
  return header;
}

juce::ValueTree read(const juce::File& file) {
  if (!file.existsAsFile())
    return {};
  juce::FileInputStream in(file);
  if (!in.openedOk())
    return {};

  switch (readMagic(in)) {
    case Framing::v2: {
      const int headerBytes = in.readInt();
      if (headerBytes <= 0 || headerBytes > kMaxHeaderBytes)
        return {};
      in.skipNextBytes(headerBytes);
      break;
    }
    case Framing::v1:
      break;
    case Framing::none:
      return {};
  }
  const juce::ValueTree tree = juce::ValueTree::readFromStream(in);
  return tree.hasType(kTag) ? tree : juce::ValueTree();
}

bool write(const juce::File& file, const juce::ValueTree& preset) {
  juce::ValueTree header(kHeaderTag);
  header.setProperty("id", preset.getProperty("id").toString(), nullptr);
  header.setProperty("name", preset.getProperty("name").toString(), nullptr);
  juce::MemoryOutputStream headerBytes;
  header.writeToStream(headerBytes);

  // Write-then-rename so a crash or full disk mid-write can't clobber an
  // existing preset. The scratch file sits beside the target (same volume,
  // so the rename is a rename) under an extension the *.t3kpreset scan never
  // matches: another plugin instance listing the folder mid-write, or a
  // leftover from a crash, must not show up as a second copy of the preset.
  const juce::File temp = file.getSiblingFile(
      file.getFileName() + "." + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".tmp");
  {
    juce::FileOutputStream out(temp);
    if (!out.openedOk())
      return false;
    out.write(kMagicV2, sizeof(kMagicV2));
    out.writeInt(static_cast<int>(headerBytes.getDataSize()));
    out.write(headerBytes.getData(), headerBytes.getDataSize());
    preset.writeToStream(out);
    out.flush();
    if (out.getStatus().failed()) {
      temp.deleteFile();
      return false;
    }
  }
  if (temp.moveFileTo(file))
    return true;
  temp.deleteFile();
  return false;
}

juce::String sanitizeStem(const juce::String& name) {
  // Forbidden characters become "-"; a run of them becomes one "-" (dashes
  // the user typed are kept as typed).
  juce::String out;
  bool lastWasReplacement = false;
  for (auto t = name.getCharPointer(); !t.isEmpty();) {
    const juce::juce_wchar c = t.getAndAdvance();
    if (isForbiddenFilenameChar(c)) {
      if (!lastWasReplacement)
        out += '-';
      lastWasReplacement = true;
    } else {
      out += juce::String::charToString(c);
      lastWasReplacement = false;
    }
  }

  // Byte cap on a code-point boundary. Done before the trims so a cut that
  // lands on a trailing space or dot is cleaned up by them.
  {
    juce::String capped;
    int bytes = 0;
    for (auto t = out.getCharPointer(); !t.isEmpty();) {
      const juce::juce_wchar c = t.getAndAdvance();
      bytes += static_cast<int>(juce::CharPointer_UTF8::getBytesRequiredFor(c));
      if (bytes > kMaxStemBytes)
        break;
      capped += juce::String::charToString(c);
    }
    out = capped;
  }

  // Leading: whitespace, dots (hidden files), "~" (editor/Office lock-file
  // conventions some sync clients special-case). Trailing: whitespace and
  // dots, which Windows strips on its own.
  while (out.isNotEmpty() && (out[0] == '.' || out[0] == '~' ||
                              juce::CharacterFunctions::isWhitespace(out[0])))
    out = out.substring(1);
  while (out.isNotEmpty() && (out.getLastCharacter() == '.' ||
                              juce::CharacterFunctions::isWhitespace(out.getLastCharacter())))
    out = out.dropLastCharacters(1);

  // Nothing left, or only the dashes that stood in for forbidden characters.
  if (out.isEmpty() || out.containsOnly("-"))
    return "Preset";
  if (isWindowsReservedName(out))
    return "_" + out;
  return out;
}

juce::File uniqueFile(const juce::File& dir, const juce::String& stem, const juce::File& self) {
  // File::operator== follows the platform's filename case rule, so on a
  // case-insensitive volume "abc" renaming to "ABC" finds itself and gets
  // the new spelling without a " 2".
  auto taken = [&self](const juce::File& candidate) {
    return candidate.exists() && !(self != juce::File() && candidate == self);
  };
  juce::File candidate = dir.getChildFile(stem + kExtension);
  for (int n = 2; taken(candidate); ++n)
    candidate = dir.getChildFile(stem + " " + juce::String(n) + kExtension);
  return candidate;
}

}  // namespace t3k::presetfile
