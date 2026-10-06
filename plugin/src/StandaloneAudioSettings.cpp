#include "StandaloneAudioSettings.h"

#include "AudioPermissions.h"
#include "IosAudioRoute.h"
#include "Processor.h"

#include <cmath>

// The standalone filter window header expects the full GUI/audio module set
// to be visible first.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace {

// The product's preferred first-contact setup: 48 kHz (the models' native
// rate) and 128 samples (~2.7 ms, low latency that still runs everywhere).
// Only ever *requested* when the device's own lists contain them.
constexpr double kPreferredSampleRate = 48000.0;
constexpr int kPreferredBufferSize = 128;

// The plugin consumes at most a stereo pair, so channel selection is a
// min-1 / max-2 window over the device's inputs (JUCE selector semantics)
// and outputs collapse to stereo pairs.
constexpr int kMaxActiveInputChannels = 2;

// Settings-file keys (stored in the standalone holder's PropertiesFile, next
// to JUCE's own "audioSetup" / "shouldMuteInput" values).
constexpr auto kRememberedSetupsKey = "t3kDeviceSetups";
constexpr auto kPoliciesAppliedKey = "t3kAudioPoliciesApplied";

juce::StandalonePluginHolder* holder() {
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
  return juce::StandalonePluginHolder::getInstance();
#else
  return nullptr;
#endif
}

juce::AudioDeviceManager* deviceManager() {
  auto* h = holder();
  return h != nullptr ? &h->deviceManager : nullptr;
}

juce::var makeResult(const juce::String& error) {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("ok", error.isEmpty());
  obj->setProperty("error", error);
  return juce::var(obj);
}

juce::var toVar(const juce::StringArray& strings) {
  juce::Array<juce::var> arr;
  for (const auto& s : strings)
    arr.add(s);
  return arr;
}

#if !JUCE_IOS
// Feedback-risk heuristic: a built-in microphone feeding speakers is the one
// setup where unmuted monitoring squeals. Device names are the only signal
// the OS gives us; interfaces and headphones never match both sides. (iOS
// asks the audio route instead: see IosAudioRoute::isBuiltInMicToSpeaker.)
bool looksLikeMicrophone(const juce::String& name) {
  const auto n = name.toLowerCase();
  return n.contains("microphone") || n.contains("mic array") || n.containsWholeWord("mic");
}

bool looksLikeSpeakers(const juce::String& name) {
  const auto n = name.toLowerCase();
  return n.contains("speaker") || n.contains("built-in output");
}
#endif

// Stereo-pair label, e.g. "Output 1 + 2". Same common-prefix trimming as
// JUCE's AudioDeviceSelectorComponent so labels match what users have seen.
juce::String nameForChannelPair(const juce::String& name1, const juce::String& name2) {
  if (name2.isEmpty())
    return name1.trim();

  juce::String commonBit;
  for (int i = 0; i < name1.length(); ++i)
    if (name1.substring(0, i).equalsIgnoreCase(name2.substring(0, i)))
      commonBit = name1.substring(0, i);

  // Only split at whitespace so "input 11" + "input 12" doesn't become
  // "input 11 + 2".
  while (commonBit.isNotEmpty() &&
         !juce::CharacterFunctions::isWhitespace(commonBit.getLastCharacter()))
    commonBit = commonBit.dropLastCharacters(1);

  return name1.trim() + " + " + name2.substring(commonBit.length()).trim();
}

const char* micStatusString(AudioPermissions::MicStatus status) {
  switch (status) {
    case AudioPermissions::MicStatus::granted:
      return "granted";
    case AudioPermissions::MicStatus::denied:
      return "denied";
    case AudioPermissions::MicStatus::unknown:
      break;
  }
  return "unknown";
}

}  // namespace

//==============================================================================
// Lifetime

StandaloneAudioSettings::StandaloneAudioSettings(TONE3000Processor& p,
                                                 std::function<void()> onChange)
    : processor(p), onDeviceStateChanged(std::move(onChange)) {
  jassert(isAvailable());
  if (auto* dm = deviceManager())
    dm->addChangeListener(this);
  // iOS: Measurement mode, and no Bluetooth headset mic route (see
  // IosAudioRoute.h). No-op off iOS.
  applyRawInputMode();
  ensureInitialPolicies();
}

StandaloneAudioSettings::~StandaloneAudioSettings() {
  setInputMetering(false);
  if (auto* dm = deviceManager())
    dm->removeChangeListener(this);
}

bool StandaloneAudioSettings::isAvailable() {
  return holder() != nullptr;
}

void StandaloneAudioSettings::changeListenerCallback(juce::ChangeBroadcaster*) {
  // Fires for every device-manager change: our own setters, hot-plugs,
  // devices vanishing mid-session, vendor control panel edits. Re-run the
  // sync policies, then push the UI to re-pull state.
  applyRawInputMode();
  ensureInitialPolicies();
  applyMonitoringPolicy();
  if (onDeviceStateChanged)
    onDeviceStateChanged();
}

