// The scenario table: fixtures/scenarios.json (one mock backend state per
// screen the UI can show) plus the drive steps (clicks, hovers, scrolls)
// that take the UI from that state to the screen being captured.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace t3k::ui {
class PluginRoot;
}

namespace t3k::ui::testbed {

class MockBackend;

struct Scenario {
  juce::String id;
  juce::var data;  // the exported scenario entry (chain, device, auth, api, …)
  bool hasDrive = false;

  bool hints() const { return static_cast<bool>(data.getProperty("hints", true)); }
  bool banner() const { return static_cast<bool>(data.getProperty("banner", false)); }
  int settleMs() const { return static_cast<int>(data.getProperty("settle", 400)); }
  // Window zoom to lay the root out at (the grid holds 1x under it).
  double zoom() const { return static_cast<double>(data.getProperty("zoom", 1.0)); }
};

struct Fixtures {
  juce::var root;  // whole scenarios.json
  std::vector<Scenario> scenarios;

  static Fixtures load(const juce::File& scenariosJson);
  const Scenario* find(const juce::String& id) const;
};

// A drive step runs against the live root + mock after the scenario has
// settled, then the capture waits `settle` again. Null when the scenario
// has no drive (the capture table flags ids whose `hasDrive` says it needs
// one).
using Drive = std::function<void(PluginRoot&, MockBackend&)>;
const Drive* driveFor(const juce::String& scenarioId);

}  // namespace t3k::ui::testbed
