// Display scales for knobs (port of knobScale.ts). Every knob's value is
// normalised 0..1 (APVTS / chain params); a KnobScale maps that to real
// units for the drag readout and the double-click text entry, and back
// again when the user types a value.
#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>

#include "Labels.h"

namespace t3k::ui {

struct KnobScale {
  // Normalised -> real units (dB, %, ms, ...).
  std::function<double(double)> toDisplay;
  // Real units -> normalised. Caller clamps to the knob's min/max.
  std::function<double(double)> fromDisplay;
  // Full readout string, units included (e.g. "-3.2 dB").
  std::function<juce::String(double)> format;
  // Text-entry prefill (number only, no unit, which is easier to retype).
  std::function<juce::String(double)> editText;
};

namespace scales {

inline KnobScale make(std::function<double(double)> toDisplay,
                      std::function<double(double)> fromDisplay, juce::String unit, int decimals) {
  KnobScale s;
  s.toDisplay = toDisplay;
  s.fromDisplay = std::move(fromDisplay);
  s.format = [toDisplay, unit, decimals](double n) {
    return labels::toFixed(toDisplay(n), decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
  };
  s.editText = [toDisplay, decimals](double n) { return labels::toFixed(toDisplay(n), decimals); };
  return s;
}

// Straight-line map from normalised 0..1 to [min..max] display units.
inline KnobScale linear(double min, double max, juce::String unit = {}, int decimals = 1) {
  return make([=](double n) { return min + n * (max - min); },
              [=](double d) { return (d - min) / (max - min); }, std::move(unit), decimals);
}

// 0..1 -> 0..100 %. Default for knobs that don't declare a scale.
inline const KnobScale& percent() {
  static const KnobScale s = make([](double n) { return n * 100; }, [](double d) { return d / 100; },
                                  "%", 0);
  return s;
}

// Main / per-block gain: normalised 0.5 = unity, full range ±24 dB.
inline const KnobScale& gainDb() {
  static const KnobScale s = linear(-24, 24, "dB", 1);
  return s;
}

// Stereo balance trim: 0.5 = centred, ±12 dB per channel at the ends.
inline const KnobScale& balanceDb() {
  static const KnobScale s = linear(-12, 12, "dB", 1);
  return s;
}

// Gate threshold: normalised spans -100..0 dB.
inline const KnobScale& gateDb() {
  static const KnobScale s = linear(-100, 0, "dB", 0);
  return s;
}

// Gate deck. These mirror the APVTS ranges in Processor.cpp (the parameters
// are stored in real units): release is a log map over 5-500 ms so the
// tight end has resolution, hold and range are linear.
inline const KnobScale& gateReleaseMs() {
  static const KnobScale s = make([](double n) { return 5.0 * std::pow(100.0, n); },
                                  [](double d) { return std::log(d / 5.0) / std::log(100.0); },
                                  "ms", 0);
  return s;
}
inline const KnobScale& gateHoldMs() {
  static const KnobScale s = linear(0, 200, "ms", 0);
  return s;
}
// Depth of the closed gate as positive attenuation, so clockwise gates
// harder (80 dB is the full mute the gate shipped with).
inline const KnobScale& gateRangeDb() {
  static const KnobScale s = linear(20, 80, "dB", 0);
  return s;
}

// Transpose: bipolar whole semitones, centre = 0. Backs an AudioParameterInt
// (Processor.cpp), so the readout always rounds; the sign is spelled out so
// "+3 st" and "-3 st" can't be confused at a glance.
inline const KnobScale& semitones() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto st = [](double n) { return std::round(-12 + n * 24); };
    c.toDisplay = st;
    c.fromDisplay = [](double d) { return (d + 12) / 24; };
    c.format = [st](double n) {
      const int v = static_cast<int>(st(n));
      return (v > 0 ? "+" : "") + juce::String(v) + " st";
    };
    c.editText = [st](double n) { return juce::String(static_cast<int>(st(n))); };
    return c;
  }();
  return s;
}

// Transpose deck. Fine is a bipolar cent trim; the tonality limit rides a
// log map whose top end reads "Off" (a pure shift; the processor treats the
// end value the same way); the window is the three detents in Transpose.h,
// read out as the latency each adds.
inline const KnobScale& cents() {
  static const KnobScale s = [] {
    KnobScale c = linear(-50, 50, "ct", 0);
    c.format = [](double n) {
      const int v = juce::roundToInt(-50 + n * 100);
      return (v > 0 ? "+" : "") + juce::String(v) + " ct";
    };
    return c;
  }();
  return s;
}
inline const KnobScale& tonalityHz() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto hz = [](double n) { return 1000.0 * std::pow(20.0, n); };
    c.toDisplay = hz;
    c.fromDisplay = [](double d) { return std::log(d / 1000.0) / std::log(20.0); };
    c.format = [hz](double n) {
      if (n >= 1.0 - 1e-6) return juce::String("Off");
      return labels::toFixed(hz(n) / 1000.0, 1) + " kHz";
    };
    c.editText = [hz](double n) { return juce::String(juce::roundToInt(hz(n))); };
    return c;
  }();
  return s;
}
inline const KnobScale& windowMs() {
  static const KnobScale s = [] {
    KnobScale c;
    constexpr int kMs[] = {30, 60, 100};
    const auto index = [](double n) { return juce::jlimit(0, 2, juce::roundToInt(n * 2)); };
    c.toDisplay = [index](double n) { return kMs[index(n)]; };
    // Snaps typed values to the nearest detent.
    c.fromDisplay = [](double d) { return d < 45 ? 0.0 : d < 80 ? 0.5 : 1.0; };
    c.format = [index](double n) { return juce::String(kMs[index(n)]) + " ms"; };
    c.editText = [index](double n) { return juce::String(kMs[index(n)]); };
    return c;
  }();
  return s;
}