//==============================================================================
// State snapshot

juce::var StandaloneAudioSettings::getState() {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return {};

  auto* obj = new juce::DynamicObject();
  juce::var state(obj);

  juce::Array<juce::var> types;
  for (auto* type : dm->getAvailableDeviceTypes())
    types.add(type->getTypeName());
  obj->setProperty("deviceTypes", types);
  obj->setProperty("currentType", dm->getCurrentAudioDeviceType());

  auto* type = dm->getCurrentDeviceTypeObject();
  const bool separateIO = type == nullptr || type->hasSeparateInputsAndOutputs();
  obj->setProperty("separateIO", separateIO);

  if (type != nullptr) {
    type->scanForDevices();  // pick up hot-plugged hardware on every pull
    obj->setProperty("inputDevices", toVar(type->getDeviceNames(true)));
    obj->setProperty("outputDevices", toVar(type->getDeviceNames(false)));
  } else {
    obj->setProperty("inputDevices", juce::Array<juce::var>{});
    obj->setProperty("outputDevices", juce::Array<juce::var>{});
  }

  const auto setup = dm->getAudioDeviceSetup();
  obj->setProperty("inputDevice", setup.inputDeviceName);
  obj->setProperty("outputDevice", setup.outputDeviceName);

  auto* device = dm->getCurrentAudioDevice();
  obj->setProperty("deviceOpen", device != nullptr && device->isOpen());

  // Input channels with their active flags; the picker renders these rows.
  // Use our own tracked request, not dm->getAudioDeviceSetup() or the
  // device's active-channel readback (see explicitInputChannels): on
  // hardware whose channel count floor is above what we asked for, both of
  // those always report every hardware channel active - the picker would
  // show both channels selected right after picking just one.
  juce::Array<juce::var> inputChannels;
  if (device != nullptr) {
    const auto names = device->getInputChannelNames();
    const auto active =
        !explicitInputChannels.isZero() ? explicitInputChannels : device->getActiveInputChannels();
    for (int i = 0; i < names.size(); ++i) {
      auto* ch = new juce::DynamicObject();
      ch->setProperty("index", i);
      ch->setProperty("name", names[i]);
      ch->setProperty("active", active[i]);
      inputChannels.add(juce::var(ch));
    }
  }
  obj->setProperty("inputChannels", inputChannels);

  // Multi-out interfaces get a stereo-pair picker; 2-out devices don't need
  // one (both channels are simply active).
  juce::Array<juce::var> outputPairs;
  int activeOutputPair = -1;
  if (device != nullptr) {
    const auto outNames = device->getOutputChannelNames();
    if (outNames.size() > 2) {
      for (int i = 0; i < outNames.size(); i += 2)
        outputPairs.add(
            nameForChannelPair(outNames[i], i + 1 < outNames.size() ? outNames[i + 1] : ""));
      const int firstActive = device->getActiveOutputChannels().findNextSetBit(0);
      activeOutputPair = firstActive >= 0 ? firstActive / 2 : -1;
    }
  }
  obj->setProperty("outputPairs", outputPairs);
  obj->setProperty("activeOutputPair", activeOutputPair);

  // Rates and buffers come from the device (never hardcoded), and the
  // current values are the post-open readback (the truth, not the request).
  juce::Array<juce::var> sampleRates;
  juce::Array<juce::var> bufferSizes;
  if (device != nullptr) {
    for (const auto rate : device->getAvailableSampleRates())
      sampleRates.add(rate);
#if JUCE_IOS
    // The explicit choices cannot predict every route (e.g. Bluetooth at
    // 24 kHz). Include the actual readback so the selector can show it on
    // first launch, saved-setup restoration and route changes as well.
    if (const auto actualRate = device->getCurrentSampleRate(); actualRate > 0.0)
      sampleRates.addIfNotAlreadyThere(juce::var(actualRate));
#endif
    for (const auto size : device->getAvailableBufferSizes())
      bufferSizes.add(size);
  }
  obj->setProperty("sampleRates", sampleRates);
  obj->setProperty("bufferSizes", bufferSizes);
  obj->setProperty("sampleRate", device != nullptr ? device->getCurrentSampleRate() : 0.0);
  obj->setProperty("bufferSize", device != nullptr ? device->getCurrentBufferSizeSamples() : 0);

  obj->setProperty("hasControlPanel", device != nullptr && device->hasControlPanel());
  obj->setProperty("hearYourself",
                   !static_cast<bool>(holder()->getMuteInputValue().getValue()));
  obj->setProperty("feedbackRisk", computeFeedbackRisk());
  obj->setProperty("asioAvailable", asioTypeHasDevices());
  // OS mic gate: on macOS a "denied" state silently kills all audio input,
  // so the UI surfaces it (banner + inline alert) with a jump to the fix.
  obj->setProperty("micPermission", micStatusString(AudioPermissions::getMicStatus()));

  // iOS: a Bluetooth route caps the session at 16 or 24 kHz and adds
  // latency; the UI turns this into one plain-language tip. Always false on
  // desktop.
  obj->setProperty("bluetoothRoute", IosAudioRoute::isBluetoothRoute());

  // MIDI inputs, re-enumerated per pull like the audio devices above (the UI
  // polls while the tab is open, which is also our hot-plug detection; the
  // OS doesn't broadcast MIDI device arrivals to the device manager).
  juce::Array<juce::var> midiInputs;
  for (const auto& input : juce::MidiInput::getAvailableDevices()) {
    auto* midiIn = new juce::DynamicObject();
    midiIn->setProperty("id", input.identifier);
    midiIn->setProperty("name", input.name);
    midiIn->setProperty("enabled", dm->isMidiInputDeviceEnabled(input.identifier));
    midiInputs.add(juce::var(midiIn));
  }
  obj->setProperty("midiInputs", midiInputs);
  obj->setProperty("btMidiAvailable", juce::BluetoothMidiDevicePairingDialogue::isAvailable());

  return state;
}

