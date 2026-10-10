#include "MockSignal.h"

#include <algorithm>
#include <cmath>

namespace t3k::ui::testbed {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr float kNoiseFloorDb = -72;
constexpr double kDecaySeconds = 0.45;
constexpr double kAttackSeconds = 0.004;
// Gain each tone block adds along the path (drive, amp, cab, ...).
constexpr float kBlockGainDb[] = {6, 10, -8, 3};
constexpr int kBins = 64;

// Open strings and the notes around them, low E to high E.
constexpr double kScaleHz[] = {82.41, 98.00, 110.00, 123.47, 146.83, 164.81, 196.00,
                               220.00, 246.94, 293.66, 329.63, 392.00};

juce::var obj(std::initializer_list<std::pair<const char*, juce::var>> props) {
  auto* o = new juce::DynamicObject();
  for (const auto& [k, v] : props) o->setProperty(k, v);
  return juce::var(o);
}

float toDb(float linear) { return 20.0f * std::log10(std::max(linear, 1e-6f)); }
float toLinear(float db) { return std::pow(10.0f, db / 20.0f); }

}  // namespace

MockSignal::MockSignal() : startMs_(juce::Time::getMillisecondCounterHiRes()) {
  // A ~90 s loop of phrases: 4-10 notes 0.15-0.7 s apart, then a 1-2.5 s
  // rest so the meters fall all the way; every so often a hit at +0.5 dBFS
  // latches the clip LEDs.
  juce::Random r(20260923);
  double t = 0.5;
  int count = 0;
  while (t < 90.0) {
    const int phrase = 4 + r.nextInt(7);
    for (int i = 0; i < phrase; ++i) {
      const bool hot = ++count % 57 == 0;
      notes_.push_back({t, kScaleHz[r.nextInt(static_cast<int>(std::size(kScaleHz)))],
                        hot ? 0.5f : -15.0f + 9.0f * r.nextFloat(), -18.0f + 36.0f * r.nextFloat()});
      t += 0.15 + 0.55 * r.nextDouble();
    }
    t += 1.0 + 1.5 * r.nextDouble();
  }
  loopSeconds_ = t;
}

float MockSignal::jitter(float amount) { return (random_.nextFloat() * 2.0f - 1.0f) * amount; }

MockSignal::Now MockSignal::now() {
  const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startMs_) / 1000.0;
  const double t = std::fmod(elapsed, loopSeconds_);
  auto it = std::upper_bound(notes_.begin(), notes_.end(), t, [](double x, const Note& n) { return x < n.start; });
  if (it == notes_.begin()) return {0, nullptr, 0, kNoiseFloorDb + jitter(1)};
  const auto& note = *std::prev(it);
  const double since = t - note.start;
  const float env = static_cast<float>(std::min(1.0, since / kAttackSeconds) * std::exp(-since / kDecaySeconds));
  const float linear = toLinear(note.peakDb) * env + toLinear(kNoiseFloorDb);
  // A peak meter reading a real string flickers by a fraction of a dB.
  return {since, &note, env, toDb(linear) + jitter(0.6f)};
}

juce::var MockSignal::meters(const juce::var& chain) {
  const auto s = now();
  const bool stereo = static_cast<bool>(chain.getProperty("stereoEnabled", false));
  const float inL = s.levelDb;
  const float inR = s.levelDb - 1.2f + jitter(0.5f);
  const bool hot = s.note != nullptr && s.note->peakDb > 0 && s.t < 0.05;

  auto* blocks = new juce::DynamicObject();
  float outL = inL, outR = inR;
  for (const char* lane : {"chain", "chainRight"}) {
    const auto* items = chain[lane].getArray();
    if (items == nullptr) continue;
    float level = juce::String(lane) == "chain" ? inL : inR;
    size_t k = 0;
    for (const auto& item : *items) {
      if (item["kind"].toString() != "tone") continue;
      const float in = level;
      float out = in + kBlockGainDb[k++ % std::size(kBlockGainDb)] + jitter(0.4f);
      if (!hot) out = std::min(out, -1.0f);
      blocks->setProperty(item["blockId"].toString(), obj({{"in", in}, {"out", out}}));
      level = out;
    }
    (juce::String(lane) == "chain" ? outL : outR) = level;
  }
  if (!stereo) outR = outL - 0.8f + jitter(0.4f);
  const auto t = (juce::Time::getMillisecondCounterHiRes() - startMs_) / 1000.0;
  return obj({{"input", juce::Array<juce::var>{inL, inR}},
              {"output", juce::Array<juce::var>{std::min(outL - 3.0f, -0.5f), std::min(outR - 3.0f, -0.5f)}},
              {"blocks", juce::var(blocks)},
              {"cpu", 0.10 + 0.02 * std::sin(t * 0.7) + 0.05 * s.env + jitter(0.004f)},
              {"correlation", stereo ? 0.72 + 0.2 * std::sin(t * 0.37) + jitter(0.03f) : 1.0}});
}

juce::var MockSignal::spectrum(const std::string&) {
  const auto s = now();
  const double f0 = s.note != nullptr ? s.note->hz : 110.0;
  // Harmonics of the sounding note (falling ~1/h, the upper ones dying
  // faster), a pink-ish floor, and a cab's rolloff above 4.5 kHz.
  const float upper = s.env * s.env;
  juce::Array<juce::var> bins;
  bins.ensureStorageAllocated(kBins);
  for (int i = 0; i < kBins; ++i) {
    const double f = 20.0 * std::pow(1000.0, i / double(kBins - 1));
    double energy = 0.004 * std::sqrt(110.0 / f);
    for (int h = 1; h <= 14; ++h) {
      const double octaves = std::log2(f / (h * f0)) / 0.08;
      const double weight = (h <= 3 ? 1.0 : upper) / std::pow(h, 1.1);
      energy += weight * std::exp(-0.5 * octaves * octaves);
    }
    double db = s.levelDb + 6.0 + 10.0 * std::log10(energy);
    if (f > 4500) db -= 18.0 * std::log2(f / 4500);
    bins.add(juce::jlimit(-100.0, -3.0, db + jitter(1.5f)));
  }
  return bins;
}

juce::var MockSignal::tuner() {
  const auto s = now();
  if (s.note == nullptr || s.env < 0.04f) return obj({{"frequency", 0}, {"confidence", 0.1}, {"level", s.levelDb}});
  // The pitch settles from a pluck's sharp attack onto the player's offset,
  // with a slow vibrato and reading noise on top.
  const double cents = s.note->centsOff + 12.0 * std::exp(-s.t / 0.08) + 3.0 * std::sin(kTwoPi * 5.0 * s.t) + jitter(0.6f);
  return obj({{"frequency", s.note->hz * std::pow(2.0, cents / 1200.0)},
              {"confidence", 0.95},
              {"level", s.levelDb}});
}

juce::var MockSignal::inputLevels(int channels) {
  const auto s = now();
  juce::Array<juce::var> levels;
  for (int i = 0; i < channels; ++i) levels.add(s.levelDb - 1.2f * static_cast<float>(i % 2) + jitter(0.5f));
  return levels;
}

}  // namespace t3k::ui::testbed
