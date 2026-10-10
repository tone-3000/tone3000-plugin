#include "MeterStore.h"

#include <cmath>

namespace t3k::ui {

namespace {
// Quantize to 0.5 dB so imperceptible changes don't repaint.
constexpr float kQuantize = 2;
// CPU: EMA every poll (~0.5 s time constant), publish a rounded % slowly so
// the digit doesn't flicker.
constexpr juce::int64 kCpuUiIntervalMs = 400;
constexpr float kCpuEmaAlpha = 0.15f;

// The main meters' mono and L/R ids are three views of one signal and which
// is on screen changes with stereo mode, so their clip latches clear together.
juce::StringArray clipGroup(const juce::String& id) {
  const auto base = id.upToFirstOccurrenceOf(":", false, false);
  if (base == "input" || base == "output") return {base, base + ":l", base + ":r"};
  return {id};
}
}  // namespace

juce::String MeterStore::mainId(bool input, Channel channel) {
  const juce::String base = input ? "input" : "output";
  switch (channel) {
    case Channel::left: return base + ":l";
    case Channel::right: return base + ":r";
    case Channel::both: break;
  }
  return base;
}
juce::String MeterStore::blockInId(const std::string& blockId) {
  return "block:" + juce::String(blockId) + ":in";
}
juce::String MeterStore::blockOutId(const std::string& blockId) {
  return "block:" + juce::String(blockId) + ":out";
}

MeterStore::MeterStore(Backend& backend, UiClock& clock) : backend_(backend), clock_(clock) {}
MeterStore::~MeterStore() { clock_.removeListener(this); }

float MeterStore::level(const juce::String& id) const {
  auto it = levels_.find(id);
  return it == levels_.end() ? kFloorDb : it->second;
}

void MeterStore::clearClip(const juce::String& id) {
  for (const auto& groupId : clipGroup(id)) {
    if (clips_.erase(groupId) == 0) continue;
    listeners.call([&](Listener& l) { l.meterChanged(groupId); });
  }
}

// Polls only while something listens; ListenerList::add is idempotent, so
// every addListener can just make sure we are on the clock.
void MeterStore::addListener(Listener* l) {
  listeners.add(l);
  clock_.addListener(this);
}

void MeterStore::removeListener(Listener* l) {
  listeners.remove(l);
  if (listeners.isEmpty()) clock_.removeListener(this);
}

void MeterStore::tick() {
  const auto res = backend_.getMeterLevels();
  if (res.isObject()) apply(MeterLevels::parse(res));
}

void MeterStore::apply(const MeterLevels& m) {
  auto applyMain = [this](const char* type, const float pair[2]) {
    update(juce::String(type) + ":l", pair[0]);
    update(juce::String(type) + ":r", pair[1]);
    update(type, std::max(pair[0], pair[1]));
  };
  applyMain("input", m.input);
  applyMain("output", m.output);
  for (const auto& b : m.blocks) {
    update(blockInId(b.id), b.in);
    update(blockOutId(b.id), b.out);
  }
  applyCpu(m.cpu);
  applyCorrelation(m.correlation);
}

void MeterStore::update(const juce::String& id, float raw) {
  const bool finite = std::isfinite(raw);
  // Latch clips on the raw value so a one-frame overshoot can't slip past
  // the quantized short-circuit below.
  const bool clipped = finite && raw >= kClipDb && clips_.insert(id).second;
  const float value = finite ? std::round(std::max(kFloorDb, raw) * kQuantize) / kQuantize : kFloorDb;
  auto it = levels_.find(id);
  if (it != levels_.end() && juce::exactlyEqual(it->second, value) && !clipped) return;
  levels_[id] = value;
  listeners.call([&](Listener& l) { l.meterChanged(id); });
}

void MeterStore::applyCpu(float raw) {
  const float sample = std::isfinite(raw) ? std::max(0.0f, raw) : 0.0f;
  if (!cpuSeeded_) {
    cpuEma_ = sample;
    cpuSeeded_ = true;
  } else {
    cpuEma_ += (sample - cpuEma_) * kCpuEmaAlpha;
  }
  const auto now = juce::Time::currentTimeMillis();
  if (now - lastCpuPublishMs_ < kCpuUiIntervalMs) return;
  lastCpuPublishMs_ = now;
  const float percent = std::round(cpuEma_ * 1000.0f) / 10.0f;
  if (juce::exactlyEqual(cpuPercent_, percent)) return;
  cpuPercent_ = percent;
  listeners.call([](Listener& l) { l.cpuChanged(); });
}

void MeterStore::applyCorrelation(float raw) {
  const float value =
      std::isfinite(raw) ? std::round(juce::jlimit(-1.0f, 1.0f, raw) * 20.0f) / 20.0f : 1.0f;
  if (juce::exactlyEqual(correlation_, value)) return;
  correlation_ = value;
  listeners.call([](Listener& l) { l.correlationChanged(); });
}

}  // namespace t3k::ui