//==============================================================================
// Setters

juce::var StandaloneAudioSettings::setDeviceType(const juce::String& typeName) {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");

  // Entirely new device context; any tracked request belongs to the old type.
  explicitInputChannels.clear();
  explicitOutputChannels.clear();
  dm->setCurrentAudioDeviceType(typeName, true);
  if (dm->getCurrentAudioDeviceType() != typeName)
    return makeResult("Couldn't switch to " + typeName + ".");

  // The type switch opened its default devices; treat that like a device
  // selection so remembered/preferred setup logic runs.
  applyRememberedOrPreferredSetup();
  return finishApply({});
}

juce::var StandaloneAudioSettings::setDevice(const juce::String& kind,
                                             const juce::String& name) {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");

  auto setup = dm->getAudioDeviceSetup();

  // The side(s) whose device is changing get a fresh device context; a mask
  // tracked for the old device on that side no longer means anything (and
  // may not even be in range for the new one).
  if (kind == "linked") {
    setup.inputDeviceName = name;
    setup.outputDeviceName = name;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    explicitInputChannels.clear();
    explicitOutputChannels.clear();
  } else if (kind == "input") {
    setup.inputDeviceName = name;
    setup.useDefaultInputChannels = true;
    explicitInputChannels.clear();
  } else if (kind == "output") {
    setup.outputDeviceName = name;
    setup.useDefaultOutputChannels = true;
    explicitOutputChannels.clear();
  } else {
    return makeResult("Unknown device kind.");
  }

  // Ask for device defaults on rate/buffer; the remembered/preferred pass
  // below immediately re-applies the right values for this combination. This
  // avoids "unsupported sample rate" failures when hopping between devices.
  setup.sampleRate = 0;
  setup.bufferSize = 0;

  const auto error = dm->setAudioDeviceSetup(setup, true);
  if (error.isEmpty())
    applyRememberedOrPreferredSetup();
  return finishApply(error);
}

// Pin the tracked request into the setup as an explicit request, so a rate-
// or buffer-only change can't let JUCE's setAudioDeviceSetup re-derive the
// mask from device defaults (updateSetupChannels: clear the mask, enable the
// first two channels) whenever the matching useDefault*Channels flag is up.
//
// Deliberately does NOT read dm->getAudioDeviceSetup() or the device's active
// channel readback as the source of truth: AudioDeviceManager's own
// updateCurrentSetup() unconditionally overwrites currentSetup.inputChannels/
// outputChannels with the device's *active* (post-open) readback after every
// successful open, and ALSA rounds a request below the hardware's
// channel-count floor up to that floor (ensureMinimumNumBitsSet) - so on a
// device fixed at 2 channels, both of those always say "2 active" even when
// we only ever asked for channel 0, from the moment the previous open
// returned. explicitInputChannels/explicitOutputChannels are the only
// members that still hold the real request; fall back to the device's
// readback only when neither has ever been recorded for this device.
static void pinActiveChannels(juce::AudioDeviceManager::AudioDeviceSetup& setup,
                              const juce::AudioIODevice* device,
                              const juce::BigInteger& explicitInputChannels,
                              const juce::BigInteger& explicitOutputChannels,
                              bool pinInputs,
                              bool pinOutputs) {
  if (device == nullptr)
    return;
  if (pinInputs) {
    setup.inputChannels = !explicitInputChannels.isZero() ? explicitInputChannels
                                                          : device->getActiveInputChannels();
    setup.useDefaultInputChannels = false;
  }
  if (pinOutputs) {
    setup.outputChannels = !explicitOutputChannels.isZero() ? explicitOutputChannels
                                                            : device->getActiveOutputChannels();
    setup.useDefaultOutputChannels = false;
  }
}

