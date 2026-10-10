// Central help system (port of the store half of helpText.ts). Controls set
// their hint with Component::setHelpText; HintTracker resolves the nearest
// hinted ancestor of whatever the pointer is over and publishes it here.
// `pin` overrides hover for the duration of an interaction (a knob drag can
// wander off the knob without releasing).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "UiPrefs.h"

namespace t3k::ui {

class HintBus {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void hintChanged() = 0;
  };

  explicit HintBus(UiPrefs& prefs);

  // Current help line ("" = nothing hovered).
  juce::String current() const { return pinned_.isNotEmpty() ? pinned_ : hover_; }

  void setHover(const juce::String& text);
  void pin(const juce::String& text);
  void unpin(const juce::String& text);

  // Whether the hint bar shows at all (per-machine preference, on by default).
  bool enabled() const { return prefs_.getBool(UiPrefs::kShowHints, true); }
  void setEnabled(bool enabled);

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

private:
  UiPrefs& prefs_;
  juce::String hover_, pinned_;
  juce::ListenerList<Listener> listeners;
};

// Installed on the root component as a recursive mouse listener; walks up
// from the event component to the nearest one with help text. Touch presses
// show the hint only while the finger is down.
class HintTracker : public juce::MouseListener {
public:
  explicit HintTracker(HintBus& bus, juce::Component& root);
  ~HintTracker() override;

  void mouseMove(const juce::MouseEvent& e) override { resolve(e); }
  void mouseEnter(const juce::MouseEvent& e) override { resolve(e); }
  void mouseDown(const juce::MouseEvent& e) override { resolve(e); }
  void mouseExit(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  void resolve(const juce::MouseEvent& e);

  HintBus& bus_;
  juce::Component& root_;
};

// RAII pin for the duration of a gesture.
class HintPin {
public:
  HintPin(HintBus& bus, juce::String text) : bus_(bus), text_(std::move(text)) { bus_.pin(text_); }
  ~HintPin() { bus_.unpin(text_); }
  HintPin(const HintPin&) = delete;
  HintPin& operator=(const HintPin&) = delete;

private:
  HintBus& bus_;
  juce::String text_;
};

}  // namespace t3k::ui
