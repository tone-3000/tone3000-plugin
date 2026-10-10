#include "BlockLed.h"

#include "core/Help.h"
#include "core/Theme.h"

namespace t3k::ui {

BlockLed::BlockLed(MeterStore& meters, juce::String id, int size)
    : meters_(meters), id_(std::move(id)) {
  setSize(size, size);
  setAccessible(false);  // a live indicator: noise to a screen reader
  setHelpText(help::text(help::Key::clipDot));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  meters_.addListener(this);
  sync();
}

BlockLed::~BlockLed() { meters_.removeListener(this); }

void BlockLed::paint(juce::Graphics& g) {
  g.setColour(theme::kBrandRed);
  g.fillEllipse(getLocalBounds().toFloat());
}

void BlockLed::mouseDown(const juce::MouseEvent&) { meters_.clearClip(id_); }

void BlockLed::meterChanged(const juce::String& id) {
  if (id == id_) sync();
}

// Rendered only while latched, so the tile's layout treats it as absent
// otherwise (the web returns null).
void BlockLed::sync() { setVisible(meters_.clipped(id_)); }

}  // namespace t3k::ui