juce::var StandaloneAudioSettings::setInputChannels(
    const juce::Array<juce::var>& channelIndices) {
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  if (device == nullptr)
    return makeResult("No audio device is open.");

  const int numChannels = device->getInputChannelNames().size();
  juce::BigInteger mask;
  for (const auto& index : channelIndices) {
    const int i = static_cast<int>(index);
    if (juce::isPositiveAndBelow(i, numChannels))
      mask.setBit(i);
  }

  const int count = mask.countNumberOfSetBits();
  if (count < 1 || count > kMaxActiveInputChannels)
    return makeResult("Select one (mono) or two (stereo) input channels.");

  explicitInputChannels = mask;
  auto setup = dm->getAudioDeviceSetup();
  setup.useDefaultInputChannels = false;
  setup.inputChannels = mask;
  pinActiveChannels(setup, device, explicitInputChannels, explicitOutputChannels,
                    /*pinInputs=*/false, /*pinOutputs=*/true);
  return finishApply(dm->setAudioDeviceSetup(setup, true));
}

juce::var StandaloneAudioSettings::setOutputPair(int pairIndex) {
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  if (device == nullptr)
    return makeResult("No audio device is open.");

  const int numChannels = device->getOutputChannelNames().size();
  const int firstBit = pairIndex * 2;
  if (!juce::isPositiveAndBelow(firstBit, numChannels))
    return makeResult("That output pair no longer exists.");

  juce::BigInteger mask;
  mask.setBit(firstBit);
  if (firstBit + 1 < numChannels)
    mask.setBit(firstBit + 1);

  explicitOutputChannels = mask;
  auto setup = dm->getAudioDeviceSetup();
  setup.useDefaultOutputChannels = false;
  setup.outputChannels = mask;
  pinActiveChannels(setup, device, explicitInputChannels, explicitOutputChannels,
                    /*pinInputs=*/true, /*pinOutputs=*/false);
  return finishApply(dm->setAudioDeviceSetup(setup, true));
}

juce::var StandaloneAudioSettings::setSampleRate(double rate) {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");
  auto setup = dm->getAudioDeviceSetup();
#if !JUCE_IOS
  setup.sampleRate = rate;
#endif
  pinActiveChannels(setup, dm->getCurrentAudioDevice(), explicitInputChannels, explicitOutputChannels,
                    /*pinInputs=*/true, /*pinOutputs=*/true);
#if JUCE_IOS
  const auto previousSetup = setup;
  setup.sampleRate = rate;
  // Explicit rates are requests, not discovered capabilities. Negotiate only
  // the selected rate; never probe the other choices here or during launch.
  auto error = dm->setAudioDeviceSetup(setup, true);
  auto* device = dm->getCurrentAudioDevice();
  if (error.isNotEmpty() || device == nullptr || !device->isOpen()) {
    if (error.isEmpty())
      error = "The audio device couldn't be opened.";
    const auto restoreError = dm->setAudioDeviceSetup(previousSetup, true);
    device = dm->getCurrentAudioDevice();
    if (restoreError.isEmpty() && device != nullptr && device->isOpen()) {
      finishApply({});  // Remember the restored readback, not the failed request.
      error += " Previous audio settings restored.";
    } else {
      error += " Couldn't restore the previous audio settings.";
      if (restoreError.isNotEmpty())
        error += " " + restoreError;
    }
    return finishApply(error);
  }

  const auto actualRate = device->getCurrentSampleRate();
  auto result = finishApply({});  // Persist the actual, negotiated setup.
  if (std::abs(actualRate - rate) > 1.0) {
    result.getDynamicObject()->setProperty(
        "warning", juce::String(rate / 1000.0, 1) + " kHz unavailable on this audio route; using " +
                       juce::String(actualRate / 1000.0, 1) + " kHz.");
  }
  return result;
#else
  return finishApply(dm->setAudioDeviceSetup(setup, true));
#endif
}

juce::var StandaloneAudioSettings::setBufferSize(int samples) {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");
  auto setup = dm->getAudioDeviceSetup();
  setup.bufferSize = samples;
  pinActiveChannels(setup, dm->getCurrentAudioDevice(), explicitInputChannels, explicitOutputChannels,
                    /*pinInputs=*/true, /*pinOutputs=*/true);
  return finishApply(dm->setAudioDeviceSetup(setup, true));
}

juce::var StandaloneAudioSettings::setHearYourself(bool hear) {
  auto* h = holder();
  if (h == nullptr)
    return makeResult("Audio settings are unavailable.");
  h->getMuteInputValue().setValue(!hear);
  // A manual toggle pins the choice for the current device: mark this pair as
  // already handled so the auto policy won't fight it. Switching to a
  // different input/output pair re-evaluates from feedback risk (on iOS, so
  // does a route change that flips the risk; see monitoringKey).
  lastMonitoringKey = monitoringKey();
  return makeResult({});
}

juce::var StandaloneAudioSettings::playTestTone() {
  auto* dm = deviceManager();
  if (dm == nullptr || dm->getCurrentAudioDevice() == nullptr)
    return makeResult("No output device is open.");
  dm->playTestSound();
  return makeResult({});
}

