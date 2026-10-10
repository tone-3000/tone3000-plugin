// One PluginRoot over the mocks, fitted into whatever bounds the shell has,
// exactly like NativeEditor does it: the capture run, the interactive window
// and the windowed self-tests all stand on this.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

#include "MockBackend.h"
#include "MockSession.h"
#include "Scenarios.h"
#include "core/Fonts.h"
#include "services/Services.h"
#include "views/PluginRoot.h"
#include "widgets/KnobFace.h"

namespace t3k::ui::testbed {

// Baked at configure time so the binary finds the fixtures from any cwd.
juce::File fixturesDir();

class ScaledHost : public juce::Component, public Shell {
public:
  ScaledHost(Backend& backend, const Scenario& scenario, const juce::var& fixtures);

  PluginRoot& pluginRoot() { return *root; }

  void setExtraContentHeight(int total, int) override;
  void resized() override;
  void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }

private:
  const double zoom_;
  // As in NativeEditor: pin the shared typefaces and knob layers for the
  // host's lifetime so paints reuse them instead of rebuilding per call.
  Fonts::Hold fontsHold_;
  KnobFaceCacheHold knobFacesHold_;
  UiPrefs prefs;
  MockSession session;
  Services services;
  std::unique_ptr<PluginRoot> root;
};

}  // namespace t3k::ui::testbed
