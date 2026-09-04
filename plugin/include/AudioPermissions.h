#pragma once

#include <juce_core/juce_core.h>

#include <functional>

/**
 * Thin OS microphone-permission shim.
 *
 * JUCE's RuntimePermissions is a no-op on desktop, but macOS gates ALL audio
 * input (built-in mic and audio interfaces alike) behind a per-app privacy
 * switch: when it's off, opening an input device still "succeeds" but the
 * stream is silent, so the plugin can't hear the instrument and nothing in the
 * audio API explains why. This surfaces that state to the settings UI (banner
 * + inline alert) and offers a one-click jump to the OS privacy page.
 *
 * Platform-specific implementations: AudioPermissions.mm (macOS, AVFoundation;
 * iOS, AVAudioSession) and AudioPermissions.cpp (Windows/Linux fallback).
 */
namespace AudioPermissions {

/** Microphone (audio-input) authorization as reported by the OS.
    - granted: the app may capture input.
    - denied:  the user or a policy blocked it; input is silent until they
               re-enable it in privacy settings and relaunch.
    - unknown: not yet decided (macOS "not determined"), or a platform with no
               queryable per-app mic gate (treated as usable, never nagged). */
enum class MicStatus { granted, denied, unknown };

/** Current mic authorization. Never prompts; cheap enough to poll. */
MicStatus getMicStatus();

/** Show the OS mic-permission prompt if the decision is still pending; a no-op
    once granted/denied. `onComplete` fires on the message thread with the
    resulting granted flag. */
void requestMicAccess(std::function<void(bool)> onComplete);

/** Open the OS page where the user grants mic access to this app (macOS:
    Privacy & Security > Microphone; iOS: the app's own page in Settings).
    No-op where unsupported. */
void openMicSettings();

}  // namespace AudioPermissions
