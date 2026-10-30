// Standalone Input/Output desk. Software routes and direct Kemper controls
// are separate: applying one never pretends to configure the other.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace t3k::routing {
class StandaloneRouting;

class RoutingWindow final : public juce::DocumentWindow {
public:
  RoutingWindow(juce::Component* centreAround, StandaloneRouting&,
                std::function<void()> onOpenAudioSettings);
  ~RoutingWindow() override;
  bool busy() const;
  void closeButtonPressed() override;
  void visibilityChanged() override;

private:
  class Panel;
  std::unique_ptr<Panel> panel_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingWindow)
};
}  // namespace t3k::routing
