#include "Fonts.h"

#include "UiBinaryData.h"

namespace t3k::ui {

namespace {

// Typefaces are heavyweight and immutable: build each once per cache
// lifetime (see Fonts::Cache for why that is not "per process").
juce::Typeface::Ptr embedded(const char* data, int size) {
  return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
}

juce::Typeface::Ptr& lazily(juce::Typeface::Ptr& slot, const char* data, int size) {
  if (slot == nullptr) slot = embedded(data, size);
  return slot;
}

// Arial is the web UI's body face and is installed on macOS, Windows and
// iOS. JUCE on Linux matches family names against the font files it scans,
// not fontconfig's aliases, so asking for Arial there gets a null typeface
// (height 0, NaN ascent: text laid out at NaN positions, which the software
// renderer turns into out-of-bounds writes). Where Arial is missing the
// embedded Arimo stands in: same metrics, so line boxes and wrapping match.
bool haveArial() {
  static const bool have =
      juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::plain)).getTypefacePtr() != nullptr;
  return have;
}

juce::Typeface::Ptr arimo(Fonts::Cache& cache, bool bold, bool italic) {
  struct Face { const char* data; int size; };
  static const Face kFaces[] = {
      {UiBinaryData::ArimoRegular_ttf, UiBinaryData::ArimoRegular_ttfSize},
      {UiBinaryData::ArimoBold_ttf, UiBinaryData::ArimoBold_ttfSize},
      {UiBinaryData::ArimoItalic_ttf, UiBinaryData::ArimoItalic_ttfSize},
      {UiBinaryData::ArimoBoldItalic_ttf, UiBinaryData::ArimoBoldItalic_ttfSize},
  };
  const int i = (bold ? 1 : 0) + (italic ? 2 : 0);
  return lazily(cache.arimo[i], kFaces[i].data, kFaces[i].size);
}

juce::Typeface::Ptr robotoMono(Fonts::Cache& cache, bool bold) {
  return bold ? lazily(cache.robotoMono[1], UiBinaryData::RobotoMonoBold_ttf,
                       UiBinaryData::RobotoMonoBold_ttfSize)
              : lazily(cache.robotoMono[0], UiBinaryData::RobotoMonoRegular_ttf,
                       UiBinaryData::RobotoMonoRegular_ttfSize);
}

}  // namespace

juce::Font Fonts::sans(float px, bool bold, bool italic) {
  const int style = (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0);
  if (haveArial()) return juce::Font(juce::FontOptions("Arial", px, style).withPointHeight(px));
  Hold cache;
  return juce::Font(juce::FontOptions(arimo(*cache, bold, italic)).withPointHeight(px));
}

juce::Font Fonts::mono(float px, bool bold) {
  Hold cache;
  return juce::Font(juce::FontOptions(robotoMono(*cache, bold)).withPointHeight(px));
}

juce::Font Fonts::tracked(const juce::Font& font, float em) {
  // withExtraKerningFactor is relative to the JUCE height; CSS em is relative
  // to the point size.
  return font.withExtraKerningFactor(em * font.getHeightToPointsFactor());
}

}  // namespace t3k::ui
