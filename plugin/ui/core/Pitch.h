// Chromatic tuner math (TunerView.tsx): frequency → nearest note + cents
// offset, and how many bars a deflection lights.
#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace t3k::ui::pitch {

// Cents window considered "in tune" and the full deflection of one side.
inline constexpr float kInTuneCents = 5;
inline constexpr float kMaxCents = 50;
inline constexpr int kBarsPerSide = 6;

struct Note {
  juce::String name;  // "C" .. "B", sharps as "C♯"
  int octave = 0;
  float cents = 0;  // -50 .. 50 from the nearest semitone
};

inline Note fromFrequency(float hz) {
  static const char* const kNames[] = {"C", "C\xe2\x99\xaf", "D", "D\xe2\x99\xaf", "E",  "F",
                                       "F\xe2\x99\xaf", "G", "G\xe2\x99\xaf", "A", "A\xe2\x99\xaf", "B"};
  const double midi = 69 + 12 * std::log2(hz / 440.0);
  const int nearest = static_cast<int>(std::round(midi));
  Note note;
  note.name = juce::String::fromUTF8(kNames[((nearest % 12) + 12) % 12]);
  note.octave = static_cast<int>(std::floor(nearest / 12.0)) - 1;
  note.cents = static_cast<float>((midi - nearest) * 100);
  return note;
}

// Bars lit on the deflection side: blue only near in-tune, all six red at a
// half-semitone off.
inline int litCount(float absCents) {
  if (absCents <= kInTuneCents) return 1;
  const float t = std::min(1.0f, (absCents - kInTuneCents) / (kMaxCents - kInTuneCents));
  return std::min(kBarsPerSide, 1 + static_cast<int>(std::floor(t * 5 + 0.5f)));
}

}  // namespace t3k::ui::pitch
