#pragma once

/**
 * iOS-only: persist the standalone app's plugin state when the app is
 * backgrounded.
 *
 * JUCE's StandalonePluginHolder saves state through savePluginState(), which
 * has exactly two callers: StandaloneFilterWindow::closeButtonPressed() (a
 * desktop window close) and StandaloneFilterApp::systemRequestedQuit(). On iOS
 * neither ever runs - there is no close button, and systemRequestedQuit() is
 * raised only from the macOS and Windows message loops. iOS routes
 * applicationWillTerminate to appWillTerminateByForce(), which tears the app
 * down without saving, and a force quit from the app switcher delivers no
 * termination callback at all.
 *
 * The result is that the standalone app on iOS never writes "filterState":
 * the signal chain lives in RAM only and is gone on the next launch, while
 * user presets (their own files) and the login session (its own
 * preferences file) survive, which is what makes the loss look selective.
 *
 * reloadPluginState() is already called from StandalonePluginHolder::init(),
 * so restoring needs nothing new - only a trigger for the save. Backgrounding
 * is that trigger: iOS always backgrounds an app before terminating it, so
 * saving there covers force quit and an OS eviction alike.
 */
namespace StandaloneStateAutosave {

/** Registers the background-notification observer. Idempotent, and a no-op off
    iOS or outside the standalone app. */
void install();

}  // namespace StandaloneStateAutosave
