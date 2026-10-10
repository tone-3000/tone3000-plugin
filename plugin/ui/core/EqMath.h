// EQ editor math (ports of eqMath.ts and the numeric half of eqShared.ts):
// the RBJ biquad response the DSP runs (plugin/src/BlockEq.cpp, A =
// 10^(dB/40)) so the drawn curve is the audio truth, the log frequency axis,
// the gain axis of the shared graph space, and the readout formats.
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "model/ChainState.h"

namespace t3k::ui::eq {

// Combined magnitude response (dB) of the active bands at each frequency.
// Inert bands are skipped, matching the audio thread.
std::vector<double> responseDb(const std::vector<EqBand>& bands, double sampleRate,
                               const std::vector<double>& freqsHz);

// Frequency ↔ 0..1 position on the log-scaled x axis (20 Hz .. 20 kHz).
double freqToNorm(double freqHz);
double normToFreq(double norm);

// "251 Hz" / "1.60k" / "12.5k".
juce::String formatFreq(double freqHz);
// Typed frequency: plain Hz ("800") or k-notation ("1.2k"); nullopt when
// unparsable.
std::optional<double> parseFreq(const juce::String& raw);
// Typed decimal ("," accepted as the separator).
std::optional<double> parseDecimal(const juce::String& raw);

bool hasGain(EqBandType type);
juce::String typeLabel(EqBandType type);
// 16x14 curve glyph for the type selector / band labels: the bare path,
// and a white-stroked SVG (stroke 1.6, round caps) for Icons::draw. Both
// point at static storage so the icon cache can key on the pointer.
const char* typeGlyphPath(EqBandType type);
const char* typeGlyphSvg(EqBandType type);
// Q applied when switching to `type` (cuts and shelves reset to 0.71; bells
// keep theirs).
std::optional<double> typeDefaultQ(EqBandType type);

// Shared graph space (eqShared.ts)
// Full card body: outer card minus 1px border each side and the chrome
// header. Both EQ views draw into this space so the spectrum lines up.
inline constexpr int kGraphW = 800 - 2;
// The body is laid out 275 tall (the card's border hides its last 2px).
// Graph space is 273 tall: the web draws grid, curve, dots and spectrum in
// an SVG with that viewBox and stretches it over the whole body
// (preserveAspectRatio none), so graph y times kSvgStretch is body y.
inline constexpr int kBodyH = 275;
// eqShared.ts GRAPH_H: the card minus its 2px border and the 45px header,
// which is the body less the 2px hidden under the bottom border.
inline constexpr int kGraphH = kBodyH - 2;
inline constexpr float kSvgStretch = static_cast<float>(kBodyH) / kGraphH;
inline constexpr int kGraphPadY = 12;  // keeps dots inside the frame at ±15 dB

inline double gainToY(double gainDb) {
  return kGraphH / 2.0 - (gainDb / kEqMaxAbsGainDb) * (kGraphH / 2.0 - kGraphPadY);
}
inline double yToGain(double y) {
  return ((kGraphH / 2.0 - y) / (kGraphH / 2.0 - kGraphPadY)) * kEqMaxAbsGainDb;
}

}  // namespace t3k::ui::eq
