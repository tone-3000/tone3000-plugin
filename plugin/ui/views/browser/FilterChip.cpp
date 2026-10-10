#include "FilterChip.h"

#include <utility>

#include "core/Brand.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// The verified badge's 17.5x20 artwork at the glyph height.
constexpr float kBadgeAspect = 17.5f / 20.0f;
constexpr float kBadgeHeight = 18;
}  // namespace

FilterChip::FilterChip(juce::String label) : Clickable(label), label_(std::move(label)) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  addChildComponent(avatar_);
  fitToContent();
}

void FilterChip::setLabel(juce::String label) {
  label_ = std::move(label);
  setButtonText(label_);
  setName(label_);
  fitToContent();
}

void FilterChip::setLeadingIcon(Icon icon) {
  glyph_ = Glyph::icon;
  icon_ = icon;
  avatar_.setVisible(false);
  fitToContent();
}

void FilterChip::setLeadingSvg(const char* svg) {
  glyph_ = Glyph::svg;
  svg_ = svg;
  avatar_.setVisible(false);
  fitToContent();
}

void FilterChip::setLeadingBadge() {
  glyph_ = Glyph::badge;
  avatar_.setVisible(false);
  fitToContent();
}

Avatar& FilterChip::leadingAvatar() {
  glyph_ = Glyph::avatar;
  avatar_.setVisible(true);
  fitToContent();
  return avatar_;
}

void FilterChip::setTrailing(Trailing trailing) {
  trailing_ = trailing;
  fitToContent();
}

void FilterChip::setDot(bool dot) {
  dot_ = dot;
  fitToContent();
}

void FilterChip::setActive(bool active) {
  if (active == active_) return;
  active_ = active;
  repaint();
}

void FilterChip::setLocked(bool locked) {
  if (locked == locked_) return;
  locked_ = locked;
  setAlpha(locked_ ? theme::kDisabledOpacity : 1.0f);
  setMouseCursor(locked_ ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

// A lone glyph pads only enough to be a circle; anything more takes the
// text pads.
int FilterChip::sidePad() const {
  return label_.isEmpty() && trailing_ == Trailing::none ? (kHeight - kGlyph) / 2 : 1 + kPadX;
}

// Content runs glyph → label → trailing → dot with fixed gaps; without a
// label the trailing glyph sits a glyph gap from the leading one.
int FilterChip::naturalWidth() const {
  const bool hasLabel = label_.isNotEmpty(), hasGlyph = glyph_ != Glyph::none;
  int w = 2 * sidePad();
  if (hasGlyph) w += kGlyph;
  if (hasLabel) w += (hasGlyph ? kGlyphGap : 0) + juce::roundToInt(Fonts::width(Fonts::sans(kPx), label_));
  if (trailing_ != Trailing::none) w += (hasLabel ? kTrailingGap : hasGlyph ? kGlyphGap : 0) + kTrailing;
  if (dot_) w += kTrailingGap + kDot;
  return w;
}

void FilterChip::fitToContent() {
  setSize(naturalWidth(), kHeight);
  repaint();
}

juce::Rectangle<float> FilterChip::glyphBox() const {
  return juce::Rectangle<float>(static_cast<float>(sidePad()), (getHeight() - kGlyph) / 2.0f, kGlyph, kGlyph);
}

juce::Rectangle<float> FilterChip::trailingBox() const {
  const float right = static_cast<float>(getWidth() - sidePad()) - (dot_ ? kTrailingGap + kDot : 0);
  return juce::Rectangle<float>(right - kTrailing, (getHeight() - kTrailing) / 2.0f, kTrailing, kTrailing);
}

void FilterChip::resized() {
  if (glyph_ == Glyph::avatar) avatar_.setBounds(glyphBox().toNearestInt());
}

bool FilterChip::inClearZone(juce::Point<int> p) const {
  // The × and its gap, out to the chip's edge.
  return trailing_ == Trailing::clear && p.x >= trailingBox().getX() - kTrailingGap;
}

void FilterChip::mouseDown(const juce::MouseEvent& e) {
  clearPressed_ = inClearZone(e.getPosition());
  juce::Button::mouseDown(e);
}

void FilterChip::clicked() {
  const bool clear = std::exchange(clearPressed_, false);  // a key press (Enter) is never on the ×
  if (locked_) return;
  if (clear && onClear) onClear();
  else if (!clear && onPress) onPress();
}

// Keyboard: Backspace / Delete on a focused chip is its ×.
bool FilterChip::keyPressed(const juce::KeyPress& key) {
  const bool clearKey = key.isKeyCode(juce::KeyPress::backspaceKey) || key.isKeyCode(juce::KeyPress::deleteKey);
  if (!clearKey || locked_ || trailing_ != Trailing::clear || !onClear) return Clickable::keyPressed(key);
  onClear();
  return true;
}

void FilterChip::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const float radius = box.getHeight() / 2;
  paint::border(g, box, radius, active_ ? theme::kWhite : theme::kBorder);
  const auto fg = active_ ? theme::kWhite : theme::kGray;

  const auto glyph = glyphBox();
  switch (glyph_) {
    case Glyph::icon: Icons::draw(g, icon_, glyph, fg); break;
    case Glyph::svg: Icons::draw(g, svg_, glyph, fg); break;
    case Glyph::badge:
      Brand::drawVerifiedBadge(
          g, juce::Rectangle<float>(kBadgeHeight * kBadgeAspect, kBadgeHeight).withCentre(glyph.getCentre()));
      break;
    case Glyph::avatar:
    case Glyph::none: break;
  }

  float x = glyph_ != Glyph::none ? glyph.getRight() : glyph.getX();
  if (label_.isNotEmpty()) {
    if (glyph_ != Glyph::none) x += kGlyphGap;
    // The chip is sized from this same measure, so the label always fits:
    // no ellipsis, and the box is padded past any rounding of the measure.
    const float labelW = Fonts::width(Fonts::sans(kPx), label_);
    g.setFont(Fonts::sans(kPx));
    g.setColour(fg);
    g.drawText(label_, juce::Rectangle<float>(x, 0, labelW + 2, box.getHeight()).toNearestInt(),
               juce::Justification::centredLeft, false);
    x += labelW;
  }

  if (trailing_ != Trailing::none) {
    // Lucide's chevron at 1.33 stroke reads lighter than the × (the
    // mockups' 16px chevron-down vs. the 2px x).
    const auto tbox = trailingBox();
    if (trailing_ == Trailing::chevron) Icons::draw(g, Icon::ChevronDown, tbox, fg, 1.5f);
    else Icons::draw(g, Icon::X, tbox, fg);
    x = tbox.getRight();
  }

  if (dot_) {
    g.setColour(fg);
    g.fillEllipse(juce::Rectangle<float>(kDot, kDot).withCentre({x + kTrailingGap + kDot / 2.0f, box.getCentreY()}));
  }
}

}  // namespace t3k::ui
