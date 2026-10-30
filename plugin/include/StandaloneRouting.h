#pragma once

#include "RoutingPlan.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

class TONE3000Processor;

namespace t3k::routing {

/** The standalone device owner. It renders the existing processor once,
    maps genuine source buses to physical ports, and yields its I/O lease
    during raw capture. No routing configuration enters portable presets. */
class StandaloneRouting {
public:
  explicit StandaloneRouting(TONE3000Processor&);
  ~StandaloneRouting();
  static bool isAvailable();
  const Config& config() const;
  bool running() const;
  bool busy() const;
  juce::String status() const;
  juce::StringArray diagnostics() const;
  juce::StringArray inputNames() const;
  juce::StringArray outputNames() const;
  juce::String deviceDescription() const;
  juce::String currentInputDevice() const;
  juce::String currentOutputDevice() const;
  std::vector<StageChoice> stageChoices() const;
  // Apply validates the entire plan, fades down, and changes it while the
  // callback is detached. Opening a window never starts audio automatically.
  juce::Result apply(const Config&);
  void stop();
  float outputPeak(Port) const;
  float inputPeak() const;
  void refresh();
private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneRouting)
};

} // namespace t3k::routing
