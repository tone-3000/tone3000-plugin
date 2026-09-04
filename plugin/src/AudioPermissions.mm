#include "AudioPermissions.h"

#include <juce_events/juce_events.h>  // MessageManager::callAsync

#import <AVFoundation/AVFoundation.h>

#if JUCE_IOS

#import <UIKit/UIKit.h>

namespace AudioPermissions {

// iOS gates audio input the same way macOS does, but through AVAudioSession
// rather than AVCaptureDevice: recordPermission is the session-level answer and
// is what actually decides whether the input bus carries signal. The three
// states map one for one onto MicStatus, so the rest of the plugin (the
// settings banner, the inline alert, the one-click jump to privacy settings)
// works unchanged.
//
// ponytail: AVAudioSession.recordPermission is deprecated since iOS 17 in
// favour of AVAudioApplication, but still works and keeps one code path at a
// deployment target of iOS 16. Split it under `if (@available(iOS 17.0, *))`
// when the deprecation becomes a removal.

MicStatus getMicStatus() {
  const auto fromRecordPermission = [](AVAudioSessionRecordPermission p) {
    switch (p) {
      case AVAudioSessionRecordPermissionGranted:
        return MicStatus::granted;
      case AVAudioSessionRecordPermissionDenied:
        return MicStatus::denied;
      case AVAudioSessionRecordPermissionUndetermined:
        return MicStatus::unknown;
    }
    return MicStatus::unknown;
  };

  return fromRecordPermission([[AVAudioSession sharedInstance] recordPermission]);
}

void requestMicAccess(std::function<void(bool)> onComplete) {
  [[AVAudioSession sharedInstance] requestRecordPermission:^(BOOL granted) {
    // Same contract as the macOS path: the AV callback lands on an arbitrary
    // queue, so bounce to the message thread before touching JUCE objects.
    juce::MessageManager::callAsync([onComplete, granted] {
      if (onComplete)
        onComplete(static_cast<bool>(granted));
    });
  }];
}

void openMicSettings() {
  // iOS has no per-pane privacy URL; UIApplicationOpenSettingsURLString opens
  // this app's own Settings page, where the Microphone switch lives.
  NSURL* url = [NSURL URLWithString:UIApplicationOpenSettingsURLString];
  if (url != nil)
    [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
}

}  // namespace AudioPermissions

#else

#import <AppKit/AppKit.h>

namespace AudioPermissions {

MicStatus getMicStatus() {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized:
      return MicStatus::granted;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:  // parental controls / MDM: can't grant
      return MicStatus::denied;
    case AVAuthorizationStatusNotDetermined:
      return MicStatus::unknown;
  }
  return MicStatus::unknown;
}

void requestMicAccess(std::function<void(bool)> onComplete) {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                           completionHandler:^(BOOL granted) {
                             // The AV callback lands on an arbitrary queue;
                             // bounce to the message thread so callers can
                             // safely touch JUCE / device-manager objects.
                             juce::MessageManager::callAsync([onComplete, granted] {
                               if (onComplete)
                                 onComplete(static_cast<bool>(granted));
                             });
                           }];
}

void openMicSettings() {
  NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference."
                                    @"security?Privacy_Microphone"];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

}  // namespace AudioPermissions

#endif  // JUCE_IOS
