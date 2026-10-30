#pragma once

#include "KemperMidi.h"

namespace t3k::capture::kemper {

enum class IoParameter {
  inputNoiseGate, cleanSens, distortionSens,
  mainVolume, headphoneVolume, monitorVolume, directVolume, spdifVolume,
  monitorCabOff, mainBass, mainMiddle, mainTreble, mainPresence, outputLowCut,
  monitorBass, monitorMiddle, monitorTreble, monitorPresence, outputHighCut,
  auxToMain, auxToMonitor, auxToHeadphones, auxMono,
  spaceIntensity, spaceRouting, koneEnabled, koneBassBoost, koneImprint,
  koneDirectivity, koneSweetening, inputSource, pureCabinetEnabled, pureCabinetLevel
};

/** Manufacturer MIDI Parameter Documentation 13. Continuous values below
    are raw hardware positions (0..16383), never assumed dB, Hz or percent.
    Choice values are literal zero-based integers, not normalized positions.
    Availability is conservative; Player upgrade/OS differences still apply. */
struct IoParameterSpec {
  enum class Kind { continuous, toggle, choice };
  IoParameter id;
  juce::String section, label;
  int page, index;
  Kind kind;
  juce::StringArray choices;
  bool playerSupported;
  bool headRackOnly = false;
  int minimumPlayerLevel = 1; //0 means OS/upgrade availability must be checked on device.
  juce::String supportNotes = {};
};

const std::vector<IoParameterSpec>& ioParameters();
const IoParameterSpec* ioParameterSpec(IoParameter);
Messages encodeIoParameter(int channel, IoParameter, int rawValue14);

/** Fields without an accessed/verified MIDI mapping. They deliberately have
    no command address. These labels direct the user to the real device UI. */
struct ManualIoSetting {
  juce::String section, label, destination, notes;
};
const std::vector<ManualIoSetting>& manualIoSettings();

} // namespace t3k::capture::kemper
