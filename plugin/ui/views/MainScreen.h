// The band between header and faceplate on the main screen (Plugin.tsx
// middle section): input meter | chain | output meter. The horizontal inset
// is on the band; the meters centre in the full header-to-faceplate height.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/ChainStore.h"
#include "services/ParamBinding.h"
#include "services/Services.h"
#include "ChainScreen.h"
#include "widgets/DbMeter.h"

namespace t3k::ui {

class MainScreen : public juce::Component, private ChainStore::Listener {
public:
  static constexpr int kPadX = 24;
  // 358 matches Figma's BLOCK column (title + gap + card).
  static constexpr int kMeterHeight = 358;

  explicit MainScreen(Services& services);
  ~MainScreen() override;

  ChainScreen& chainScreen() { return chain_; }

  void resized() override;

private:
  void chainChanged(const ChainState& state) override;
  void syncMeters();

  Services& services_;
  ParamBinding spreadEnabled_;
  DbMeter inputMeter_, outputMeter_;
  ChainScreen chain_;
};

}  // namespace t3k::ui