juce::var StandaloneAudioSettings::openControlPanel() {
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  if (device == nullptr || !device->hasControlPanel())
    return makeResult("This driver has no control panel.");

  bool shown = false;
  {
    // Same trick as JUCE's selector: a desktop modal component keeps our
    // message loop sane while the vendor panel runs.
    juce::Component modalWindow;
    modalWindow.setOpaque(true);
    modalWindow.addToDesktop(0);
    modalWindow.enterModalState();
    shown = device->showControlPanel();
  }

  if (shown) {
    // The panel may have changed the setup behind our back; reopen so the
    // readback values (and the plugin) reflect reality.
    dm->closeAudioDevice();
    dm->restartLastAudioDevice();
  }
  return finishApply({});
}

juce::var StandaloneAudioSettings::restartDevice() {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");
  dm->closeAudioDevice();
  dm->restartLastAudioDevice();
  const bool reopened = dm->getCurrentAudioDevice() != nullptr;
  return makeResult(reopened ? juce::String()
                             : juce::String("Couldn't reopen the audio device."));
}

juce::var StandaloneAudioSettings::openMicSettings() {
  AudioPermissions::openMicSettings();
  return makeResult({});
}

juce::var StandaloneAudioSettings::setMidiInputEnabled(const juce::String& identifier,
                                                       bool enabled) {
  auto* dm = deviceManager();
  if (dm == nullptr)
    return makeResult("Audio settings are unavailable.");
  dm->setMidiInputDeviceEnabled(identifier, enabled);  // broadcasts → UI re-pulls
  // Enablement rides the holder's audioSetup XML, which JUCE otherwise only
  // writes on clean shutdown; save eagerly so a crash can't lose the toggle.
  if (auto* h = holder())
    h->saveAudioDeviceState();
  return makeResult({});
}

juce::var StandaloneAudioSettings::openBluetoothMidiPairing() {
  if (!juce::BluetoothMidiDevicePairingDialogue::isAvailable())
    return makeResult("Bluetooth MIDI isn't available on this system.");
  juce::BluetoothMidiDevicePairingDialogue::open();
  return makeResult({});
}

//==============================================================================
// Input metering

void StandaloneAudioSettings::InputLevelTap::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&) {
  // CRITICAL: as a secondary device callback, our output buffer is a reused
  // scratch buffer that AudioDeviceManager *sums into the real output*. We
  // produce no audio, so we must clear it; leaving it untouched would mix
  // stale/garbage samples into the output and blast the user's ears.
  for (int ch = 0; ch < numOutputChannels; ++ch)
    if (auto* out = outputChannelData[ch])
      juce::FloatVectorOperations::clear(out, numSamples);

  const int count = juce::jmin(numInputChannels, kMaxMeteredChannels);
  for (int ch = 0; ch < count; ++ch) {
    const auto* data = inputChannelData[ch];
    if (data == nullptr)
      continue;
    const auto range = juce::FloatVectorOperations::findMinAndMax(data, numSamples);
    const float peak = juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd()));
    peaks[static_cast<size_t>(ch)].store(peak, std::memory_order_relaxed);
  }
}

void StandaloneAudioSettings::InputLevelTap::clear() {
  for (auto& peak : peaks)
    peak.store(0.0f, std::memory_order_relaxed);
}

void StandaloneAudioSettings::setInputMetering(bool enabled) {
  if (enabled == inputMeteringEnabled)
    return;
  inputMeteringEnabled = enabled;
  if (auto* dm = deviceManager()) {
    if (enabled) {
      inputLevelTap.clear();
      dm->addAudioCallback(&inputLevelTap);
    } else {
      dm->removeAudioCallback(&inputLevelTap);
    }
  }
}

juce::var StandaloneAudioSettings::getInputLevels() {
  juce::Array<juce::var> levels;
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  if (!inputMeteringEnabled || device == nullptr)
    return levels;

  // The callback sees our *requested* channels packed in order (ALSAThread
  // builds its callback array from the request, before padding it up to the
  // hardware's channel-count floor for the actual open - see pinActiveChannels),
  // so fan out against that request, not device->getActiveInputChannels():
  // on hardware whose floor is above what we asked for, the latter always
  // reports every hardware channel active, and a channel we never requested
  // (so the tap never wrote a peak for it) would show whatever stale/zero
  // value happened to be sitting in that peaks slot instead of the floor.
  const int numChannels = device->getInputChannelNames().size();
  for (int i = 0; i < numChannels; ++i)
    levels.add(kMeterFloorDb);

  const auto active =
      !explicitInputChannels.isZero() ? explicitInputChannels : device->getActiveInputChannels();
  int callbackChannel = 0;
  for (int i = active.findNextSetBit(0); i >= 0 && callbackChannel < kMaxMeteredChannels;
       i = active.findNextSetBit(i + 1)) {
    const float peak =
        inputLevelTap.peaks[static_cast<size_t>(callbackChannel++)].load(std::memory_order_relaxed);
    if (i < numChannels)
      levels.set(i, juce::Decibels::gainToDecibels(peak, kMeterFloorDb));
  }
  return levels;
}

