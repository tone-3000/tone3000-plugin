#include "MainScreen.h"

namespace t3k::ui {

MainScreen::MainScreen(Services& services)
    : services_(services),
      spreadEnabled_(services.backend, "spreadEnabled"),
      inputMeter_(services.meters, /*input=*/true, kMeterHeight, DbMeter::Labels::left),
      outputMeter_(services.meters, /*input=*/false, kMeterHeight, DbMeter::Labels::right),
      chain_(services) {
  // The meters sit above the chain (its gutter fades), so stereo columns
  // overflowing into the centre aren't covered by it.
  addAndMakeVisible(chain_);
  addAndMakeVisible(inputMeter_);
  addAndMakeVisible(outputMeter_);

  spreadEnabled_.onChange = [this] { syncMeters(); };
  services_.chain.addListener(this);
  syncMeters();
}

MainScreen::~MainScreen() { services_.chain.removeListener(this); }

void MainScreen::chainChanged(const ChainState&) { syncMeters(); }

void MainScreen::syncMeters() {
  const auto& chain = services_.chain.state();
  // Two input bars whenever both source channels reach the chain (Stereo or
  // Dual Mono); the L/R picks fold onto one.
  inputMeter_.setStereo(chain.stereoInput && (chain.inputMode == InputMode::stereo ||
                                              chain.inputMode == InputMode::dualMono));
  // The output carries a real stereo image only when a stereo-image feature
  // is on (stereo mode, mono-mode spread, or dual mono's two voices) AND the
  // rig can reproduce it (dualMonoActive already implies that).
  outputMeter_.setStereo(((chain.stereoEnabled || spreadEnabled_.boolValue()) && chain.stereoOutput) ||
                         chain.dualMonoActive);
}

void MainScreen::resized() {
  auto area = getLocalBounds().reduced(kPadX, 0);
  // Each meter slot is the mono footprint; the component is wider by kInset
  // on both sides to hold the stereo overflow.
  const int meterY = (getHeight() - inputMeter_.getHeight()) / 2;
  inputMeter_.setTopLeftPosition(area.getX() - DbMeter::kInset, meterY);
  outputMeter_.setTopLeftPosition(area.getRight() - DbMeter::kFootprint - DbMeter::kInset, meterY);
  area.removeFromLeft(DbMeter::kFootprint);
  area.removeFromRight(DbMeter::kFootprint);
  chain_.setBounds(area);
}

}  // namespace t3k::ui
