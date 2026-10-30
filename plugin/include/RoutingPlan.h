#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <string>
#include <vector>

namespace t3k::routing {

enum class Source {
  off, di, stack, modLeft, modRight, modMono, modStereo,
  masterLeft, masterRight, masterMono, masterStereo, wetFx,
  diStack, diModLeft, diModMono, diMasterLeft, diMasterMono, returns
};
enum class Port { main, monitor, direct, phones, spdif, usb };
enum class InputSource { instrument, alternative, returnReamp, digitalReamp };
constexpr size_t portCount = 6;

const char* sourceName(Source);
const char* portName(Port);
const char* inputSourceName(InputSource);
const std::vector<Source>& sources();
bool stereoSource(Source);

struct Output {
  Source source = Source::off;
  int left = -1, right = -1; // Physical interface channels; -1 = unassigned.
  float volumeDb = 0.0f;
  bool linkMaster = false, pad12 = false, stereo = true, cabinetOff = false;
  float bassDb = 0, middleDb = 0, trebleDb = 0, presenceDb = 0;
  float lowCutHz = 0, highCutHz = 0; // Zero = off.
};
struct Config {
  juce::String inputDevice, outputDevice;
  InputSource inputSource = InputSource::instrument;
  std::array<int, 4> inputChannels{0, 0, 0, 0};
  int stereoInputRight = -1;
  int diInput = 0;
  std::array<int, 2> returnInputs{-1, -1};
  std::array<int, 2> auxInputs{-1, -1};
  float inputGainDb = 0, reampSensitivityDb = 0, masterVolumeDb = 0;
  std::array<float, 3> auxMix{0, 0, 0}; // Main / Monitor / Phones.
  std::array<Output, portCount> outputs;
  // Explicit stage UUIDs for each rendered lane. Missing/stale assignments
  // are diagnosed, never silently replaced with a different signal stage.
  std::array<std::string, 2> stackEnd, modEnd, wetStart, cabinet;
  bool enabled = false;
  Config();
};

struct StageChoice {
  std::string id;
  juce::String label;
  int lane = 0;
  bool cabinetCandidate = false;
};

juce::var toVar(const Config&);
juce::Result fromVar(const juce::var&, Config&);
juce::StringArray validate(const Config&, int inputCount, int outputCount);
bool sameDeviceTopology(const Config&, const Config&);
std::vector<std::string> effectivePath(const juce::var& chainState, int renderedLane);

} // namespace t3k::routing
