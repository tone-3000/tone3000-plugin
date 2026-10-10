#include "IosAudioRoute.h"

#if JUCE_IOS

#import <AVFoundation/AVFoundation.h>

namespace IosAudioRoute {

bool isBluetoothRoute() {
  AVAudioSession* session = [AVAudioSession sharedInstance];
  for (AVAudioSessionPortDescription* output in session.currentRoute.outputs) {
    NSString* type = output.portType;
    if ([type isEqualToString:AVAudioSessionPortBluetoothHFP] ||
        [type isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
        [type isEqualToString:AVAudioSessionPortBluetoothLE])
      return true;
  }
  // A headset mic can be the input while the output is still the speaker.
  for (AVAudioSessionPortDescription* input in session.currentRoute.inputs)
    if ([input.portType isEqualToString:AVAudioSessionPortBluetoothHFP])
      return true;
  return false;
}

bool isBuiltInMicToSpeaker() {
  AVAudioSessionRouteDescription* route = [AVAudioSession sharedInstance].currentRoute;
  bool mic = false;
  for (AVAudioSessionPortDescription* input in route.inputs)
    if ([input.portType isEqualToString:AVAudioSessionPortBuiltInMic])
      mic = true;
  if (!mic)
    return false;
  for (AVAudioSessionPortDescription* output in route.outputs)
    if ([output.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker])
      return true;
  return false;
}

void configureSession() {
  AVAudioSession* session = [AVAudioSession sharedInstance];

  // Only PlayAndRecord carries the option; leave Playback alone.
  if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord])
    return;

  // Same SDK gate JUCE uses for the same constant (it was renamed in the
  // iOS 26 SDK; the value is unchanged).
 #if JUCE_IOS_API_VERSION_CAN_BE_BUILT (26, 0)
  constexpr auto hfp = AVAudioSessionCategoryOptionAllowBluetoothHFP;
 #else
  constexpr auto hfp = AVAudioSessionCategoryOptionAllowBluetooth;
 #endif

  // Take JUCE's own options and clear one bit, rather than rebuilding the
  // set: MixWithOthers, DefaultToSpeaker, AllowAirPlay and A2DP stay exactly
  // as JUCE asked for them, whatever the JUCE version decided. They are read
  // from the live session, so this never adds an option back; JUCE sets the
  // full set again on the next device open.
  const AVAudioSessionCategoryOptions options = session.categoryOptions & ~hfp;

  // Also what ends the loop: our own setCategory raises a CategoryChange
  // route notification, the device type forwards every reason to the
  // device-manager broadcast, and that lands here again.
  if (options == session.categoryOptions
      && [session.mode isEqualToString:AVAudioSessionModeMeasurement])
    return;

  // Two attempts, back to back with no delay, so the second one only covers
  // a refusal the OS clears immediately (a route still settling as the call
  // lands). Anything slower than that is not retried here: the next route
  // change re-applies this anyway. Each attempt starts from a fresh error so
  // the log below describes the attempt that actually failed last.
  NSError* error = nil;
  for (int attempt = 0; attempt < 2; ++attempt) {
    error = nil;
    if ([session setCategory:session.category
                        mode:AVAudioSessionModeMeasurement
                     options:options
                       error:&error])
      return;
  }

  DBG ("IosAudioRoute: could not set Measurement mode without HFP: "
       << (error != nil ? juce::String::fromUTF8 ([[error localizedDescription] UTF8String])
                        : juce::String ("no error object")));
}

}  // namespace IosAudioRoute

#endif
