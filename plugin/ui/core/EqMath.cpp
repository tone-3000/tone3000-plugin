#include "EqMath.h"

#include <cmath>
#include <map>
#include <string>

namespace t3k::ui::eq {

namespace {

struct Biquad {
  double b0, b1, b2, a1, a2;
};

Biquad coeffs(const EqBand& band, double sampleRate) {
  const double freq = juce::jlimit(kEqMinFreqHz, std::min(kEqMaxFreqHz, sampleRate * 0.49), band.freqHz);
  const double A = std::pow(10.0, band.gainDb / 40.0);
  const double omega = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
  const double sn = std::sin(omega), cs = std::cos(omega);
  const double alpha = sn / (2.0 * band.q);
  const double sqrtA = std::sqrt(A);

  double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
  switch (band.type) {
    case EqBandType::lowcut:  // highpass
      b0 = (1 + cs) * 0.5;
      b1 = -(1 + cs);
      b2 = (1 + cs) * 0.5;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    case EqBandType::highcut:  // lowpass
      b0 = (1 - cs) * 0.5;
      b1 = 1 - cs;
      b2 = (1 - cs) * 0.5;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    case EqBandType::bell:
      b0 = 1 + alpha * A;
      b1 = -2 * cs;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cs;
      a2 = 1 - alpha / A;
      break;
    case EqBandType::lowshelf:
      b0 = A * (A + 1 - (A - 1) * cs + 2 * sqrtA * alpha);
      b1 = 2 * A * (A - 1 - (A + 1) * cs);
      b2 = A * (A + 1 - (A - 1) * cs - 2 * sqrtA * alpha);
      a0 = A + 1 + (A - 1) * cs + 2 * sqrtA * alpha;
      a1 = -2 * (A - 1 + (A + 1) * cs);
      a2 = A + 1 + (A - 1) * cs - 2 * sqrtA * alpha;
      break;
    case EqBandType::highshelf:
      b0 = A * (A + 1 + (A - 1) * cs + 2 * sqrtA * alpha);
      b1 = -2 * A * (A - 1 + (A + 1) * cs);
      b2 = A * (A + 1 + (A - 1) * cs - 2 * sqrtA * alpha);
      a0 = A + 1 - (A - 1) * cs + 2 * sqrtA * alpha;
      a1 = 2 * (A - 1 - (A + 1) * cs);
      a2 = A + 1 - (A - 1) * cs - 2 * sqrtA * alpha;
      break;
  }
  return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// |H(e^jω)| in dB of a normalised biquad at one frequency.
double magnitudeDb(const Biquad& c, double freqHz, double sampleRate) {
  const double omega = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
  const double cosW = std::cos(omega), cos2W = std::cos(2 * omega);
  const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 + 2 * (c.b0 * c.b1 + c.b1 * c.b2) * cosW +
                     2 * c.b0 * c.b2 * cos2W;
  const double den = 1 + c.a1 * c.a1 + c.a2 * c.a2 + 2 * (c.a1 + c.a1 * c.a2) * cosW + 2 * c.a2 * cos2W;
  const double magSq = num / std::max(den, 1e-24);
  return 10.0 * std::log10(std::max(magSq, 1e-24));
}

const double kLogMin = std::log(kEqMinFreqHz);
const double kLogMax = std::log(kEqMaxFreqHz);

}  // namespace

std::vector<double> responseDb(const std::vector<EqBand>& bands, double sampleRate,
                               const std::vector<double>& freqsHz) {
  std::vector<Biquad> active;
  for (const auto& band : bands)
    if (band.isActive()) active.push_back(coeffs(band, sampleRate));
  std::vector<double> out(freqsHz.size(), 0.0);
  for (size_t i = 0; i < freqsHz.size(); ++i)
    for (const auto& c : active) out[i] += magnitudeDb(c, freqsHz[i], sampleRate);
  return out;
}

double freqToNorm(double freqHz) {
  return (std::log(juce::jlimit(kEqMinFreqHz, kEqMaxFreqHz, freqHz)) - kLogMin) / (kLogMax - kLogMin);
}

double normToFreq(double norm) {
  return std::exp(kLogMin + juce::jlimit(0.0, 1.0, norm) * (kLogMax - kLogMin));
}

juce::String formatFreq(double freqHz) {
  if (freqHz >= 10000) return juce::String(freqHz / 1000.0, 1) + "k";
  if (freqHz >= 1000) return juce::String(freqHz / 1000.0, 2) + "k";
  return juce::String(juce::roundToInt(freqHz)) + " Hz";
}

std::optional<double> parseFreq(const juce::String& raw) {
  auto cleaned = raw.trim().toLowerCase().replaceCharacter(',', '.');
  if (cleaned.endsWith("hz")) cleaned = cleaned.dropLastCharacters(2).trim();
  const bool hasK = cleaned.containsChar('k');
  const auto number = cleaned.removeCharacters("k").trim();
  if (number.isEmpty() || !number.containsOnly("0123456789.-+")) return std::nullopt;
  const double value = number.getDoubleValue();
  if (!std::isfinite(value)) return std::nullopt;
  return hasK ? value * 1000.0 : value;
}

std::optional<double> parseDecimal(const juce::String& raw) {
  const auto cleaned = raw.trim().replaceCharacter(',', '.');
  if (cleaned.isEmpty() || !cleaned.containsOnly("0123456789.-+")) return std::nullopt;
  const double value = cleaned.getDoubleValue();
  return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

bool hasGain(EqBandType type) {
  return type == EqBandType::bell || type == EqBandType::lowshelf || type == EqBandType::highshelf;
}

juce::String typeLabel(EqBandType type) {
  switch (type) {
    case EqBandType::lowcut: return "Low Cut";
    case EqBandType::lowshelf: return "Low Shelf";
    case EqBandType::bell: return "Bell";
    case EqBandType::highshelf: return "High Shelf";
    case EqBandType::highcut: return "High Cut";
  }
  return {};
}

const char* typeGlyphPath(EqBandType type) {
  switch (type) {
    case EqBandType::lowshelf: return "M1 11 C5 11 6 3 10 3 L15 3";
    case EqBandType::bell: return "M1 11 C4 11 5 3 8 3 C11 3 12 11 15 11";
    case EqBandType::highshelf: return "M1 3 C5 3 6 11 10 11 L15 11";
    case EqBandType::lowcut: return "M1 13 C4 13 5 3 9 3 L15 3";
    case EqBandType::highcut: return "M1 3 L7 3 C11 3 12 13 15 13";
  }
  return "";
}

const char* typeGlyphSvg(EqBandType type) {
  static const std::map<EqBandType, std::string> table = [] {
    std::map<EqBandType, std::string> t;
    for (auto kind : {EqBandType::lowcut, EqBandType::lowshelf, EqBandType::bell, EqBandType::highshelf,
                      EqBandType::highcut})
      t[kind] = std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 14\" fill=\"none\" "
                            "stroke=\"#ffffff\" stroke-width=\"1.6\" stroke-linecap=\"round\"><path d=\"") +
               typeGlyphPath(kind) + "\"/></svg>";
    return t;
  }();
  return table.at(type).c_str();
}

std::optional<double> typeDefaultQ(EqBandType type) {
  return type == EqBandType::bell ? std::nullopt : std::optional<double>(0.71);
}

}  // namespace t3k::ui::eq
