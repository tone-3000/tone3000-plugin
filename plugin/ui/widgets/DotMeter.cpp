#include "DotMeter.h"

#include <cmath>

#include "core/Help.h"
#include "core/MeterScale.h"

namespace t3k::ui {

namespace {
constexpr float kDimOpacity = 0.22f;
}

// DotRail
juce::Rectangle<float> DotRail::boxAt(juce::Point<float> origin) const {
  return vertical ? juce::Rectangle<float>(origin.x, origin.y - length(), dotSize, length())
                  : juce::Rectangle<float>(origin.x, origin.y, length(), dotSize);
}

juce::Point<float> DotRail::centre(int i, juce::Rectangle<float> box) const {
  const float along = dotSize / 2 + i * (dotSize + gap);
  return vertical ? juce::Point<float>(box.getCentreX(), box.getBottom() - along)
                  : juce::Point<float>(box.getX() + along, box.getCentreY());
}

void DotRail::paint(juce::Graphics& g, juce::Rectangle<float> box, float db, bool clipped) const {
  for (int i = 0; i < count; ++i) {
    // Dot i sits at an exact dB threshold; the last one is exactly 0 dBFS and
    // doubles as the latching clip LED.
    const float position = count > 1 ? static_cast<float>(i) / (count - 1) : 0.0f;
    const float dotDb = meter::kMinDb + position * (meter::kMaxDb - meter::kMinDb);
    const bool active = i == count - 1 ? clipped : db >= dotDb;
    g.setColour(meter::gradientColour(position).withAlpha(active ? 1.0f : kDimOpacity));
    g.fillEllipse(dot(i, box));
  }
}

int DotRail::countForColumn(int height, float dotSize, float gap) {
  return static_cast<int>(std::floor(height / (dotSize + gap)));
}

int DotRail::countForStrip(int length, float dotSize, float gap) {
  return std::max(2, static_cast<int>(std::floor((length + gap) / (dotSize + gap))));
}

// DotMeter
DotMeter::DotMeter(int length, bool vertical)
    : rail_{kDotSize, kGap, DotRail::countForStrip(length, kDotSize, kGap), vertical} {
  const int extent = juce::roundToInt(rail_.length());
  const int across = static_cast<int>(kDotSize);
  setSize(vertical ? across : extent, vertical ? extent : across);
  setPaintingIsUnclipped(true);
  setAccessible(false);  // live levels
}

void DotMeter::setLevel(float db) {
  if (juce::exactlyEqual(db_, db)) return;
  db_ = db;
  repaint();
}

void DotMeter::setClipped(bool clipped) {
  if (clipped_ == clipped) return;
  clipped_ = clipped;
  refreshAffordance(getMouseXYRelative());
  repaint();
}

void DotMeter::paint(juce::Graphics& g) {
  rail_.paint(g, getLocalBounds().toFloat(), db_, clipped_);
}

bool DotMeter::overClearableClip(juce::Point<int> p) const {
  return clipped_ && onClearClip && rail_.clipDot(getLocalBounds().toFloat()).contains(p.toFloat());
}

// The LED is the only interactive spot: while it is lit and under the pointer
// the strip takes the clip hint and a pointer cursor, like the web's per-dot
// data-help / cursor.
void DotMeter::refreshAffordance(juce::Point<int> p) {
  const bool over = overClearableClip(p);
  setHelpText(over ? help::text(help::Key::clipDot) : juce::String());
  setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void DotMeter::mouseMove(const juce::MouseEvent& e) { refreshAffordance(e.getPosition()); }
void DotMeter::mouseExit(const juce::MouseEvent&) { refreshAffordance({-1, -1}); }

void DotMeter::mouseDown(const juce::MouseEvent& e) {
  if (overClearableClip(e.getPosition())) onClearClip();
}

// LiveDotMeter
LiveDotMeter::LiveDotMeter(MeterStore& meters, juce::String id, int length, bool vertical)
    : DotMeter(length, vertical), meters_(meters), id_(std::move(id)) {
  onClearClip = [this] { meters_.clearClip(id_); };
  meters_.addListener(this);
  sync();
}

LiveDotMeter::~LiveDotMeter() { meters_.removeListener(this); }

void LiveDotMeter::meterChanged(const juce::String& id) {
  if (id == id_) sync();
}

void LiveDotMeter::sync() {
  setLevel(meters_.level(id_));
  setClipped(meters_.clipped(id_));
}

}  // namespace t3k::ui
