#include "BlockLed.h"

#include "DotMeter.h"
#include "core/Help.h"
#include "core/Theme.h"

namespace t3k::ui {

BlockLed::BlockLed(MeterStore& meters, juce::String id, int size)
    : meters_(meters), id_(std::move(id)) {
  setSize(size, size);
  setAccessible(false);  // a live indicator: accessible only while latched (sync)
  setMouseClickGrabsKeyboardFocus(false);
  setTitle("Clear clip");
  setHelpText(help::text(help::Key::clipDot));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  meters_.addListener(this);
  sync();
}

BlockLed::~BlockLed() { meters_.removeListener(this); }

void BlockLed::paint(juce::Graphics& g) {
  g.setColour(theme::kBrandRed);
  g.fillEllipse(getLocalBounds().toFloat());
  if (hasKeyboardFocus(false)) clipA11y::paintFocus(g, getLocalBounds().toFloat());
}

void BlockLed::mouseDown(const juce::MouseEvent&) { meters_.clearClip(id_); }

void BlockLed::meterChanged(const juce::String& id) {
  if (id == id_) sync();
}

// Rendered only while latched, so the tile's layout treats it as absent
// otherwise (the web returns null).
void BlockLed::sync() {
  const bool latched = meters_.clipped(id_);
  // Edge-triggered so a latch speaks once ("<tone> clipping"), not per frame.
  // A latch already set when the tile is built stays quiet.
  if (latched == latched_) {
    setVisible(latched);
    return;
  }
  latched_ = latched;
  clipA11y::setLatched(*this, latched);
  setVisible(latched);
  if (latched && isShowing()) clipA11y::announce(clipA11y::context(*this));
}

bool BlockLed::keyPressed(const juce::KeyPress& key) {
  if (!latched_ || !hasKeyboardFocus(false)) return false;
  return clipA11y::keyPress(key, [this] { meters_.clearClip(id_); });
}

std::unique_ptr<juce::AccessibilityHandler> BlockLed::createAccessibilityHandler() {
  return clipA11y::handler(*this, [this] { meters_.clearClip(id_); });
}

}  // namespace t3k::ui
