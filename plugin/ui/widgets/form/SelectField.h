// Custom dropdown select styled like the plugin's other pickers (controls.tsx
// SelectField): outlined trigger with a chevron, black panel of hover-lit
// rows below it. Renders disabled (dimmed, no chevron) for locked
// single-option lists; per the audio settings spec, a one-option select must
// never pretend to be a choice. A null value is the empty state: the trigger
// shows the dimmed placeholder and no option renders as selected. Options
// may carry a sublabel (the MIDI mapping picker names each block slot's
// current tone this way); the trigger always shows the label alone.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "FormItem.h"

namespace t3k::ui {

class SelectField : public FormItem {
public:
  struct Option {
    juce::String value;
    juce::String label;
    juce::String sublabel;
  };

  static constexpr int kChevron = 16;
  static constexpr int kListGap = 4;
  static constexpr int kListMaxHeight = 264;
  static constexpr float kSublabelPx = 11;
  static constexpr int kSublabelGap = 2;

  // `name` is the accessible label (ariaLabel) the drives find it by.
  explicit SelectField(const juce::String& name = {});
  ~SelectField() override;

  void setOptions(std::vector<Option> options);
  const std::vector<Option>& options() const { return options_; }
  // nullopt = placeholder state.
  void setValue(std::optional<juce::String> value);
  const std::optional<juce::String>& value() const { return value_; }
  void setPlaceholder(const juce::String& text);
  void setDisabled(bool disabled);
  bool isOpen() const;
  void open();
  void close();

  std::function<void(const juce::String& value)> onChange;

  float heightFor(float) const override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  class Dropdown;
  const Option* selected() const;
  void pick(const juce::String& value);

  std::vector<Option> options_;
  std::optional<juce::String> value_;
  juce::String placeholder_;
  bool disabled_ = false;
  std::unique_ptr<Dropdown> dropdown_;
};

}  // namespace t3k::ui
