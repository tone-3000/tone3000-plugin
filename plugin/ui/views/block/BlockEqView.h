// 6-band EQ editor shown in the card body while the header EQ toggle is
// active (port of BlockEqView.tsx + EqSliders.tsx + SpectrumBackdrop.tsx),
// with two interchangeable views chosen from the header's EQ menu:
//
// - Graph: the full editor. Grid bleeds edge-to-edge, controls float over
//   it. Drag dots for freq/gain (vertical drag tunes Q on cut bands), scroll
//   to tune the selected band's Q; type selector and Freq/Gain/Q chips for
//   the selected band.
// - Sliders: a Mesa-style graphic EQ mirroring the same bands. Gain only.
//
// Interaction conventions (mirroring the knobs): Shift = 8x finer, Alt-click
// (touch: double tap) resets a band's effect without touching its
// frequency. Both views draw into the same card-body space so the live
// spectrum behind them lines up. Bands are held optimistically; native's
// resyncs are ignored mid-drag so a stale snapshot can't fight the pointer.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "model/ChainState.h"
#include "services/Services.h"
#include "services/SpectrumFeed.h"

namespace t3k::ui {

class BlockEqView : public juce::Component {
public:
  enum class View { sliders, graph };

  BlockEqView(Services& services, std::string blockId);
  ~BlockEqView() override;

  // Native's bands (skipped while a drag is in flight).
  void setBands(const std::vector<EqBand>& bands);
  // Bypassed EQ: curve/dots/faders dim and go inert.
  void setEqEnabled(bool enabled);
  void setView(View view);
  void setSampleRate(double sampleRate);

  void paint(juce::Graphics& g) override;
  void resized() override;

  // Shared by the two editors.
  class Graph;
  class Sliders;
  const std::vector<EqBand>& bands() const { return bands_; }
  bool eqEnabled() const { return enabled_; }
  double sampleRate() const { return sampleRate_; }
  const std::vector<float>& spectrum() const { return feed_.bins(); }
  Services& services() { return services_; }
  // Optimistic write-through: patch a band locally and fire it at native.
  void updateBand(int index, const EqBand& band);
  void setDragging(bool dragging) { dragging_ = dragging; }
  // Neighbour-clamped frequency range for band `index` (bands keep their
  // left-to-right order, with a hair of margin so dots never overlap).
  std::pair<double, double> freqRange(int index) const;

private:
  Services& services_;
  std::string blockId_;
  std::vector<EqBand> bands_;
  bool enabled_ = true;
  bool dragging_ = false;
  double sampleRate_ = 48000;
  View view_ = View::sliders;
  SpectrumFeed feed_;
  std::unique_ptr<juce::Component> grid_, spectrum_;  // cached grid, live spectrum
  std::unique_ptr<Graph> graph_;
  std::unique_ptr<Sliders> sliders_;
};

}  // namespace t3k::ui
