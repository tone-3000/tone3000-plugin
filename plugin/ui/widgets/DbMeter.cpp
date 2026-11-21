#include "DbMeter.h"

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/MeterScale.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kScaleMarks[] = {-60, -48, -36, -24, -18, -12, -9, -6, -3, 0};
constexpr float kLabelFontPx = 8;
}  // namespace

DbMeter::DbMeter(MeterStore& meters, bool input, int height, Labels labels)
    : meters_(meters),
      input_(input),
      labels_(labels),
      rail_{kDotSize, kDotGap, DotRail::countForColumn(height, kDotSize, kDotGap), true} {
  setName(input ? "input meter" : "output meter");
  setAccessible(false);  // live levels: noise to a screen reader (a latched clip is not; see syncLatch)
  setMouseClickGrabsKeyboardFocus(false);
  setSize(kWidth, juce::roundToInt(rail_.length()));
  setPaintingIsUnclipped(true);
  setStereo(false);
  meters_.addListener(this);
}

DbMeter::~DbMeter() { meters_.removeListener(this); }

// One column reading the combined level, or L/R columns per channel.
void DbMeter::setStereo(bool stereo) {
  if (stereo == this->stereo() && !columns_.empty()) return;
  if (stereo)
    columns_ = {MeterStore::mainId(input_, MeterStore::Channel::left),
                MeterStore::mainId(input_, MeterStore::Channel::right)};
  else
    columns_ = {MeterStore::mainId(input_)};
  repaint();
  syncLatch();
}

bool DbMeter::anyClipped() const {
  for (const auto& id : columns_)
    if (meters_.clipped(id)) return true;
  return false;
}

void DbMeter::clearClips() {
  for (const auto& id : columns_)
    if (meters_.clipped(id)) meters_.clearClip(id);
}

// Edge-triggered: the meter speaks once when a clip latches and turns into
// a "Clear input clip" button until it clears; levels never notify.
void DbMeter::syncLatch() {
  const bool latched = anyClipped();
  if (latched == latched_) return;
  latched_ = latched;
  setTitle(latched ? juce::String(input_ ? "Clear input clip" : "Clear output clip") : juce::String());
  clipA11y::setLatched(*this, latched);
  if (latched && isShowing()) clipA11y::announce(input_ ? "Input" : "Output");
  repaint();
}

bool DbMeter::keyPressed(const juce::KeyPress& key) {
  if (!latched_ || !hasKeyboardFocus(false)) return false;
  return clipA11y::keyPress(key, [this] { clearClips(); });
}

std::unique_ptr<juce::AccessibilityHandler> DbMeter::createAccessibilityHandler() {
  return clipA11y::handler(*this, [this] { clearClips(); });
}

void DbMeter::meterChanged(const juce::String& id) {
  for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
    if (columns_[static_cast<size_t>(i)] == id) {
      refreshAffordance(getMouseXYRelative());
      syncLatch();
      // Only that column's rail moved; the labels and the other rail stay.
      repaint(columnBox(i).getSmallestIntegerContainer());
      return;
    }
}

// Row geometry: the labels+dots row is centred in the slot, which is the
// footprint less the margin that a tighter right-side gap gives back.
juce::Rectangle<float> DbMeter::columnBox(int i) const {
  const float labelGap = labels_ == Labels::left ? kLabelGap : kLabelGapRight;
  const float slotW = kLabelWidth + labelGap + kDotSize;
  const int n = static_cast<int>(columns_.size());
  const float dotsW = n * kDotSize + (n - 1) * kColumnGap;
  const float rowW = kLabelWidth + labelGap + dotsW;
  const float rowX = kInset + (slotW - rowW) / 2;
  const float dotsX = labels_ == Labels::left ? rowX + kLabelWidth + labelGap : rowX;
  return rail_.boxAt({dotsX + i * (kDotSize + kColumnGap), static_cast<float>(getHeight())});
}

void DbMeter::paint(juce::Graphics& g) {
  const float h = static_cast<float>(getHeight());
  const auto first = columnBox(0), last = columnBox(static_cast<int>(columns_.size()) - 1);

  // Labels: centred on the dot centres, MIN at the bottom dot, 0 dB at the
  // top (clip) dot; right-aligned digits in a fixed-width rail.
  const float labelGap = labels_ == Labels::left ? kLabelGap : kLabelGapRight;
  const float labelX =
      labels_ == Labels::left ? first.getX() - labelGap - kLabelWidth : last.getRight() + labelGap;
  g.setFont(Fonts::mono(kLabelFontPx));
  g.setColour(theme::kGray);
  for (int db : kScaleMarks) {
    const float cy = h - (kDotSize / 2 + meter::unit(static_cast<float>(db)) * (h - kDotSize));
    g.drawText(juce::String(db),
               juce::Rectangle<float>(labelX, cy - kLabelFontPx / 2, kLabelWidth, kLabelFontPx),
               juce::Justification::centredRight, false);
  }

  for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
    rail_.paint(g, columnBox(i), meters_.level(columns_[static_cast<size_t>(i)]),
                meters_.clipped(columns_[static_cast<size_t>(i)]));
  if (hasKeyboardFocus(false))
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
      if (meters_.clipped(columns_[static_cast<size_t>(i)])) clipA11y::paintFocus(g, rail_.clipDot(columnBox(i)));
}

juce::Point<int> DbMeter::clipDotCentre(int column) const {
  return rail_.clipDot(columnBox(column)).getCentre().toInt();
}

int DbMeter::clipColumnAt(juce::Point<int> p) const {
  for (int i = 0; i < static_cast<int>(columns_.size()); ++i)
    if (meters_.clipped(columns_[static_cast<size_t>(i)]) &&
        rail_.clipDot(columnBox(i)).contains(p.toFloat()))
      return i;
  return -1;
}

void DbMeter::refreshAffordance(juce::Point<int> p) {
  const bool over = clipColumnAt(p) >= 0;
  setHelpText(over ? help::text(help::Key::clipDot) : juce::String());
  setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void DbMeter::mouseMove(const juce::MouseEvent& e) { refreshAffordance(e.getPosition()); }
void DbMeter::mouseExit(const juce::MouseEvent&) { refreshAffordance({-1, -1}); }

void DbMeter::mouseDown(const juce::MouseEvent& e) {
  if (const int i = clipColumnAt(e.getPosition()); i >= 0)
    meters_.clearClip(columns_[static_cast<size_t>(i)]);
}

}  // namespace t3k::ui
