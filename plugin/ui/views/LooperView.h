#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

// Tuner-style takeover. Closing it leaves the processor-owned loop running.
class LooperView : public juce::Component, private juce::Timer {
public:
  explicit LooperView(Services& services);
  ~LooperView() override { stopTimer(); }
  std::function<void()> onClose;
  void resized() override;
  void paint(juce::Graphics& g) override;
private:
  void timerCallback() override;
  Services& services_;
  IconButton close_{Icon::X, 44, 20};
  juce::TextButton record_{"Record"}, stop_{"Stop"}, play_{"Play"};
  juce::Slider mix_, pan_;
  juce::Label status_, mixLabel_, panLabel_;
};

}  // namespace t3k::ui
