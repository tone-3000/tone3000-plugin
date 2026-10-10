#pragma once

#include <juce_core/juce_core.h>

/**
 * iOS audio-route shim (a no-op everywhere else).
 *
 * JUCE's iOS device opens the session as PlayAndRecord with
 * AllowBluetoothHFP (juce_Audio_ios.cpp, setAudioSessionCategory), so a
 * Bluetooth headset with a microphone becomes the whole route and iOS caps
 * the session at 16 or 24 kHz, refusing the requested 48 kHz. We hit exactly
 * that on an iPad with AirPods: `prepareToPlay: sampleRate=24000` and no
 * explanation in the UI.
 *
 * Two answers, both here: tell the user what happened (isBluetoothRoute
 * feeds the settings tip), and stop asking for the HFP route in the first
 * place (configureSession, which also holds Measurement mode). A2DP stays
 * allowed, so Bluetooth output-only listening still works; only the
 * low-rate headset *mic* route goes away.
 *
 * Same platform-shim pattern as Haptics / AudioPermissions: one header, one
 * ObjC++ implementation, a header-only no-op off iOS so desktop links.
 */
namespace IosAudioRoute {

#if JUCE_IOS

/** True when the session's current output route is Bluetooth (HFP, A2DP or
    LE). Cheap enough to call on every state pull. */
bool isBluetoothRoute();

/** True when the current route takes input from the built-in microphone and
    plays out of the built-in speaker: the one iOS setup where monitoring
    squeals. The desktop check matches device names, but JUCE's iOS device is
    always "iOS Audio", so the route's port types are the only signal. */
bool isBuiltInMicToSpeaker();

/** One setCategory:mode:options: call: the category and options JUCE asked
    for minus AllowBluetoothHFP, with Measurement mode (the raw input path, no
    AGC and no voice processing). Mode and options go together because a bare
    setMode: clears category options on iPadOS 26. Measured: from Default
    mode 0x69 became 0x1 (A2DP, AirPlay and DefaultToSpeaker all gone); with
    the mode already Measurement, a repeated setMode: turned 0x69 into 0x61
    (DefaultToSpeaker gone). Playback (no input channels) is left alone:
    nothing to keep raw there. Idempotent, so it can be called after every
    device-manager change: JUCE only sets the category when it opens a device,
    never on the route-change restart path, so re-applying is how the override
    survives a reopen. */
void configureSession();

#else

inline bool isBluetoothRoute() {
  return false;
}
inline bool isBuiltInMicToSpeaker() {
  return false;
}
inline void configureSession() {}

#endif

}  // namespace IosAudioRoute
