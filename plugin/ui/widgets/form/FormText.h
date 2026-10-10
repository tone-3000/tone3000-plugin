// Text items of the settings forms: section labels (one line of 15px/600)
// and paragraphs (wrapped 14px body copy on a 1.45 line-height grid, or the
// 12px captions / italic empty-state notes).
#pragma once

#include "FormItem.h"
#include "FormStyle.h"
#include "widgets/RichTextView.h"

namespace t3k::ui {

// A single line of text at the font's normal line height (sectionLabelStyle
// and the plain "Rate" field label). Never wraps or ellipsises: the web
// labels were short by design.
class FormLabel : public FormItem {
public:
  FormLabel(juce::String text, float px = form::kLabelPx, bool bold = true, juce::Colour colour = theme::kWhite);

  void setText(const juce::String& text);
  const juce::String& text() const { return text_; }
  // Restyle after construction (a row demoted to a child of a section).
  void setStyle(float px, bool bold, juce::Colour colour);
  // The label is a bare span in a block whose body font is `bodyPx` (the
  // web's 16px body): the line box also holds that strut, so it is taller
  // than the label's own line (18px instead of 17 for a 15px label).
  void setStrut(float bodyPx);

  float heightFor(float) const override;
  // Rendered width of the label.
  float preferredWidth() const;

  void paint(juce::Graphics& g) override;

private:
  float ascent() const;

  juce::String text_;
  float px_;
  bool bold_;
  juce::Colour colour_;
  float strutPx_ = 0;
};

// Wrapped copy. Defaults to descriptionStyle (14px muted, line-height 1.45);
// pass another size/colour for captions and footers.
class Paragraph : public RichTextView {
public:
  explicit Paragraph(const juce::String& text = {}, float px = form::kBodyPx, juce::Colour colour = theme::kMuted,
                     juce::Justification align = juce::Justification::left,
                     float lineHeight = form::kBodyLineHeight);
  explicit Paragraph(RichText runs, float px = form::kBodyPx, juce::Colour colour = theme::kMuted,
                     juce::Justification align = juce::Justification::left,
                     float lineHeight = form::kBodyLineHeight);

  using RichTextView::setText;
  // Whole paragraph in italics (the empty-state notes).
  void setItalicText(const juce::String& text);
};

}  // namespace t3k::ui