//==============================================================================
// Auto-setup policy

void StandaloneAudioSettings::ensureInitialPolicies() {
  if (initialPoliciesDone)
    return;
  auto* dm = deviceManager();
  if (dm == nullptr || dm->getCurrentAudioDevice() == nullptr)
    return;  // startup may still be waiting on the mic-permission prompt

  // A device is live, so the saved-device open survived; retire the crash
  // sentinel before doing anything that might itself fault.
  markAudioInitClean();

  // Nudge the OS mic prompt on a fresh decision (macOS). Harmless if input
  // already triggered it; the same TCC gate is shared.
  ensureMicPermissionRequested();

  initialPoliciesDone = true;

  auto* p = props();
  if (p == nullptr || !p->getBoolValue(kPoliciesAppliedKey, false)) {
    // One machine-lifetime pass. An existing install (it has a saved
    // "audioSetup" from JUCE's holder) keeps its config; a fresh install
    // gets the preferred first-contact setup so the first open lands on
    // 48 kHz / 128 / mono input 1 with zero user action.
    if (p == nullptr || !p->containsKey("audioSetup"))
      applyPreferredSetup();
    if (p != nullptr)
      p->setValue(kPoliciesAppliedKey, true);
  }

  // Boot reconciliation. JUCE's own XML restore can come up with the wrong
  // channels (updateXml skips the channel masks whenever the useDefault*
  // flags were up when it last ran, and a restore from such an XML defaults
  // to stereo), so a remembered setup for this device pair is re-asserted
  // rather than clobbered with whatever booted. setAudioDeviceSetup is a
  // no-op when nothing differs, so the common path costs nothing. Only when
  // this device pair has never been seen do we record the boot state as its
  // baseline.
  const auto saved = getRememberedSetups().getProperty(currentSetupKey(), {});
  if (saved.isObject())
    applyRememberedSetup(saved);
  else
    rememberCurrentSetup();
  applyMonitoringPolicy();
}

void StandaloneAudioSettings::ensureMicPermissionRequested() {
  if (micRequestIssued)
    return;
  micRequestIssued = true;

  // Only worth prompting when the decision is still open; granted/denied are
  // terminal (a denied user re-grants in System Settings, not via a prompt).
  if (AudioPermissions::getMicStatus() != AudioPermissions::MicStatus::unknown)
    return;

  juce::WeakReference<StandaloneAudioSettings> weak(this);
  AudioPermissions::requestMicAccess([weak](bool granted) {
    auto* self = weak.get();
    if (self == nullptr)
      return;  // window closed while the prompt was up
    // On grant, the input stream opened before permission is still silent;
    // reopen so it goes live without a relaunch. Then refresh either way so
    // the mic-denied banner appears/clears.
    if (granted)
      self->restartDevice();
    if (self->onDeviceStateChanged)
      self->onDeviceStateChanged();
  });
}

void StandaloneAudioSettings::markAudioInitClean() {
  if (audioInitMarkedClean)
    return;
  audioInitMarkedClean = true;
  if (auto* h = holder()) {
    const auto sentinel = h->getAudioOpenSentinelFile();
    if (sentinel != juce::File())
      sentinel.deleteFile();
  }
}

void StandaloneAudioSettings::applyRememberedOrPreferredSetup() {
  auto* dm = deviceManager();
  if (dm == nullptr || dm->getCurrentAudioDevice() == nullptr)
    return;

  const auto saved = getRememberedSetups().getProperty(currentSetupKey(), {});
  if (!saved.isObject() || !applyRememberedSetup(saved))
    applyPreferredSetup();
}

void StandaloneAudioSettings::applyPreferredSetup() {
  auto* dm = deviceManager();
  auto* device = dm->getCurrentAudioDevice();
  auto setup = dm->getAudioDeviceSetup();
  bool changed = false;

  // Requests are conditional on the device's own lists: never ask JACK for
  // a rate or a coarse-period WASAPI device for 128 samples it can't do.
  if (device->getAvailableSampleRates().contains(kPreferredSampleRate) &&
      !juce::exactlyEqual(device->getCurrentSampleRate(), kPreferredSampleRate)) {
    setup.sampleRate = kPreferredSampleRate;
    changed = true;
  }
  if (device->getAvailableBufferSizes().contains(kPreferredBufferSize) &&
      device->getCurrentBufferSizeSamples() != kPreferredBufferSize) {
    setup.bufferSize = kPreferredBufferSize;
    changed = true;
  }

  // Guitar-first default: one mono channel (the device's first input). Check
  // our own tracked request, not dm->getAudioDeviceSetup() or the device's
  // active-channel readback: on hardware whose channel-count floor is above
  // 1, both always report 2+ active inputs (see pinActiveChannels), so
  // checking either here would never see "already single-channel" and would
  // keep forcing channel 0 back on every call, even after the user
  // deliberately picked a different channel.
  const bool alreadySingleChannel = explicitInputChannels.countNumberOfSetBits() == 1;
  if (device->getInputChannelNames().size() > 0 && !alreadySingleChannel) {
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setBit(0);
    explicitInputChannels = setup.inputChannels;
    changed = true;
  }

  if (changed)
    dm->setAudioDeviceSetup(setup, true);
}

