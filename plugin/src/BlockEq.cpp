#include "BlockEq.h"
#include <cmath>

std::array<BlockEq::Band, BlockEq::kNumBands> BlockEq::defaultBands() {
  return {{
      {BandType::LowShelf, 100.0f, 0.0f, 0.71f},
      {BandType::Bell, 250.0f, 0.0f, 1.0f},
      {BandType::Bell, 650.0f, 0.0f, 1.0f},
      {BandType::Bell, 1600.0f, 0.0f, 1.0f},
      {BandType::Bell, 3500.0f, 0.0f, 1.4f},
      {BandType::HighShelf, 8000.0f, 0.0f, 0.71f},
  }};
}

BlockEq::BandType BlockEq::coerceTypeForBand(int index, BandType type) {
  if (index == 0)
    return (type == BandType::LowCut || type == BandType::LowShelf) ? type : BandType::LowShelf;
  if (index == kNumBands - 1)
    return (type == BandType::HighCut || type == BandType::HighShelf) ? type : BandType::HighShelf;
  return BandType::Bell;
}

BlockEq::BlockEq() : bands(defaultBands()) {
  updateActivity();
}

void BlockEq::prepare(double newSampleRate) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  for (int i = 0; i < kNumBands; ++i) {
    updateBand(i);
    filters[static_cast<size_t>(i)].resetState();
  }
  updateActivity();
}

bool BlockEq::setBand(int index, const Band& band) {
  if (index < 0 || index >= kNumBands)
    return false;
  Band clamped = clampBand(band);
  clamped.type = coerceTypeForBand(index, clamped.type);
  bands[static_cast<size_t>(index)] = clamped;
  updateBand(index);

  const bool wasActive = anyBandActive;
  updateActivity();
  // Coming back from the flat-skip path: filter state is stale, clear it so
  // the first processed block doesn't ring with old history.
  if (!wasActive && anyBandActive)
    for (auto& f : filters)
      f.resetState();
  return true;
}

bool BlockEq::setBandFromVar(int index, const juce::var& bandVar) {
  if (!bandVar.isObject())
    return false;
  Band band;
  band.type = bandTypeFromString(bandVar.getProperty("type", "bell").toString());
  band.freqHz = static_cast<float>(static_cast<double>(bandVar.getProperty("freqHz", 1000.0)));
  band.gainDb = static_cast<float>(static_cast<double>(bandVar.getProperty("gainDb", 0.0)));
  band.q = static_cast<float>(static_cast<double>(bandVar.getProperty("q", 1.0)));
  return setBand(index, band);
}

void BlockEq::resetToDefault() {
  bands = defaultBands();
  enabled = true;
  for (int i = 0; i < kNumBands; ++i) {
    updateBand(i);
    filters[static_cast<size_t>(i)].resetState();
  }
  updateActivity();
}

void BlockEq::setEnabled(bool shouldBeEnabled) {
  // Re-engaging after a bypass: filter state is stale, clear it so the first
  // processed block doesn't ring with old history.
  if (!enabled && shouldBeEnabled)
    for (auto& f : filters)
      f.resetState();
  enabled = shouldBeEnabled;
}

void BlockEq::process(juce::AudioBuffer<float>& buffer) {
  const int numSamples = buffer.getNumSamples();
  const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

  for (int b = 0; b < kNumBands; ++b) {
    if (!bandActive[static_cast<size_t>(b)])
      continue;
    auto& filter = filters[static_cast<size_t>(b)];
    for (int ch = 0; ch < numChannels; ++ch) {
      auto* data = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i)
        data[i] = filter.processSample(data[i], ch);
    }
  }
}

bool BlockEq::isBandActive(const Band& band) {
  switch (band.type) {
    case BandType::Bell:
    case BandType::LowShelf:
    case BandType::HighShelf:
      return std::abs(band.gainDb) >= 0.05f;
    // Cuts shape the signal regardless of gain.
    case BandType::LowCut:
    case BandType::HighCut:
      return true;
  }
  return false;
}

BlockEq::Band BlockEq::clampBand(Band band) {
  band.freqHz = juce::jlimit(kMinFreqHz, kMaxFreqHz, band.freqHz);
  band.gainDb = juce::jlimit(-kMaxAbsGainDb, kMaxAbsGainDb, band.gainDb);
  band.q = juce::jlimit(kMinQ, kMaxQ, band.q);
  return band;
}

