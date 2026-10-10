#include "LocalFiles.h"

namespace t3k::ui {

LocalFiles::LocalFiles(ChainStore& chain, Toast& toast) : chain_(chain), toast_(toast) {}

// Destroying the chooser dismisses the dialog; its callback never fires.
LocalFiles::~LocalFiles() = default;

void LocalFiles::report(const juce::String& error) {
  if (error.isNotEmpty()) toast_.show(error);
}

void LocalFiles::drop(const std::string& targetBlockId, const juce::StringArray& paths) {
  if (paths.isEmpty()) return;
  report(chain_.loadLocalTonePath(juce::File(paths[0]), targetBlockId));
}

void LocalFiles::pick(const std::string& targetBlockId, Kind kind) {
  if (chooser_ != nullptr) return;
  const bool folder = kind == Kind::folder;

#if JUCE_IOS
  // iOS has no usable folder route: the document picker's folder URLs are
  // security scoped and can't be enumerated, so "Load Folder" asks for the
  // files themselves and the same many-models-at-once path runs on them.
  chooser_ = std::make_unique<juce::FileChooser>(folder ? "Load Files" : "Load File", juce::File{},
                                                 juce::String("*.nam;*.wav"));
  const int flags = juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectFiles |
                    (folder ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
#else
  chooser_ = std::make_unique<juce::FileChooser>(folder ? "Load Folder" : "Load File", juce::File{},
                                                 folder ? juce::String("*") : juce::String("*.nam;*.wav"));
  const int flags = juce::FileBrowserComponent::openMode |
                    (folder ? juce::FileBrowserComponent::canSelectDirectories
                            : juce::FileBrowserComponent::canSelectFiles);
#endif

  juce::WeakReference<LocalFiles> self(this);
  chooser_->launchAsync(flags, [self, target = targetBlockId](const juce::FileChooser& chooser) {
    if (self == nullptr) return;
    // Release the chooser once its callback unwinds (it is the caller).
    juce::MessageManager::callAsync([self] {
      if (self != nullptr) self->chooser_.reset();
    });
#if JUCE_IOS
    // URL results, not paths: the picked files live outside the sandbox and
    // are only readable through the security scope JUCE bookmarked.
    const auto results = chooser.getURLResults();
    if (results.isEmpty()) return;
    self->report(self->chain_.loadLocalToneUrls(results, target));
#else
    const auto results = chooser.getResults();
    if (results.isEmpty()) return;
    self->report(self->chain_.loadLocalTonePath(results.getReference(0), target));
#endif
  });
}

}  // namespace t3k::ui