bool StandaloneAudioSettings::applyRememberedSetup(const juce::var& saved) {
  auto* dm = deviceManager();
  auto* device = dm->getCurrentAudioDevice();
  auto setup = dm->getAudioDeviceSetup();
#if JUCE_IOS
  const auto previousSetup = setup;
  const auto previousInputs = explicitInputChannels;
  const auto previousOutputs = explicitOutputChannels;
#endif

  const double rate = saved.getProperty("rate", 0.0);
  const int buffer = saved.getProperty("buffer", 0);
  if (device->getAvailableSampleRates().contains(rate))
    setup.sampleRate = rate;
  if (device->getAvailableBufferSizes().contains(buffer))
    setup.bufferSize = buffer;

  // Drop remembered bits for channels the device no longer reports.
  const auto parseMask = [](const juce::var& hex, int numChannels) {
    juce::BigInteger mask;
    mask.parseString(hex.toString(), 16);
    const int excess = mask.getHighestBit() + 1 - numChannels;
    if (excess > 0)
      mask.setRange(numChannels, excess, false);
    return mask;
  };
  const auto inMask =
      parseMask(saved.getProperty("in", "0"), device->getInputChannelNames().size());
  const auto outMask =
      parseMask(saved.getProperty("out", "0"), device->getOutputChannelNames().size());

  if (!inMask.isZero()) {
    setup.useDefaultInputChannels = false;
    setup.inputChannels = inMask;
    explicitInputChannels = inMask;
  }
  if (!outMask.isZero()) {
    setup.useDefaultOutputChannels = false;
    setup.outputChannels = outMask;
    explicitOutputChannels = outMask;
  }

#if JUCE_IOS
  const auto error = dm->setAudioDeviceSetup(setup, true);
  device = dm->getCurrentAudioDevice();
  if (error.isEmpty() && device != nullptr && device->isOpen()) {
    // A saved request may belong to a different USB route. Replace it with
    // the actual startup readback so we don't retry an unavailable rate.
    rememberCurrentSetup();
    return true;
  }
  explicitInputChannels = previousInputs;
  explicitOutputChannels = previousOutputs;
  const auto restoreError = dm->setAudioDeviceSetup(previousSetup, true);
  device = dm->getCurrentAudioDevice();
  if (restoreError.isEmpty() && device != nullptr && device->isOpen())
    rememberCurrentSetup();
  juce::Logger::writeToLog("[AudioSettings] saved iOS setup failed: " + error +
                           "; restore: " + restoreError);
  return false;
#else
  return dm->setAudioDeviceSetup(setup, true).isEmpty();
#endif
}

void StandaloneAudioSettings::rememberCurrentSetup() {
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  auto* p = props();
  if (device == nullptr || p == nullptr)
    return;

  // Channel masks: persist our tracked request, not dm->getAudioDeviceSetup()
  // or the device readback. AudioDeviceManager::setAudioDeviceSetup() ends by
  // calling updateCurrentSetup(), which unconditionally overwrites its own
  // currentSetup.inputChannels/outputChannels with the device's *active*
  // (post-open) readback - on hardware whose channel-count floor is above
  // what we asked for (ALSA's ensureMinimumNumBitsSet), that readback is
  // always the padded value, never our request, by the time this function
  // (called right after setAudioDeviceSetup returns) reads it. Remembering
  // that readback silently converts a saved mono selection into stereo.
  // Rate/buffer stay readback values: those are what the device actually
  // granted and are always safe to re-request.
  const auto inMask =
      !explicitInputChannels.isZero() ? explicitInputChannels : device->getActiveInputChannels();
  const auto outMask =
      !explicitOutputChannels.isZero() ? explicitOutputChannels : device->getActiveOutputChannels();

  auto* entry = new juce::DynamicObject();
  entry->setProperty("rate", device->getCurrentSampleRate());
  entry->setProperty("buffer", device->getCurrentBufferSizeSamples());
  entry->setProperty("in", inMask.toString(16));
  entry->setProperty("out", outMask.toString(16));
  // Own `entry` through a var immediately and never re-wrap the raw pointer:
  // NamedValueSet::set() skips storing a value that equals what's already
  // there for the key, and a skipped store deletes a sole-owner temporary
  // (was a live use-after-free when a log line re-wrapped `entry` here).
  const juce::var entryVar(entry);

  auto remembered = getRememberedSetups();
  if (auto* obj = remembered.getDynamicObject())
    obj->setProperty(currentSetupKey(), entryVar);
  p->setValue(kRememberedSetupsKey, juce::JSON::toString(remembered, true));
}

