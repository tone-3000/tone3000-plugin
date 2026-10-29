#pragma once
#include <juce_core/juce_core.h>

// Transport-key passthrough to the host DAW (used by plugin/ui/NativeEditor).
// Implemented in WindowKeyEvents.mm (macOS / iOS) and WindowKeyEvents.cpp
// (Windows / Linux / Android, where it is a no-op).
namespace HostKeys {

enum class HostKey { space, enter };

/**
 * Re-dispatch a transport keypress to the host DAW.
 *
 * Once the user has clicked the plugin, its view owns keyboard focus, so
 * Space (play/stop) and Enter (return to start) would stop at us. A press
 * no control consumed is handed here: keyboard focus goes back to the host,
 * then a synthesized key event is delivered to it, so follow-up presses and
 * repeats reach the host directly. Takes the editor's peer native handle.
 * Best effort per host.
 */
void forwardKeyToHost(void* nativeHandle, HostKey key);

}  // namespace HostKeys
