// Meter transport (port of useMeters.tsx): ONE `getMeterLevels` pull per
// UiClock tick feeds every meter in the plugin (global input/output +
// per-block in/out), plus the CPU readout and the spread correlation that
// ride the same payload. Values are quantized so steady signals cause no
// repaints, and the poll runs only while something listens.
#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <set>

#include "UiClock.h"
#include "backend/Backend.h"
#include "model/ChainState.h"

namespace t3k::ui {

class MeterStore : private UiClock::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    // A level or clip latch for `id` changed.
    virtual void meterChanged(const juce::String& /*id*/) {}
    virtual void cpuChanged() {}
    virtual void correlationChanged() {}
  };

  static constexpr float kFloorDb = -60;
  // Levels at/above this latch the clip indicator until cleared.
  static constexpr float kClipDb = 0;

  // Meter ids: "input" / "output" (max of both channels), "input:l",
  // "output:r", …, and per block "block:<id>:in" / "block:<id>:out".
  enum class Channel { both, left, right };
  static juce::String mainId(bool input, Channel channel = Channel::both);
  static juce::String blockInId(const std::string& blockId);
  static juce::String blockOutId(const std::string& blockId);

  MeterStore(Backend& backend, UiClock& clock);
  ~MeterStore() override;

  float level(const juce::String& id) const;
  bool clipped(const juce::String& id) const { return clips_.count(id) > 0; }
  // Clears the id's latch and its channel siblings (mono/L/R are one signal).
  void clearClip(const juce::String& id);
  // Audio-callback load as a percent (one decimal), EMA smoothed, ~2.5 Hz.
  float cpuPercent() const { return cpuPercent_; }
  // Spread output correlation, -1..1 (1 when idle), 0.05 steps.
  float correlation() const { return correlation_; }

  void addListener(Listener* l);
  void removeListener(Listener* l);

private:
  void tick() override;
  void apply(const MeterLevels& levels);
  void update(const juce::String& id, float raw);
  void applyCpu(float raw);
  void applyCorrelation(float raw);

  Backend& backend_;
  UiClock& clock_;
  std::map<juce::String, float> levels_;
  std::set<juce::String> clips_;
  float cpuPercent_ = 0, cpuEma_ = 0;
  bool cpuSeeded_ = false;
  juce::int64 lastCpuPublishMs_ = 0;
  float correlation_ = 1;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
