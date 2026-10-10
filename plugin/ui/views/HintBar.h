// Hint strip under the faceplate (port of HintBar.tsx): the current help
// line on the left, the CPU readout and a × that hides the bar on the right.
// Like the banner it grows the window rather than eating into the plugin.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class HintBar : public juce::Component, private HintBus::Listener {
public:
  explicit HintBar(Services& services);
  ~HintBar() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  // Audio-callback load, tabular in a fixed-width slot so the row doesn't
  // shimmy as digits change.
  class CpuReadout : public juce::Component, private MeterStore::Listener {
  public:
    explicit CpuReadout(MeterStore& meters);
    ~CpuReadout() override;
    void paint(juce::Graphics& g) override;

  private:
    void cpuChanged() override { repaint(); }
    MeterStore& meters_;
  };

  void hintChanged() override { repaint(); }

  Services& services_;
  CpuReadout cpu_;
  IconButton hide_{Icon::X, 20, 16};
};

}  // namespace t3k::ui
