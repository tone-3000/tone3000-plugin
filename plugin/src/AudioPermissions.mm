#include "AudioPermissions.h"

#include <juce_events/juce_events.h>  // MessageManager::callAsync

#import <AVFoundation/AVFoundation.h>
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
