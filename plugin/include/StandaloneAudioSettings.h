#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>

class TONE3000Processor;

/**
 * Bespoke replacement for JUCE's AudioDeviceSelectorComponent / standalone
 * audio-settings dialog, exposed to the WebView settings UI as one JSON state
 * snapshot plus imperative setters. It mirrors the selector's semantics
 * exactly (device types, linked ASIO I/O, channel masks, control panel, test
 * tone) and layers the product's auto-setup policy on top:
 *
 * - First contact with a device: request 48 kHz / 128 samples when the device
 *   lists them, and default to a single mono input channel (guitar in input
 *   1). Requests are wishes — the UI always renders the readback values.
 * - Returning device: restore its remembered setup verbatim (rate, buffer,
 *   channel masks are persisted per type+input+output combination in the
 *   standalone holder's settings file). Preferences never override a user's
 *   saved config.
 * - Monitoring: unless the user has toggled Hear Yourself themselves, input
 *   mute follows the feedback-risk heuristic (built-in mic feeding speakers),
 *   so interface users hear themselves immediately and laptop users don't
 *   squeal.
 * - The processor's standalone input mode is kept in sync with the device
 *   channel mask (two active channels = stereo, one = mono fold), replacing
 *   the old processor-side Input 1 / Input 2 / Stereo picker. Legacy saved
 *   modes are migrated into the channel mask once.
 *
 * Everything runs on the message thread (native bridge functions and device
 * manager change callbacks both arrive there); the only audio-thread code is
 * the lock-free input level tap used by the channel picker's meters.
 *
 * In hosted builds (or when the standalone holder doesn't exist) none of this
 * is instantiated — construct only when isAvailable() returns true.
 */
class StandaloneAudioSettings : private juce::ChangeListener {
public:
  /** @param onDeviceStateChanged Fired (message thread) whenever the device
      manager broadcasts a change — the editor forwards it to the WebView as
      an `audioDeviceChanged` event so the UI can re-pull state. */
  StandaloneAudioSettings(TONE3000Processor& processor,
                          std::function<void()> onDeviceStateChanged);
  ~StandaloneAudioSettings() override;

  /** True when running under the standalone plugin holder. */
  static bool isAvailable();

  /** Full settings snapshot for the UI (see buildState for the shape). */
  juce::var getState();

  // Setters. Each returns { ok: bool, error: String } — errors are the
  // human-readable strings setAudioDeviceSetup() reports ("device in use",
  // locked ALSA device…), surfaced inline by the UI instead of a modal.
  juce::var setDeviceType(const juce::String& typeName);
  /** kind: "input" | "output" | "linked" (linked = single-picker ASIO-style
      types where hasSeparateInputsAndOutputs() is false). An empty name means
      "no device" — a legitimate capture-only / playback-only choice. */
  juce::var setDevice(const juce::String& kind, const juce::String& name);
  /** Active input channels by device channel index (1 = mono, 2 = stereo). */
  juce::var setInputChannels(const juce::Array<juce::var>& channelIndices);
  /** Activate output pair N (channels 2N and 2N+1) on multi-out interfaces. */
  juce::var setOutputPair(int pairIndex);
  juce::var setSampleRate(double rate);
  juce::var setBufferSize(int samples);
  /** Output monitoring. Toggling this by hand pins the choice (the automatic
      feedback-risk policy stops overriding it). */
  juce::var setHearYourself(bool hear);
  juce::var playTestTone();
  /** Vendor control panel (ASIO). Reopens the device afterwards since the
      panel can change the setup behind our back — mirrors JUCE's selector. */
  juce::var openControlPanel();
  /** Close + reopen the current device (recovery after errors / panel edits). */
  juce::var restartDevice();
  /** Jump to the OS microphone privacy page (the fix for a denied mic). */
  juce::var openMicSettings();

  // MIDI device layer (mapping itself lives in the processor's MidiMapper —
  // this only decides which hardware feeds it). Enabled inputs are merged
  // into one stream by the standalone holder's player; enablement persists
  // in the holder's audioSetup XML, saved eagerly on every toggle.
  juce::var setMidiInputEnabled(const juce::String& identifier, bool enabled);
  /** OS Bluetooth MIDI pairing dialog (macOS; unavailable elsewhere). */
  juce::var openBluetoothMidiPairing();

  // Input channel meters for the picker. Metering registers a lightweight
  // audio callback with the device manager; enable only while the channel
  // list is on screen.
  void setInputMetering(bool enabled);
  /** Peak level in dB per device input channel index (inactive channels
      report the floor — only active channels produce data). */
  juce::var getInputLevels();

private:
  static constexpr int kMaxMeteredChannels = 64;
  static constexpr float kMeterFloorDb = -120.0f;

  /** Raw per-channel peak tap. Registered alongside (not instead of) the
      standalone holder's own callback, so it sees the real device input even
      while Hear Yourself mutes the plugin's feed. */
  struct InputLevelTap : juce::AudioIODeviceCallback {
    std::array<std::atomic<float>, kMaxMeteredChannels> peaks{};
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels, float* const* outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override { clear(); }
    void audioDeviceStopped() override { clear(); }
    void clear();
  };

  void changeListenerCallback(juce::ChangeBroadcaster*) override;

  // One-time policy pass (waits for the device manager to land a device —
  // startup can be deferred behind the mic-permission prompt): fresh installs
  // get the preferred setup, existing installs keep their saved config.
  void ensureInitialPolicies();

  // First-run only: if the OS mic decision is still pending, ask for it (macOS
  // shows the system prompt). On grant we reopen the device so input goes live
  // without a relaunch; either way the UI re-pulls so the banner updates.
  void ensureMicPermissionRequested();

  /** Clear the holder's "audio open" crash sentinel once a live device proves
      the saved-device open survived (see StandalonePluginHolder). */
  void markAudioInitClean();

  // Auto-setup building blocks (see class comment).
  void applyRememberedOrPreferredSetup();
  void applyPreferredSetup();
  bool applyRememberedSetup(const juce::var& saved);
  void rememberCurrentSetup();

  /** Follow the feedback-risk heuristic unless the user has overridden. */
  void applyMonitoringPolicy();
  bool computeFeedbackRisk() const;

  /** Shared tail of every successful mutation: persist + resync + report. */
  juce::var finishApply(const juce::String& error);

  // Persistence in the standalone holder's settings file.
  juce::PropertySet* props() const;
  juce::String currentSetupKey() const;
  juce::var getRememberedSetups() const;

  bool asioTypeHasDevices();

  TONE3000Processor& processor;
  std::function<void()> onDeviceStateChanged;

  InputLevelTap inputLevelTap;
  bool inputMeteringEnabled = false;
  bool initialPoliciesDone = false;
  bool audioInitMarkedClean = false;
  bool micRequestIssued = false;
  // Device pair the monitoring policy last auto-applied to. Monitoring is
  // re-evaluated only when the input/output pair changes, so a manual Hear
  // Yourself toggle sticks for the current device (see applyMonitoringPolicy).
  juce::String lastMonitoringKey;
  /** Cached once per session — enumerating ASIO drivers hits the registry. */
  std::optional<bool> cachedAsioAvailable;

  // The async mic-permission callback (see ensureMicPermissionRequested) may
  // outlive this object if the window closes while the prompt is up; the weak
  // ref lets it no-op instead of touching freed memory.
  JUCE_DECLARE_WEAK_REFERENCEABLE(StandaloneAudioSettings)
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneAudioSettings)
};
