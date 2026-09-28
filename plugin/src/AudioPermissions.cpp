#include "AudioPermissions.h"

#if JUCE_ANDROID

namespace AudioPermissions {

// Unlike Windows/Linux (no per-app mic gate the plugin can read) and unlike
// iOS/macOS (a queryable tri-state via AVFoundation), Android enforces
// RECORD_AUDIO at the OS level but juce::RuntimePermissions only exposes a
// bool (isGranted): there's no API distinguishing "never asked" from "denied,
// don't ask again" short of Activity.shouldShowRequestPermissionRationale,
// which isn't wrapped by RuntimePermissions. So "not granted" maps to
// unknown, not denied: the same conservative choice the MicStatus enum's own
// doc comment describes for macOS's "not determined", and it keeps the
// settings UI offering a request rather than routing straight to
// openMicSettings on a device that was simply never asked yet.
MicStatus getMicStatus() {
  return juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio)
             ? MicStatus::granted
             : MicStatus::unknown;
}

void requestMicAccess(std::function<void(bool)> onComplete) {
  // RuntimePermissions::request's Android backend resolves its callback from
  // the permission-result Fragment callback, which Android delivers on the
  // main thread - already the JUCE message thread here, so no
  // MessageManager::callAsync bounce is needed (unlike the AVFoundation
  // callbacks in AudioPermissions.mm, which land on an arbitrary queue).
  juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, std::move(onComplete));
}

void openMicSettings() {
  // No AndroidX/Settings wrapper in JUCE's public API, so this fires the
  // android.settings.APPLICATION_DETAILS_SETTINGS intent by hand, the same
  // low-level JNI pattern JUCE's own Process::openDocument uses internally
  // (juce_Files_android.cpp) for ACTION_VIEW - AndroidIntent/AndroidContext/
  // AndroidUri and getEnv()/getAppContext()/getCurrentActivity() are all
  // public juce_core Android JNI helpers (juce_JNIHelpers_android.h).
  auto* env = juce::getEnv();

  auto packageName = env->CallObjectMethod(juce::getAppContext().get(), juce::AndroidContext.getPackageName);
  auto uriString = juce::javaString("package:" + juce::juceString((jstring)packageName));
  auto uri = juce::LocalRef<jobject>(
      env->CallStaticObjectMethod(juce::AndroidUri, juce::AndroidUri.parse, uriString.get()));
  auto action = juce::javaString("android.settings.APPLICATION_DETAILS_SETTINGS");
  auto intent = juce::LocalRef<jobject>(
      env->NewObject(juce::AndroidIntent, juce::AndroidIntent.constructWithUri, action.get(), uri.get()));

  env->CallVoidMethod(juce::getCurrentActivity().get(), juce::AndroidContext.startActivity, intent.get());
}

}  // namespace AudioPermissions

#else

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

#endif  // JUCE_ANDROID
