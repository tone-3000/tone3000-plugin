// Model picker (ModelSelect.tsx): a rounded track with ‹ name › stepper
// buttons, a hairline divider and the "n/N" folder tally; clicking the name
// opens the option list *above* the track (the picker sits at the card's
// bottom edge). The list lands scrolled to the active row, shows at most
// five rows before scrolling, and appends a dots row while the catalog
// fetch is in flight.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace t3k::ui {

class ModelSelect : public juce::Component {
public:
  struct Option {
    juce::String id;
    juce::String name;
  };

  static constexpr int kHeight = 36;
  static constexpr int kRowHeight = 41;  // 12px pads + ~17px line
  static constexpr int kMaxVisibleRows = 5;

  ModelSelect();
  ~ModelSelect() override;

  void setOptions(std::vector<Option> options);
  void setValue(const juce::String& id);
  // Catalog total for the "n/N" tally (differs from options.size() until the
  // catalog fetch lands).
  void setTotalCount(int total);
  // The fetch is in flight: dots row in the open list.
  void setLoading(bool loading);
  // Signed out: DISABLED_OPACITY and inert (the owner carries the hint).
  void setDisabledLook(bool disabled);

  std::function<void(const juce::String& id)> onChange;
  // The list just opened (retry a failed catalog fetch).
  std::function<void()> onOpen;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class StepButton;
  class Dropdown;

  int currentIndex() const;
  void step(int delta);
  void toggleList();
  void dismissAndSelect(const juce::String& id);
  void syncSteppers();
  void syncList();

  std::vector<Option> options_;
  juce::String value_;
  int total_ = 0;
  bool loading_ = false;

  std::unique_ptr<StepButton> prev_, next_;
  std::unique_ptr<juce::Component> trigger_;
  std::unique_ptr<Dropdown> list_;
};

}  // namespace t3k::ui