// Faceplate tone stack knobs: 0..10, 5 = flat.
inline const KnobScale& tone() {
  static const KnobScale s = linear(0, 10, {}, 1);
  return s;
}

// Bipolar one-sided delay: centre = 0 ms, ends reach ±maxMs. Display shows
// the magnitude plus the delayed side ("15.0 ms R").
inline KnobScale sidedMs(double maxMs) {
  const double span = 2 * maxMs;
  KnobScale s;
  s.toDisplay = [span](double n) { return (n - 0.5) * span; };
  s.fromDisplay = [span](double d) { return 0.5 + d / span; };
  s.format = [span](double n) {
    const double ms = (n - 0.5) * span;
    if (std::abs(ms) < 0.05) return juce::String("0 ms");
    return labels::toFixed(std::abs(ms), 1) + " ms " + (ms < 0 ? "L" : "R");
  };
  s.editText = [span](double n) { return labels::toFixed((n - 0.5) * span, 1); };
  return s;
}

// Offset (bipolar), shared by the mono-mode Spread lag and the stereo-mode
// Align delay: centre = 0 ms, ends reach 24 ms toward L or R.
inline const KnobScale& offsetMs() {
  static const KnobScale s = sidedMs(24);
  return s;
}

// Deck crossover cutoff: log map over 32.5-520 Hz, centre = 130 Hz.
inline const KnobScale& crossoverHz() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto hz = [](double n) { return 32.5 * std::pow(16.0, n); };
    c.toDisplay = hz;
    c.fromDisplay = [](double d) { return std::log(d / 32.5) / std::log(16.0); };
    c.format = [hz](double n) { return juce::String(juce::roundToInt(hz(n))) + " Hz"; };
    c.editText = [hz](double n) { return juce::String(juce::roundToInt(hz(n))); };
    return c;
  }();
  return s;
}

// Chain pan halves. The left knob covers normalised 0..0.5 (hard left ..
// centre), the right 0.5..1 (centre .. hard right). Display is the pan
// amount toward the side, 100 = hard, 0 = centre.
inline const KnobScale& pan(bool left) {
  static const auto make = [](bool l) {
    KnobScale s;
    const auto toDisplay = [l](double n) { return l ? (0.5 - n) * 200 : (n - 0.5) * 200; };
    s.toDisplay = toDisplay;
    s.fromDisplay = [l](double d) { return l ? 0.5 - d / 200 : 0.5 + d / 200; };
    s.format = [toDisplay, l](double n) {
      const int amount = juce::roundToInt(toDisplay(n));
      return amount == 0 ? juce::String("C") : juce::String(amount) + (l ? "L" : "R");
    };
    s.editText = [toDisplay](double n) { return juce::String(juce::roundToInt(toDisplay(n))); };
    return s;
  };
  static const KnobScale leftScale = make(true), rightScale = make(false);
  return left ? leftScale : rightScale;
}

}  // namespace scales
}  // namespace t3k::ui
