#include "AudioPermissions.h"

namespace AudioPermissions {

// Windows and Linux don't expose a per-app microphone gate we can read here
// (Windows' privacy toggle needs the WinRT AppCapability APIs, unavailable in
// this static plugin context), so report "granted" and never nag. If a user
// ever does need the Windows privacy page, openMicSettings jumps there.
MicStatus getMicStatus() {
  return MicStatus::granted;
}

void requestMicAccess(std::function<void(bool)> onComplete) {
  if (onComplete)
    onComplete(true);
}

void openMicSettings() {
#if JUCE_WINDOWS
  juce::URL("ms-settings:privacy-microphone").launchInDefaultBrowser();
#endif
}

}  // namespace AudioPermissions
