#include "AppData.h"

namespace t3k::appdata {

namespace {

constexpr const char* kFolderName = "TONE3000";

#if JUCE_IOS

// The group id the build was configured with ("" when the build has none).
constexpr const char* kAppGroup =
#if defined(T3K_IOS_APP_GROUP)
    T3K_IOS_APP_GROUP;
#else
    "";
#endif

// The process's own container. userApplicationDataDirectory is ~/Library on
// iOS, which is where the earlier iOS builds (and the no-group fallback) put
// the folder.
juce::File containerLibrary() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
}

juce::File resolveRoot() {
  if (*kAppGroup != '\0') {
    const auto group = juce::File::getContainerForSecurityApplicationGroupIdentifier(kAppGroup);
    // The system creates the container's Library the first time it is
    // asked for; a missing entitlement comes back as an empty File.
    if (group != juce::File()) return group.getChildFile("Library").getChildFile(kFolderName);
  }
  return containerLibrary().getChildFile(kFolderName);
}

// Move what an earlier build left in the app's own container into `root`.
// Every earlier iOS build kept the presets and the local-model stash under
// ~/Library/TONE3000 and, through PropertiesFile's default folder, the two
// settings files under ~/Library/Application Support/TONE3000. Each child is
// one rename (atomic, and both containers sit on the same volume), skipped
// when the destination already has it, so a second process starting at the
// same moment, or a later launch, finds nothing left to do. The standalone's
// own TONE3000.settings (audio setup, autosaved state) and the log stay
// where they are: neither is shared.
juce::String adoptLegacyContainer(const juce::File& root) {
  const juce::File library = containerLibrary();
  const juce::File legacyData = library.getChildFile(kFolderName);
  const juce::File legacySettings = library.getChildFile("Application Support").getChildFile(kFolderName);
  const std::pair<juce::File, const char*> sources[] = {
      {legacyData, "Presets"},
      {legacyData, "LocalModels"},
      {legacySettings, "preferences.settings"},
      {legacySettings, "ui-preferences.settings"},
  };
  juce::String report;
  for (const auto& [dir, child] : sources) {
    const juce::File from = dir.getChildFile(child);
    const juce::File to = root.getChildFile(child);
    if (from == to || !from.exists() || to.exists()) continue;
    const bool moved = root.createDirectory() && from.moveFileTo(to);
    report << (report.isEmpty() ? "" : ", ") << child << (moved ? "" : " (FAILED, left in place)");
  }
  return report;
}

#else

juce::File resolveRoot() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  return base.getChildFile(kFolderName);
}

#endif

struct Resolved {
  juce::File root;
  juce::String adopted;
};

const Resolved& resolved() {
  static const Resolved instance = [] {
    Resolved r;
    r.root = resolveRoot();
#if JUCE_IOS
    r.adopted = adoptLegacyContainer(r.root);
#endif
    return r;
  }();
  return instance;
}

}  // namespace

juce::File root() { return resolved().root; }

juce::String adoptionReport() {
  const auto& r = resolved();
  if (r.adopted.isEmpty()) return {};
  return "Adopted into " + r.root.getFullPathName() + ": " + r.adopted;
}

}  // namespace t3k::appdata
