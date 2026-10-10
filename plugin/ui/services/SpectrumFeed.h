// Live spectrum of the audio leaving one block (useBlockSpectrum.ts).
// Constructing a feed enables the native analyser for that block (the audio
// thread does zero analyser work otherwise); destroying it disables it
// again. Polls on the UiClock tick and notifies only when the bins moved by
// at least kQuantumDb, so an idle signal or analyser jitter costs no repaints.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "UiClock.h"
#include "backend/Backend.h"

namespace t3k::ui {

class SpectrumFeed : private UiClock::Listener {
public:
  // Mirrored from plugin/include/BlockSpectrum.h: kNumBins dB values
  // (kMinDb..0) log-spaced 20 Hz .. 20 kHz, the EQ graph's own axis.
  static constexpr int kNumBins = 64;
  static constexpr float kMinDb = -100;
  // Bins are rounded to this before the moved-or-not comparison.
  static constexpr float kQuantumDb = 0.25f;

  SpectrumFeed(Backend& backend, UiClock& clock, std::string blockId);
  ~SpectrumFeed() override;

  const std::vector<float>& bins() const { return bins_; }
  std::function<void()> onChange;

private:
  void tick() override;

  Backend& backend_;
  UiClock& clock_;
  std::string blockId_;
  std::vector<float> bins_, scratch_;  // scratch_: reused poll buffer
};

}  // namespace t3k::ui
