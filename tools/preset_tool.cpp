// Maintainer tool for .t3kpreset files (not part of any shipped build).
//
//   PresetTool info <file-or-dir>      framing, id, name, size per preset
//   PresetTool migrate <dir>           rewrite every preset in the v2 framing
//                                      (PresetFile.h) under its readable
//                                      filename, keeping its id, with its
//                                      parameter ids current (LegacyParamIds.h)
//   PresetTool rename <file> <name>    change a preset's display name (and
//                                      filename), keeping its id
//
// `migrate` keeps resources/factory-presets in the shipped form: every file
// in the v2 framing, named after its display name, with its id inside and
// current parameter ids. A legacy <uuid>.t3kpreset keeps that uuid as its
// id (the same rule the plugin applies), so "factory:<uuid>" references in
// users' DAW projects and order.json files keep resolving after the rename.
// Idempotent: a file already in that form is left alone. Safe on a user's
// Presets folder too, though the plugin migrates those lazily on its own.
// Build: cmake --build build --target PresetTool
#include "LegacyParamIds.h"
#include "PresetFile.h"

#include <algorithm>
#include <cstdio>

namespace presetfile = t3k::presetfile;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage:\n"
               "  PresetTool info <file-or-dir>\n"
               "  PresetTool migrate <dir>\n"
               "  PresetTool rename <file> <name>\n");
  return 2;
}

juce::Array<juce::File> presetsIn(const juce::File& target) {
  juce::Array<juce::File> files;
  if (target.isDirectory())
    files = target.findChildFiles(juce::File::findFiles, false,
                                  "*" + juce::String(presetfile::kExtension));
  else if (target.existsAsFile())
    files.add(target);
  std::sort(files.begin(), files.end());
  return files;
}

int info(const juce::File& target) {
  const auto files = presetsIn(target);
  if (files.isEmpty()) {
    std::fprintf(stderr, "no presets at %s\n", target.getFullPathName().toRawUTF8());
    return 1;
  }
  for (const auto& file : files) {
    const presetfile::Header header = presetfile::readHeader(file);
    if (!header.valid) {
      std::printf("%-40s  INVALID\n", file.getFileName().toRawUTF8());
      continue;
    }
    std::printf("%-40s  %s  id=%s  name=\"%s\"  %lld bytes\n", file.getFileName().toRawUTF8(),
                header.legacy ? "v1" : "v2",
                header.id.isEmpty() ? "(stem)" : header.id.toRawUTF8(), header.name.toRawUTF8(),
                static_cast<long long>(file.getSize()));
  }
  return 0;
}

int migrate(const juce::File& dir) {
  if (!dir.isDirectory()) {
    std::fprintf(stderr, "not a directory: %s\n", dir.getFullPathName().toRawUTF8());
    return 1;
  }
  int failures = 0;
  for (const auto& file : presetsIn(dir)) {
    const juce::String stem = file.getFileNameWithoutExtension();
    const presetfile::Header header = presetfile::readHeader(file);
    if (!header.valid) {
      std::fprintf(stderr, "skip (unreadable): %s\n", file.getFileName().toRawUTF8());
      ++failures;
      continue;
    }
    juce::ValueTree preset = presetfile::read(file);
    if (!preset.isValid()) {
      std::fprintf(stderr, "skip (bad body): %s\n", file.getFileName().toRawUTF8());
      ++failures;
      continue;
    }
    // Same rules as PresetManager: a missing id is the stem (a legacy uuid
    // file keeps the exact id it has always had), the name is the stored
    // one or, failing that, the stem.
    const juce::String id = header.id.isEmpty() ? stem : header.id;
    const juce::String name = header.name.isEmpty() ? stem : header.name;
    preset.setProperty("id", id, nullptr);
    preset.setProperty("name", name, nullptr);
    const int renamedIds =
        t3k::legacy_ids::migrateParamIds(preset.getChildWithName("Params"), "id");

    const juce::File target = presetfile::uniqueFile(dir, presetfile::sanitizeStem(name), file);
    if (!header.legacy && renamedIds == 0 && target == file &&
        target.getFileName() == file.getFileName()) {
      std::printf("ok        %s\n", file.getFileName().toRawUTF8());
      continue;
    }
    if (!presetfile::write(target, preset)) {
      std::fprintf(stderr, "FAILED to write %s\n", target.getFullPathName().toRawUTF8());
      ++failures;
      continue;
    }
    if (target != file && !file.deleteFile()) {
      std::fprintf(stderr, "FAILED to remove %s\n", file.getFullPathName().toRawUTF8());
      ++failures;
      continue;
    }
    if (target == file && target.getFileName() != file.getFileName())
      file.moveFileTo(target);  // case-only spelling change
    std::printf("migrated  %s -> %s  (id=%s, %d parameter ids renamed)\n",
                file.getFileName().toRawUTF8(), target.getFileName().toRawUTF8(), id.toRawUTF8(),
                renamedIds);
  }
  return failures == 0 ? 0 : 1;
}

int renamePreset(const juce::File& file, const juce::String& name) {
  juce::ValueTree preset = presetfile::read(file);
  if (!preset.isValid()) {
    std::fprintf(stderr, "unreadable: %s\n", file.getFullPathName().toRawUTF8());
    return 1;
  }
  if (name.trim().isEmpty()) {
    std::fprintf(stderr, "empty name\n");
    return 2;
  }
  // Same rule as PresetManager::rename: the name changes inside and the
  // file follows it; the id (the stem for a file without one) stays.
  if (preset.getProperty("id").toString().isEmpty())
    preset.setProperty("id", file.getFileNameWithoutExtension(), nullptr);
  preset.setProperty("name", name.trim(), nullptr);
  const juce::File target =
      presetfile::uniqueFile(file.getParentDirectory(), presetfile::sanitizeStem(name.trim()), file);
  if (!presetfile::write(target, preset)) {
    std::fprintf(stderr, "FAILED to write %s\n", target.getFullPathName().toRawUTF8());
    return 1;
  }
  if (target != file && !file.deleteFile()) {
    std::fprintf(stderr, "FAILED to remove %s\n", file.getFullPathName().toRawUTF8());
    return 1;
  }
  std::printf("renamed   %s -> %s\n", file.getFileName().toRawUTF8(), target.getFileName().toRawUTF8());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3)
    return usage();
  const juce::String command(argv[1]);
  const juce::File target = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
  if (command == "info" && argc == 3)
    return info(target);
  if (command == "migrate" && argc == 3)
    return migrate(target);
  if (command == "rename" && argc == 4)
    return renamePreset(target, juce::String::fromUTF8(argv[3]));
  return usage();
}
