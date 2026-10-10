#include "HintBus.h"

namespace t3k::ui {

HintBus::HintBus(UiPrefs& prefs) : prefs_(prefs) {}

void HintBus::setHover(const juce::String& text) {
  if (hover_ == text) return;
  hover_ = text;
  listeners.call([](Listener& l) { l.hintChanged(); });
}

void HintBus::pin(const juce::String& text) {
  if (pinned_ == text) return;
  pinned_ = text;
  listeners.call([](Listener& l) { l.hintChanged(); });
}

void HintBus::unpin(const juce::String& text) {
  if (pinned_ != text) return;
  pinned_.clear();
  listeners.call([](Listener& l) { l.hintChanged(); });
}

void HintBus::setEnabled(bool enabled) {
  if (this->enabled() == enabled) return;
  prefs_.setBool(UiPrefs::kShowHints, enabled);
  listeners.call([](Listener& l) { l.hintChanged(); });
}

// Tracker
HintTracker::HintTracker(HintBus& bus, juce::Component& root) : bus_(bus), root_(root) {
  root_.addMouseListener(this, true);
}

HintTracker::~HintTracker() { root_.removeMouseListener(this); }

void HintTracker::resolve(const juce::MouseEvent& e) {
  for (auto* c = e.eventComponent; c != nullptr && c != root_.getParentComponent();
       c = c->getParentComponent()) {
    if (c->getHelpText().isNotEmpty()) {
      bus_.setHover(c->getHelpText());
      return;
    }
  }
  bus_.setHover({});
}

void HintTracker::mouseExit(const juce::MouseEvent& e) {
  // Only a root exit means the pointer left the UI; child exits are followed
  // by an enter/move on the newly hovered component.
  if (e.eventComponent == &root_) bus_.setHover({});
}

void HintTracker::mouseUp(const juce::MouseEvent& e) {
  // A touch press is the hover equivalent, so the release is the un-hover.
  if (e.source.isTouch()) bus_.setHover({});
}

}  // namespace t3k::ui