void StandaloneAudioSettings::applyRawInputMode() {
#if JUCE_IOS
  // JUCE opens the session with setCategory: and no mode, so it stays in
  // AVAudioSessionModeDefault - the voice chain. Its AGC levels the guitar
  // before the model ever sees it (pick attack and the guitar's volume knob
  // stop coming through), and the processing inflates
  // AVAudioSession.inputLatency, the figure the settings UI reports.
  // Measurement mode is the raw path.
  //
  // The mode goes in one setCategory:mode:options: call together with the
  // category options, never through setMode: alone: on iPadOS 26 a bare
  // setMode: from Default mode cleared the category options to MixWithOthers
  // only, which cost the A2DP, AirPlay and DefaultToSpeaker options JUCE
  // asked for. The same call drops AllowBluetoothHFP, the option that lets a
  // headset mic cap the whole session at 16 or 24 kHz (see IosAudioRoute.h).
  //
  // setCategory: clears the session mode, and JUCE calls it every time a
  // device opens, so this cannot be done once at startup. Device opens and
  // route changes both end up at the device manager's change broadcast, which
  // is where this is re-applied from.
  IosAudioRoute::configureSession();
#endif
}

void StandaloneAudioSettings::applyMonitoringPolicy() {
  auto* h = holder();
  if (h == nullptr)
    return;

  // Auto-manage monitoring only when the input/output pair changes. Selecting
  // a pair with no feedback risk (an interface, headphones, etc.) turns Hear
  // Yourself on so the user is heard immediately; a laptop mic + speakers pair
  // stays muted (with the UI banner explaining why). Within the same pair we
  // don't touch it, so a manual toggle sticks until the user switches devices
  // (on iOS, until the route's feedback risk changes).
  const auto key = monitoringKey();
  if (key == lastMonitoringKey)
    return;
  lastMonitoringKey = key;

  const bool mute = computeFeedbackRisk();
  auto& muteValue = h->getMuteInputValue();
  if (static_cast<bool>(muteValue.getValue()) != mute)
    muteValue.setValue(mute);
}

bool StandaloneAudioSettings::computeFeedbackRisk() const {
  auto* h = holder();
  auto* dm = deviceManager();
  auto* device = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
  if (h == nullptr || device == nullptr || !h->getProcessorHasPotentialFeedbackLoop())
    return false;
  if (device->getActiveInputChannels().isZero() || device->getActiveOutputChannels().isZero())
    return false;

#if JUCE_IOS
  return IosAudioRoute::isBuiltInMicToSpeaker();
#else
  const auto setup = dm->getAudioDeviceSetup();
  return looksLikeMicrophone(setup.inputDeviceName) && looksLikeSpeakers(setup.outputDeviceName);
#endif
}

juce::var StandaloneAudioSettings::finishApply(const juce::String& error) {
  if (error.isEmpty()) {
    rememberCurrentSetup();
    applyMonitoringPolicy();
  } else {
    // Failures are rare and were the hardest thing to diagnose remotely;
    // one line per failed apply is worth keeping in release logs.
    juce::Logger::writeToLog("[AudioSettings] apply failed: " + error);
  }
  return makeResult(error);
}

//==============================================================================
// Persistence helpers

juce::PropertySet* StandaloneAudioSettings::props() const {
  auto* h = holder();
  return h != nullptr ? h->settings.get() : nullptr;
}

juce::String StandaloneAudioSettings::currentSetupKey() const {
  auto* dm = deviceManager();
  const auto setup = dm->getAudioDeviceSetup();
  return dm->getCurrentAudioDeviceType() + "|" + setup.inputDeviceName + "|" +
         setup.outputDeviceName;
}

juce::String StandaloneAudioSettings::monitoringKey() const {
  auto key = currentSetupKey();
#if JUCE_IOS
  // The iOS device keeps one name ("iOS Audio") whatever is plugged in, so the
  // pair never changes; key on the feedback risk too, so plugging in
  // headphones or an interface re-runs the policy the way picking a new pair
  // does.
  key << (computeFeedbackRisk() ? "|risk" : "|safe");
#endif
  return key;
}

juce::var StandaloneAudioSettings::getRememberedSetups() const {
  if (auto* p = props()) {
    auto parsed = juce::JSON::parse(p->getValue(kRememberedSetupsKey));
    if (parsed.isObject())
      return parsed;
  }
  return juce::var(new juce::DynamicObject());
}

bool StandaloneAudioSettings::asioTypeHasDevices() {
#if JUCE_WINDOWS
  if (!cachedAsioAvailable.has_value()) {
    cachedAsioAvailable = false;
    if (auto* dm = deviceManager()) {
      for (auto* type : dm->getAvailableDeviceTypes()) {
        if (type->getTypeName() != "ASIO")
          continue;
        type->scanForDevices();
        cachedAsioAvailable = !type->getDeviceNames().isEmpty();
      }
    }
  }
  return *cachedAsioAvailable;
#else
  return false;
#endif
}
