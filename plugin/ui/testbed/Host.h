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
#include "core/WindowFit.h"
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
  // The service bundle under the root, for self-tests that read a flow's state.
  Services& bundle() { return services; }
  // The resize policy for a window around us (TestbedWindow), tracking the
  // chrome as NativeEditor's does.
  design::FitConstrainer& constrainer() { return bounds_; }

  void setExtraContentHeight(int total, int) override;
  void resized() override;
  void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }

private:
  // The design box at the current chrome: the least the window shows whole.
  int minDesignHeight() const;

  const double zoom_;
  const int stretch_;  // the scenario's; the window's own once the user drags it
  int chrome_ = 0;     // the root's strips, reported through setExtraContentHeight
  design::FitConstrainer bounds_;
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
