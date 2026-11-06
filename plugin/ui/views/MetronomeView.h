// The metronome takeover, laid out like the tuner (TunerView): six tapered
// bars a side that flash blue → yellow → red on each beat, alternating left
// and right, with the current BPM, the tempo and volume knobs and
// power in the column between them. Replaces the whole meters + chain band.
// Keys (taught by the hover hints): Space starts / stops, Up / Down step the
// tempo by 1 BPM, Right / Left the volume by 1 %, Shift makes the step 10,
// Escape or M closes.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/BeatPulse.h"
#include "services/Services.h"
#include "widgets/IconButton.h"
#include "widgets/ParamControls.h"

namespace t3k::ui {

class MetronomeView : public juce::Component, private juce::KeyListener, private UiClock::Listener {
public:
  explicit MetronomeView(Services& services);
  ~MetronomeView() override;

  // The top-right ✕ (the header's metronome button also closes).
  std::function<void()> onClose;

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

  // M: opens the view from the main screen (PluginRoot) and closes it here.
  static bool isToggleKey(const juce::KeyPress& key);

private:
  void tick() override;
  bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return keyPressed(key); }
  static void step(ParamKnob& knob, const KnobScale& scale, double units);
  void setAnimating(bool on);
  static void placeScaled(juce::Component& button, int x, int y);
  void paintBars(juce::Graphics& g, bool left, juce::Rectangle<int> row) const;
  void paintReadout(juce::Graphics& g) const;

  IconButton close_;
  ParamPowerButton power_;
  ParamKnob tempo_, volume_;
  Backend& backend_;
  UiClock& clock_;
  BeatPulse pulse_;
  juce::Rectangle<int> leftRow_, rightRow_, centre_;
};

}  // namespace t3k::ui
