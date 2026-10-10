// A wrapped paragraph of mixed-style text (RichFlow) as a component: sizes
// its own height from the width it is given, and turns link runs into
// clickable spots (hand cursor, `onLink` with the href). The update notice's
// remote message and the settings copy render through it. As a FormItem it
// also reports its height for any width, so the settings forms can flow it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/RichText.h"
#include "widgets/form/FormItem.h"

namespace t3k::ui {

class RichTextView : public FormItem {
public:
  RichTextView(float px, float lineHeightPx, juce::Colour colour,
               juce::Justification align = juce::Justification::left);

  // Re-lays out for the current width; the height follows (setSize) unless
  // a form host owns the bounds (setAutoHeight(false)).
  void setText(RichText runs);
  void setText(const juce::String& text) { setText(RichText{TextRun::plain(text)}); }
  const RichText& text() const { return runs_; }
  void setColour(juce::Colour colour);
  // Lay out for `width` (the height adjusts).
  void setWidth(int width);
  void setAutoHeight(bool autoHeight) { autoHeight_ = autoHeight; }
  int lineCount() const;

  float heightFor(float width) const override;

  std::function<void(const juce::String& href)> onLink;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  // The flow laid out at `width`, memoised: a stack measures each item via
  // heightFor and then lays it out at that same width.
  const RichFlow& flowFor(float width) const;
  void reflow();
  juce::String linkAt(juce::Point<int> p) const;

  float px_, lineHeightPx_;
  juce::Colour colour_;
  juce::Justification align_;
  RichText runs_;
  mutable std::unique_ptr<RichFlow> flow_;
  mutable float flowWidth_ = -1;
  bool autoHeight_ = true;
};

}  // namespace t3k::ui
