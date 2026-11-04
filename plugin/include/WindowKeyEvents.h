#pragma once
#include <juce_core/juce_core.h>

// Transport-key passthrough to the host DAW (used by plugin/ui/NativeEditor).
// Implemented in WindowKeyEvents.mm (macOS / iOS) and WindowKeyEvents.cpp
// (Windows / Linux).
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

/**
 * Windows: hand any other key the plugin didn't use back to the host, by its
 * JUCE key code, the same way (focus back to the host, the press posted to
 * it), so the host's shortcuts keep working while the plugin holds the
 * keyboard (it takes it on a click: see PluginRoot's FocusPolicy). Returns
 * false where it doesn't apply (no-op): the other platforms give the plugin
 * focus on a click by themselves.
 */
bool forwardKeyCodeToHost(void* nativeHandle, int juceKeyCode);

}  // namespace HostKeys