// RBJ Audio EQ Cookbook coefficients, A = 10^(dB/40). Keep in exact sync with
// the TypeScript mirror in ui/src/components/eqMath.ts.
void BlockEq::updateBand(int index) {
  const Band& band = bands[static_cast<size_t>(index)];
  Biquad& f = filters[static_cast<size_t>(index)];
  bandActive[static_cast<size_t>(index)] = isBandActive(band);

  const double freq = juce::jlimit(static_cast<double>(kMinFreqHz),
                                   juce::jmin(static_cast<double>(kMaxFreqHz), sampleRate * 0.49),
                                   static_cast<double>(band.freqHz));
  const double A = std::pow(10.0, band.gainDb / 40.0);
  const double omega = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
  const double sn = std::sin(omega);
  const double cs = std::cos(omega);
  const double alpha = sn / (2.0 * band.q);
  const double sqrtA = std::sqrt(A);

  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

  switch (band.type) {
    case BandType::LowCut:  // highpass
      b0 = (1.0 + cs) * 0.5;
      b1 = -(1.0 + cs);
      b2 = (1.0 + cs) * 0.5;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cs;
      a2 = 1.0 - alpha;
      break;
    case BandType::HighCut:  // lowpass
      b0 = (1.0 - cs) * 0.5;
      b1 = 1.0 - cs;
      b2 = (1.0 - cs) * 0.5;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cs;
      a2 = 1.0 - alpha;
      break;
    case BandType::Bell:
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cs;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cs;
      a2 = 1.0 - alpha / A;
      break;
    case BandType::LowShelf:
      b0 = A * ((A + 1.0) - (A - 1.0) * cs + 2.0 * sqrtA * alpha);
      b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
      b2 = A * ((A + 1.0) - (A - 1.0) * cs - 2.0 * sqrtA * alpha);
      a0 = (A + 1.0) + (A - 1.0) * cs + 2.0 * sqrtA * alpha;
      a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
      a2 = (A + 1.0) + (A - 1.0) * cs - 2.0 * sqrtA * alpha;
      break;
    case BandType::HighShelf:
      b0 = A * ((A + 1.0) + (A - 1.0) * cs + 2.0 * sqrtA * alpha);
      b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
      b2 = A * ((A + 1.0) + (A - 1.0) * cs - 2.0 * sqrtA * alpha);
      a0 = (A + 1.0) - (A - 1.0) * cs + 2.0 * sqrtA * alpha;
      a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
      a2 = (A + 1.0) - (A - 1.0) * cs - 2.0 * sqrtA * alpha;
      break;
  }

  const double norm = 1.0 / a0;
  f.b0 = static_cast<float>(b0 * norm);
  f.b1 = static_cast<float>(b1 * norm);
  f.b2 = static_cast<float>(b2 * norm);
  f.a1 = static_cast<float>(a1 * norm);
  f.a2 = static_cast<float>(a2 * norm);
}

void BlockEq::updateActivity() {
  anyBandActive = false;
  for (bool active : bandActive)
    anyBandActive = anyBandActive || active;
}

juce::var BlockEq::toVar() const {
  juce::Array<juce::var> bandArray;
  for (const auto& band : bands) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", bandTypeToString(band.type));
    obj->setProperty("freqHz", static_cast<double>(band.freqHz));
    obj->setProperty("gainDb", static_cast<double>(band.gainDb));
    obj->setProperty("q", static_cast<double>(band.q));
    bandArray.add(juce::var(obj));
  }
  auto* eq = new juce::DynamicObject();
  eq->setProperty("enabled", enabled);
  eq->setProperty("bands", bandArray);
  return juce::var(eq);
}

juce::ValueTree BlockEq::toValueTree() const {
  juce::ValueTree tree("Eq");
  tree.setProperty("enabled", enabled, nullptr);
  for (const auto& band : bands) {
    juce::ValueTree bandTree("Band");
    bandTree.setProperty("type", bandTypeToString(band.type), nullptr);
    bandTree.setProperty("freqHz", static_cast<double>(band.freqHz), nullptr);
    bandTree.setProperty("gainDb", static_cast<double>(band.gainDb), nullptr);
    bandTree.setProperty("q", static_cast<double>(band.q), nullptr);
    tree.appendChild(bandTree, nullptr);
  }
  return tree;
}

void BlockEq::restoreFromValueTree(const juce::ValueTree& tree) {
  bands = defaultBands();
  enabled = true;
  if (tree.isValid() && tree.hasType("Eq")) {
    enabled = static_cast<bool>(tree.getProperty("enabled", true));
    const int count = juce::jmin(tree.getNumChildren(), kNumBands);
    for (int i = 0; i < count; ++i) {
      const auto bandTree = tree.getChild(i);
      Band band;
      band.type = coerceTypeForBand(
          i, bandTypeFromString(bandTree.getProperty("type", "bell").toString()));
      band.freqHz = static_cast<float>(static_cast<double>(bandTree.getProperty("freqHz", 1000.0)));
      band.gainDb = static_cast<float>(static_cast<double>(bandTree.getProperty("gainDb", 0.0)));
      band.q = static_cast<float>(static_cast<double>(bandTree.getProperty("q", 1.0)));
      bands[static_cast<size_t>(i)] = clampBand(band);
    }
  }
  for (int i = 0; i < kNumBands; ++i) {
    updateBand(i);
    filters[static_cast<size_t>(i)].resetState();
  }
  updateActivity();
}

juce::String BlockEq::bandTypeToString(BandType type) {
  switch (type) {
    case BandType::LowCut: return "lowcut";
    case BandType::LowShelf: return "lowshelf";
    case BandType::Bell: return "bell";
    case BandType::HighShelf: return "highshelf";
    case BandType::HighCut: return "highcut";
  }
  return "bell";
}

BlockEq::BandType BlockEq::bandTypeFromString(const juce::String& s) {
  if (s == "lowcut") return BandType::LowCut;
  if (s == "lowshelf") return BandType::LowShelf;
  if (s == "highshelf") return BandType::HighShelf;
  if (s == "highcut") return BandType::HighCut;
  // Unknown/removed types (e.g. "notch" from older state) fall back to bell;
  // coerceTypeForBand then snaps it to the band's role.
  return BandType::Bell;
}
