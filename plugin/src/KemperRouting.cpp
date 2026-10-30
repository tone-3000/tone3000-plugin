#include "KemperRouting.h"

#include <algorithm>

namespace t3k::capture::kemper {

const std::vector<IoParameterSpec>& ioParameters() {
  using P = IoParameter;
  using K = IoParameterSpec::Kind;
  static const std::vector<IoParameterSpec> parameters = [] {
    std::vector<IoParameterSpec> result{
      {P::inputNoiseGate, "Input", "Noise Gate", 9, 3, K::continuous, {}, true},
      {P::cleanSens, "Input", "Clean Sens", 9, 4, K::continuous, {}, true},
      {P::distortionSens, "Input", "Distortion Sens", 9, 5, K::continuous, {}, false},
      {P::mainVolume, "Main Output", "Volume", 127, 0, K::continuous, {}, true},
      {P::headphoneVolume, "Headphones", "Volume", 127, 1, K::continuous, {}, true},
      {P::monitorVolume, "Monitor Output", "Volume", 127, 2, K::continuous, {}, true},
      {P::directVolume, "Direct Output", "Volume", 127, 3, K::continuous, {}, false},
      {P::spdifVolume, "S/PDIF", "Volume", 127, 4, K::continuous, {}, false},
      {P::monitorCabOff, "Monitor Output", "Cabinet Off", 127, 8, K::toggle, {}, true},
      {P::mainBass, "Main Output", "Bass", 127, 12, K::continuous, {}, true},
      {P::mainMiddle, "Main Output", "Middle", 127, 13, K::continuous, {}, true},
      {P::mainTreble, "Main Output", "Treble", 127, 14, K::continuous, {}, true},
      {P::mainPresence, "Main Output", "Presence", 127, 15, K::continuous, {}, true},
      {P::outputLowCut, "Main Output", "Low Cut (all analog outputs)", 127, 16, K::continuous, {}, true},
      {P::monitorBass, "Monitor Output", "Bass", 127, 17, K::continuous, {}, true},
      {P::monitorMiddle, "Monitor Output", "Middle", 127, 18, K::continuous, {}, true},
      {P::monitorTreble, "Monitor Output", "Treble", 127, 19, K::continuous, {}, true},
      {P::monitorPresence, "Monitor Output", "Presence", 127, 20, K::continuous, {}, true},
      {P::outputHighCut, "Main Output", "High Cut (all analog outputs)", 127, 21, K::continuous, {}, true},
      {P::auxToMain, "Aux Input", "To Main Out", 127, 32, K::continuous, {}, false},
      {P::auxToMonitor, "Aux Input", "To Monitor Out", 127, 33, K::continuous, {}, false},
      {P::auxToHeadphones, "Aux Input", "To Phones", 127, 34, K::continuous, {}, false},
      {P::spaceIntensity, "Space", "Intensity", 127, 36, K::continuous, {}, true},
      {P::spaceRouting, "Space", "Routing (raw option)", 127, 37, K::choice,
        {"0 - verify destination on Kemper", "1 - verify destination on Kemper"}, false},
      {P::koneEnabled, "Kemper Kone", "Kemper Kone enabled", 127, 38, K::toggle, {}, true},
      {P::koneBassBoost, "Kemper Kone", "Bass Boost", 127, 39, K::toggle, {}, true},
      {P::koneImprint, "Kemper Kone", "Imprint Select (raw index)", 127, 40, K::choice, {}, true},
      {P::koneDirectivity, "Kemper Kone", "Directivity", 127, 41, K::continuous, {}, true},
      {P::koneSweetening, "Kemper Kone", "Sweetening", 127, 42, K::continuous, {}, true},
      {P::inputSource, "Input", "Input Source", 127, 44, K::choice,
        {"Front Input", "Alternative Input", "Return Input Reamp", "S/PDIF Input Reamp"}, false, true},
      {P::pureCabinetEnabled, "Pure Cabinet", "Pure Cabinet enabled", 127, 50, K::toggle, {}, true},
      {P::pureCabinetLevel, "Pure Cabinet", "Intensity", 127, 51, K::continuous, {}, true},
      {P::auxMono, "Aux Input", "Mono", 127, 59, K::toggle, {}, false}
    };
    for (auto& spec : result) {
      if (spec.id == P::koneImprint) {
        // The documented stable values are0..18. Firmware-specific speaker
        // names/order were not verified against the connected unit.
        for (int i = 0; i <= 18; ++i)
          spec.choices.add("Imprint " + juce::String(i) + " - check Kemper label");
        spec.supportNotes = "Global imprint index0..18; per-rig override can take precedence.";
      } else if (spec.id == P::inputSource) {
        spec.supportNotes = "Head/Rack menu order verified. Stage has no Alternative Input; its MIDI enumeration was not verified. USB Reamping is a separate manual control.";
      } else if (spec.id == P::spaceRouting) {
        spec.supportNotes = "MIDI13 gives the address, but does not map integer values to Headphones/Main + Phones. Verify the selected destination on the hardware.";
      } else if (spec.id == P::cleanSens) {
        spec.supportNotes = "Balances clean/distorted loudness; this is not an ordinary input gain. Raw MIDI position, not a verified dB conversion.";
      } else if (spec.id == P::distortionSens) {
        spec.supportNotes = "Not available on Player; raw position, not a verified dB conversion.";
      } else if (spec.id == P::mainBass || spec.id == P::mainMiddle || spec.id == P::mainTreble ||
                 spec.id == P::mainPresence || spec.id == P::monitorBass || spec.id == P::monitorMiddle ||
                 spec.id == P::monitorTreble || spec.id == P::monitorPresence) {
        spec.minimumPlayerLevel = 0;
        spec.supportNotes = "Player output EQ depends on OS/upgrade feature set: FAQ lists LevelII, while Main Manual14.2 includes output EQ without an upgrade note. Verify availability on the device. No dB conversion is assumed.";
      } else if (spec.id == P::koneDirectivity) {
        spec.supportNotes = "Effective in Speaker Imprint mode; Kone mode and a compatible speaker are required.";
      } else if (spec.id == P::koneSweetening) {
        spec.supportNotes = "Effective in Kone full-range mode.";
      } else if (spec.kind == K::continuous) {
        spec.supportNotes = "Raw hardware position0..16383. The accessed manufacturer MIDI table does not publish an exact physical-unit conversion.";
      }
    }
    return result;
  }();
  return parameters;
}

const IoParameterSpec* ioParameterSpec(IoParameter id) {
  const auto& parameters = ioParameters();
  const auto found = std::find_if(parameters.begin(), parameters.end(),
                                 [id](const auto& spec) { return spec.id == id; });
  return found == parameters.end() ? nullptr : &*found;
}

Messages encodeIoParameter(int channel, IoParameter id, int rawValue14) {
  const auto* spec = ioParameterSpec(id);
  if (spec == nullptr || rawValue14 < 0)
    return {};
  switch (spec->kind) {
    case IoParameterSpec::Kind::continuous:
      if (rawValue14 > 16383) return {};
      break;
    case IoParameterSpec::Kind::toggle:
      if (rawValue14 > 1) return {};
      break;
    case IoParameterSpec::Kind::choice:
      if (rawValue14 >= spec->choices.size()) return {};
      break;
  }
  return encodeNrpn(channel, spec->page, spec->index, rawValue14);
}

const std::vector<ManualIoSetting>& manualIoSettings() {
  static const std::vector<ManualIoSetting> settings{
    {"Input", "Reamp Sens", "Rig Manager > Input > Reamp Sens",
      "No address in accessed MIDI13. Applies to Return/S/PDIF reamping; USB Reamping is separate."},
    {"Main Output", "Output Source", "Rig Manager > Output > Main Output > Source",
      "Choose the real Main source: Off, DI Low/High, Stack, Mod, Master or DLY/REV Wet, as the device allows."},
    {"Main Output", "PAD -12 dB", "Rig Manager > Output > Main Output > PAD -12 dB",
      "Changes the analog Main output pad. A software trim is not this hardware pad."},
    {"Main Output", "Output Linked", "Rig Manager > Output > Main Output > Output Linked",
      "Links actual Main volume to the hardware Master Volume."},
    {"Monitor Output", "Output Source", "Rig Manager > Output > Monitor Output > Source",
      "Set the actual physical Monitor source; software destinations do not change this selector."},
    {"Monitor Output", "Output Linked", "Rig Manager > Output > Monitor Output > Output Linked", "Hardware Master Volume link."},
    {"Monitor Output", "Stereo", "Rig Manager > Output > Monitor Output > Stereo",
      "On Head/Rack this pairs Monitor with Direct; Stage has dedicated stereo Monitor."},
    {"Direct Output", "Output Source", "Rig Manager > Output > Direct Output > Source",
      "Head/Rack: Off, DI Analog/Low/High, Stack, Mod Right/Mono, Master Right/Mono. Not present on Player."},
    {"Direct Output", "Output Linked", "Rig Manager > Output > Direct Output > Output Linked", "Hardware Master Volume link; no Direct output on Player."},
    {"Headphones", "Output Linked", "Rig Manager > Output > Headphones > Output Linked", "Hardware Master Volume link."},
    {"S/PDIF", "Output Source", "Rig Manager > Output > S/PDIF Output > Source",
      "Select DI/processed split or processed stereo source on the hardware. Not present on Player."},
    {"S/PDIF", "Frequency", "Rig Manager > Output > S/PDIF Output > Frequency",
      "S/PDIF clock setting is independent of fixed44.1 kHz USB audio. Not present on Player."},
    {"S/PDIF", "Output Linked", "Rig Manager > Output > S/PDIF Output > Output Linked", "Hardware Master Volume link."},
    {"USB", "USB Reamping", "Rig Manager > Output > USB > USB Reamping",
      "Enable only for the chosen reamp path. Head/Rack MK1 uses USB1; Stage/Player/MK2 uses USB3."},
    {"USB", "Direct Monitor", "Rig Manager > Output > USB > Direct Monitor",
      "Required during Head/Rack MK1 USB reamping; disabled during Stage/Player/MK2 reamping per the manufacturer workflow."},
    {"USB", "Rec. Source", "Rig Manager > Output > USB > Rec. Source",
      "Configure the actual USB record channel pair; source menus differ by model and OS."},
    {"USB", "Reamp Source", "Rig Manager > Output > USB > Reamp Source",
      "Configure the actual USB return-source pair while USB Reamping is enabled."},
    {"USB", "DAW Volume", "Rig Manager > Output > USB > DAW Volume", "Controls hardware USB playback level; no verified MIDI13 address."},
    {"USB", "USB 3/4 Source", "Rig Manager > Output > USB > source for USB3+4 (model dependent)",
      "Stage/Player/MK2 have four USB channels. Head/Rack MK1 has only two."},
    {"Space", "Routing", "Rig Manager > Output > Space > Routing",
      "Use Headphones or Main + Phones when the raw MIDI routing value is not verified on your model."},
    {"Output", "Latency", "Rig Manager > Output > Latency", "Select Dynamic or Constant Latency manually."},
    {"Output", "Ground Lift", "Kemper hardware > Ground Lift for the relevant physical output",
      "Physical electrical switch; software cannot lift a hardware audio ground."},
    {"Main Output", "Main Out Stereo (Player)", "Rig Manager > Output > Main Output > Main Out Stereo",
      "Player/model dependent; requires the manufacturer's special Main XLR stereo adapter."}
  };
  return settings;
}

} // namespace t3k::capture::kemper
