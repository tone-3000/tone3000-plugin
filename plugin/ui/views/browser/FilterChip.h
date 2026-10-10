// A filter bar chip (the Select tone mockups' pill controls): an optional
// leading glyph (a Lucide icon, a gear glyph, the creator avatar or the
// verified badge), a 14px label, and an optional trailing chevron (the chip
// opens a menu) or × (the chip clears its value). Chips read gray until
// active, then white; a chip that is only a glyph is a circle. Sizes itself
// from its content.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "core/Icons.h"
#include "widgets/Avatar.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class FilterChip : public Clickable {
public:
  static constexpr int kHeight = 38;  // 20px glyph + 8px pads + 1px borders
  enum class Trailing { none, chevron, clear };

  explicit FilterChip(juce::String label = {});

  void setLabel(juce::String label);
  void setLeadingIcon(Icon icon);
  void setLeadingSvg(const char* svg);
  void setLeadingBadge();  // the verified badge, in its brand colours
  // The creator avatar; the caller feeds it the image.
  Avatar& leadingAvatar();
  void setTrailing(Trailing trailing);
  // A 6px dot after the glyph (the filter button while filters are set).
  void setDot(bool dot);
  void setActive(bool active);
  // Dimmed and inert, but still hoverable so its hint can say why.
  void setLocked(bool locked);

  bool active() const { return active_; }
  int naturalWidth() const;

  // A press on the × clears; anywhere else is the chip's own action.
  std::function<void()> onPress;
  std::function<void()> onClear;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;

private:
  enum class Glyph { none, icon, svg, badge, avatar };
  static constexpr int kPadX = 16;
  static constexpr int kGlyph = 20;
  static constexpr int kGlyphGap = 8;
  static constexpr int kTrailing = 16;
  static constexpr int kTrailingGap = 6;
  static constexpr int kDot = 6;
  static constexpr float kPx = 14;

  void clicked() override;
  void fitToContent();
  bool inClearZone(juce::Point<int> p) const;
  int sidePad() const;
  juce::Rectangle<float> glyphBox() const;
  juce::Rectangle<float> trailingBox() const;

  juce::String label_;
  Glyph glyph_ = Glyph::none;
  Icon icon_ = Icon::X;
  const char* svg_ = nullptr;
  Avatar avatar_;
  Trailing trailing_ = Trailing::none;
  bool dot_ = false;
  bool active_ = false;
  bool locked_ = false;
  bool clearPressed_ = false;
};

}  // namespace t3k::ui
