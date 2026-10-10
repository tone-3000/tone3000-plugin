// The settings forms' controls (controls.tsx): the green pill switch, the
// radio / check indicator, the small Mono/Stereo segmented control and the
// outlined / CTA / plain-text buttons.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

#include "FormStyle.h"
#include "core/Icons.h"
#include "core/Tween.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

// Green pill switch mirroring the web ToggleSimple: 40×24 track (zinc-500
// off, #00D13B on), 16px white knob with a 4px inset, 300ms ease.
class PillToggle : public Clickable {
public:
  static constexpr int kWidth = 40, kHeight = 24;
  static constexpr int kKnob = 16, kInset = 4;
  static constexpr int kAnimMs = 300;

  PillToggle();

  void setValue(bool on, bool animate = true);
  bool value() const { return on_; }
  std::function<void(bool)> onChange;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  bool on_ = false;
  Tween progress_;  // 0 = off, 1 = on
};

// Round (radio) or rounded-square (check) selection indicator: 18px, 2px
// ring (white when selected, zinc-500 otherwise) around an 8px white dot.
struct ChoiceIndicator {
  static constexpr int kSize = 18;
  static void paint(juce::Graphics& g, juce::Rectangle<float> box, bool selected, bool square);
};

// Small segmented control (Mono / Stereo): #0a0a0a track with the field
// border, 2px padding, 11px/600 cells with the selected one filled.
class SegmentedControl : public juce::Component {
public:
  static constexpr int kPad = 2;
  static constexpr int kCellPadX = 12, kCellPadY = 4;
  static constexpr float kCellPx = 11;

  explicit SegmentedControl(std::vector<juce::String> labels);
  ~SegmentedControl() override;

  void setSelected(int index);
  int selected() const { return selected_; }
  std::function<void(int)> onChange;

  int preferredWidth() const;
  static int preferredHeight();

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Cell;
  std::vector<std::unique_ptr<Cell>> cells_;
  int selected_ = 0;
};

// The forms' text buttons. `outlined` is outlinedFieldStyle as a button
// (transparent, zinc-700 border, 14px), `cta` the full-width white-bordered
// call to action (ctaButtonStyle), `text` a borderless label (the "Reveal
// log file" and "Cancel" links).
class FormButton : public Clickable {
public:
  struct Look {
    std::optional<juce::Colour> border;
    float radius;
    int padX, padY;
    float fontPx;
    bool bold;
    juce::Colour text;
    juce::Justification align = juce::Justification::centred;
  };
  static Look outlined();
  static Look cta();
  static Look text(float px, bool bold, juce::Colour colour, int padX = 0, int padY = 0,
                   juce::Justification align = juce::Justification::centredLeft);

  FormButton(juce::String label, Look look);

  void setLabel(const juce::String& label);
  void setTextColour(juce::Colour colour);
  void setLeadingIcon(Icon icon, float px, int gap);
  // Border-box size the label wants.
  int preferredWidth() const;
  int preferredHeight() const;
  void fitToContent() { setSize(preferredWidth(), preferredHeight()); }

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  struct LeadingIcon {
    Icon icon;
    float px;
    int gap;
  };
  juce::Font font() const { return Fonts::sans(look_.fontPx, look_.bold); }

  juce::String label_;
  Look look_;
  std::optional<LeadingIcon> icon_;
};

}  // namespace t3k::ui
