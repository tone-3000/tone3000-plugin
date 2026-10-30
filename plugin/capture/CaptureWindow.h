// Standalone capture desk. The native editor owns this window; closing it
// restores normal playback, keeps a dry/wet take, and cancels partial sweeps.
// Reviewed takes and background processing/export survive while it is hidden.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace t3k::capture {

class CaptureWindow final : public juce::DocumentWindow {
public:
  CaptureWindow(juce::Component* centreAround, std::function<void()> onOpenAudioSettings);
  ~CaptureWindow() override;
  void closeButtonPressed() override;
  bool busy() const;

private:
  class Panel;
  std::unique_ptr<Panel> panel_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaptureWindow)
};

}  // namespace t3k::capture
