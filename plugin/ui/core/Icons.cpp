#include "Icons.h"

#include <map>
#include <tuple>

#include "Paint.h"

namespace t3k::ui {

namespace {

std::unique_ptr<juce::Drawable> parse(const juce::String& svg, juce::Colour colour) {
  auto drawable = juce::Drawable::createFromSVGString(svg);
  jassert(drawable != nullptr);
  if (drawable != nullptr) drawable->replaceColour(juce::Colours::white, colour);
  return drawable;
}

// Parsed + tinted once per (svg, colour, stroke): the palette is a handful
// of colours, so this stays tiny and paint() never re-parses SVG.
const juce::Drawable* cached(const char* svg, juce::Colour colour, float strokeWidth = 0.0f) {
  struct Key {
    const char* svg;
    juce::uint32 argb;
    float stroke;
    bool operator<(const Key& o) const {
      return std::tie(svg, argb, stroke) < std::tie(o.svg, o.argb, o.stroke);
    }
  };
  // Function-local statics are destroyed in reverse order of construction,
  // JUCE's leak counters included: if the first Drawable in the process were
  // parsed *into* this map, the counters would be torn down first and report
  // every cached icon. Construct one of each Drawable a parsed SVG holds
  // before the map exists so their counters always outlive it.
  static const bool countersFirst = (juce::DrawablePath{}, juce::DrawableComposite{}, true);
  juce::ignoreUnused(countersFirst);
  static std::map<Key, std::unique_ptr<juce::Drawable>> cache;
  auto& slot = cache[{svg, colour.getARGB(), strokeWidth}];
  if (slot == nullptr) {
    juce::String source(svg);
    // Lucide icons carry stroke-width="2" on the root; swap in the override.
    if (strokeWidth > 0.0f)
      source = source.replace("stroke-width=\"2\"",
                              "stroke-width=\"" + juce::String(strokeWidth) + "\"");
    slot = parse(source, colour);
  }
  return slot.get();
}

// Tint with the *opaque* colour and apply its alpha as group opacity, the way
// CSS `opacity` composites a whole <svg>. Baking a translucent colour into
// each sub-path instead would double up wherever Lucide's strokes overlap
// (dimmed icons got brighter at the crossings), and would also bloat the
// cache with one entry per alpha.
void drawTinted(juce::Graphics& g, const char* svg, juce::Rectangle<float> box, juce::Colour colour,
                float strokeWidth = 0.0f) {
  if (colour.getFloatAlpha() <= 0.0f) return;
  if (auto* d = cached(svg, colour.withAlpha(1.0f), strokeWidth))
    paint::svg(g, *d, box, colour.getFloatAlpha());
}

}  // namespace

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour) {
  drawTinted(g, lucideSvg(icon), box, colour);
}

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour,
                 float strokeWidth) {
  drawTinted(g, lucideSvg(icon), box, colour, strokeWidth);
}

void Icons::draw(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                 juce::Colour colour) {
  drawTinted(g, svg, box, colour);
}

}  // namespace t3k::ui
