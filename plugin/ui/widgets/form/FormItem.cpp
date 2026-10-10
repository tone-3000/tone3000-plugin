#include "FormItem.h"

#include <cmath>

namespace t3k::ui {

void FormItem::setSubpixelTop(float fraction) {
  if (juce::approximatelyEqual(fraction, subpixelTop_)) return;
  subpixelTop_ = fraction;
  // The children's snapped positions depend on it.
  resized();
  repaint();
}

void FormItem::placeChild(juce::Component& child, juce::Rectangle<float> box) const {
  child.setBounds(snapped(box));
}

void FormItem::placeChild(FormItem& child, juce::Rectangle<float> box) const {
  const auto bounds = snapped(box);
  const float fraction = subpixelTop_ + box.getY() - static_cast<float>(bounds.getY());
  // Set the fraction before the bounds so a resized() triggered by the
  // bounds change sees it; if the bounds did not change, re-lay out anyway.
  const bool phaseChanged = !juce::approximatelyEqual(fraction, child.subpixelTop_);
  child.subpixelTop_ = fraction;
  if (bounds != child.getBounds()) {
    child.setBounds(bounds);
  } else if (phaseChanged) {
    child.resized();
    child.repaint();
  }
}

juce::Rectangle<int> FormItem::snapped(juce::Rectangle<float> box) const {
  const int t = static_cast<int>(std::round(subpixelTop_ + box.getY()));
  const int b = static_cast<int>(std::round(subpixelTop_ + box.getBottom()));
  return {static_cast<int>(std::round(box.getX())), t, static_cast<int>(std::round(box.getWidth())), b - t};
}

void FormItem::heightChanged() {
  for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent()) {
    if (auto* host = dynamic_cast<FormHost*>(p)) {
      host->itemHeightChanged();
      return;
    }
  }
}

// FormStack
void FormStack::add(FormItem& item, float marginTop) {
  entries_.push_back({&item, marginTop});
  addAndMakeVisible(item);
}

void FormStack::setShown(FormItem& item, bool shown) {
  if (item.isVisible() == shown) return;
  item.setVisible(shown);
  itemHeightChanged();
}

void FormStack::setGap(float gap) {
  gap_ = gap;
  heightChanged();
}

void FormStack::setTrailing(float trailing) {
  trailing_ = trailing;
  heightChanged();
}

float FormStack::heightFor(float width) const {
  float y = 0;
  bool first = true;
  for (const auto& e : entries_) {
    if (!e.item->isVisible()) continue;
    y += (first ? 0 : gap_) + e.marginTop + e.item->heightFor(width);
    first = false;
  }
  return y + trailing_;
}

float FormStack::topOf(const FormItem& item) const {
  const auto width = static_cast<float>(getWidth());
  float y = 0;
  bool first = true;
  for (const auto& e : entries_) {
    if (!e.item->isVisible()) continue;
    y += (first ? 0 : gap_) + e.marginTop;
    if (e.item == &item) return y;
    y += e.item->heightFor(width);
    first = false;
  }
  return -1;
}

void FormStack::resized() {
  const auto width = static_cast<float>(getWidth());
  float y = 0;
  bool first = true;
  for (const auto& e : entries_) {
    if (!e.item->isVisible()) continue;
    y += (first ? 0 : gap_) + e.marginTop;
    const float h = e.item->heightFor(width);
    placeChild(*e.item, {0, y, width, h});
    y += h;
    first = false;
  }
}

}  // namespace t3k::ui
