// The repeating field shapes of the settings tabs (controls.tsx): a label
// with help copy over a control (FieldRow), a label with a pill switch and
// its description (ToggleRow), a radio option with its description, the
// bordered icon-titled card (SettingsGroup) and the (i) tip line.
#pragma once

#include <functional>
#include <memory>

#include "FormControls.h"
#include "FormItem.h"
#include "FormText.h"
#include "core/Icons.h"

namespace t3k::ui {

// Label + help + control. Children go into content() (a FormStack); an
// optional fixed-size component sits at the label's right (labelExtra).
class FieldRow : public FormItem {
public:
  static constexpr int kHelpGapTop = 8;

  FieldRow(const juce::String& label, const juce::String& help = {});

  void setLabel(const juce::String& label) {
    label_.setText(label);
    resized();
  }
  void setHelp(const juce::String& help);
  // The web wrote some sections as a bare label span in a div rather than a
  // FieldRow (whose flex header blockifies the span): the body strut makes
  // that line 18px tall instead of 17.
  void setInlineLabel() { label_.setStrut(form::kBodyStrutPx); }
  // A control beside the label (the Mono/Stereo switch); nullptr clears.
  void setLabelExtra(juce::Component* extra);
  FormStack& content() { return content_; }

  float heightFor(float width) const override;
  void resized() override;

private:
  float headerHeight() const;

  FormLabel label_;
  juce::Component* extra_ = nullptr;
  Paragraph help_;
  FormStack content_;
};

// Section label with a pill toggle on the right, description underneath,
// and an optional block of controls that shows while the row is expanded.
// setNested() demotes the row to a child of the section above it: indented,
// with a body-size label instead of the section label.
class ToggleRow : public FormItem {
public:
  static constexpr int kDescriptionGap = 4;
  static constexpr int kNestedIndent = 20;

  ToggleRow(const juce::String& label, RichText description);
  ToggleRow(const juce::String& label, const juce::String& description)
      : ToggleRow(label, RichText{TextRun::plain(description)}) {}

  void setNested();

  void setValue(bool on, bool animate = true) { toggle_.setValue(on, animate); }
  bool value() const { return toggle_.value(); }
  std::function<void(bool)> onChange;

  // Controls / tips shown 16px under the description while expanded.
  FormStack& content() { return content_; }
  void setExpanded(bool expanded);
  bool expanded() const { return content_.isVisible(); }

  float heightFor(float width) const override;
  void resized() override;

private:
  FormLabel label_;
  PillToggle toggle_;
  Paragraph description_;
  FormStack content_{form::kControlGap};
  int indent_ = 0;
};

// Radio row with label + description (NAM A2 Size options etc).
class RadioOption : public FormItem {
public:
  static constexpr int kGap = 12;
  static constexpr int kDescriptionGap = 4;
  static constexpr float kLabelLineHeight = 1.3f;

  RadioOption(const juce::String& label, const juce::String& description);

  void setSelected(bool selected);
  bool selected() const { return selected_; }
  std::function<void()> onSelect;

  float heightFor(float width) const override;
  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  static float textX() { return ChoiceIndicator::kSize + kGap; }

  juce::String label_;
  Paragraph description_;
  bool selected_ = false;
};

// Bordered settings card with an icon + uppercase title (AUDIO INTERFACE).
// Children stack with a tighter internal gap; the card's outer SECTION_GAP
// is the page's business.
class SettingsGroup : public FormItem {
public:
  static constexpr int kPad = 20, kIcon = 20, kIconGap = 10, kTitleGap = 16;
  static constexpr float kTitlePx = 16;

  // `iconSvg` is a CustomIcons.h constant drawn white at 20px.
  SettingsGroup(const juce::String& title, const char* iconSvg);

  FormStack& content() { return content_; }

  float heightFor(float width) const override;
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  static float headerHeight();

  juce::String title_;
  const char* iconSvg_;
  FormStack content_{form::kControlGap};
};

// (i) glyph beside a line or two of white body copy, vertically centred.
class TipRow : public FormItem {
public:
  static constexpr int kIcon = 20, kGap = 16;

  explicit TipRow(RichText text);

  float heightFor(float width) const override;
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  Paragraph text_;
};

}  // namespace t3k::ui
