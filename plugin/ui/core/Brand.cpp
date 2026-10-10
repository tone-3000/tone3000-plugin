#include "Brand.h"

#include "Paint.h"
#include "UiBinaryData.h"

namespace t3k::ui {

namespace {
std::unique_ptr<juce::Drawable> parse(const char* data, int size) {
  auto drawable = juce::Drawable::createFromImageData(data, static_cast<size_t>(size));
  jassert(drawable != nullptr);
  return drawable;
}

constexpr const char* kA2MarkSvg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none">
<path d="M20 0C22.2091 2.57702e-07 24 1.79086 24 4V20C24 22.2091 22.2091 24 20 24H4C1.79086 24 6.44266e-08 22.2091 0 20V4C2.57706e-07 1.79086 1.79086 6.44256e-08 4 0H20ZM6.3125 6.16406L2.40039 18.1074H4.82812L5.5332 15.6143H9.11816L9.81543 18.1074H12.2441L8.4043 6.16406H6.3125ZM17.0674 6C16.433 6 15.8531 6.10373 15.3281 6.31152C14.8086 6.51387 14.3654 6.79297 13.999 7.14844C13.6272 7.50388 13.34 7.91977 13.1377 8.39551C12.9354 8.86575 12.834 9.3661 12.834 9.89648H15.123C15.123 9.57383 15.1641 9.28672 15.2461 9.03516C15.3281 8.78359 15.4488 8.57285 15.6074 8.40332C15.7605 8.23941 15.9521 8.11616 16.1816 8.03418C16.4167 7.9468 16.6872 7.90334 16.9932 7.90332C17.2281 7.90332 17.4415 7.94114 17.6328 8.01758C17.8296 8.09409 17.9995 8.20366 18.1416 8.3457C18.2783 8.49329 18.3854 8.67121 18.4619 8.87891C18.5385 9.08667 18.5762 9.32488 18.5762 9.59277C18.5762 9.77313 18.5464 9.95911 18.4863 10.1504C18.4317 10.3417 18.3415 10.547 18.2158 10.7656C18.0846 10.9898 17.9148 11.236 17.707 11.5039C17.4992 11.7718 17.2451 12.0704 16.9443 12.3984L13.0801 16.5488V18.1074H21.2266V16.2783H15.9844L18.2324 13.8994C18.6205 13.5004 18.973 13.1205 19.29 12.7598C19.6072 12.3934 19.8807 12.0346 20.1104 11.6846C20.3345 11.3347 20.5067 10.9822 20.627 10.627C20.7527 10.266 20.8164 9.88789 20.8164 9.49414C20.8164 8.97476 20.7343 8.50165 20.5703 8.0752C20.4062 7.64318 20.1682 7.27401 19.8564 6.96777C19.5393 6.66152 19.1451 6.42344 18.6748 6.25391C18.2101 6.08447 17.6742 6.00002 17.0674 6ZM8.57715 13.6533H6.09961L7.35449 9.25684L8.57715 13.6533Z" fill="url(#a2g)"/>
<defs><linearGradient id="a2g" x1="12" y1="0" x2="12" y2="24" gradientUnits="userSpaceOnUse"><stop stop-color="white"/><stop offset="1" stop-color="#434343"/></linearGradient></defs>
</svg>)svg";

// The web badge is one check polyline stroked three times with an inside
// stroke via SVG masks, which JUCE's parser doesn't support. This is the same
// geometry flattened to three filled polygons (one per colour, 2.94 units
// apart), painted blue, red, yellow like the source so the overlaps match.
constexpr const char* kVerifiedBadgeSvg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 17.5 20">
<polygon points="17.50,5.88 5.47,20.00 0.00,12.96 2.83,11.81 5.58,15.34 11.60,8.27" fill="#0000ff"/>
<polygon points="17.50,0.00 5.47,14.12 0.00,7.08 2.83,5.93 5.58,9.46 11.60,2.38" fill="#ff0000"/>
<polygon points="17.50,2.94 5.47,17.06 0.00,10.02 2.83,8.87 5.58,12.40 11.60,5.32" fill="#ffff00"/>
</svg>)svg";

std::unique_ptr<juce::Drawable> parseString(const char* svg) {
  auto drawable = juce::Drawable::createFromSVGString(svg);
  jassert(drawable != nullptr);
  return drawable;
}

}  // namespace

const juce::Drawable& Brand::logo() {
  static const auto d = parse(UiBinaryData::t3k_svg, UiBinaryData::t3k_svgSize);
  return *d;
}

const juce::Drawable& Brand::mark() {
  static const auto d = parse(UiBinaryData::t3kmark_svg, UiBinaryData::t3kmark_svgSize);
  return *d;
}

const juce::Drawable& Brand::a2Mark() {
  static const auto d = parseString(kA2MarkSvg);
  return *d;
}

const juce::Drawable& Brand::verifiedBadge() {
  static const auto d = parseString(kVerifiedBadgeSvg);
  return *d;
}

void Brand::drawLogo(juce::Graphics& g, juce::Rectangle<float> box) {
  // The web laid the logo out by width only (<img style="width: 160rem">),
  // so the height follows the 210:32 viewBox and may be fractional.
  const float h = box.getWidth() * 32.0f / 210.0f;
  paint::svg(g, logo(), box.withSizeKeepingCentre(box.getWidth(), h));
}

void Brand::drawMark(juce::Graphics& g, juce::Rectangle<float> box) {
  paint::svg(g, mark(), box);
}

void Brand::drawA2Mark(juce::Graphics& g, juce::Rectangle<float> box) {
  paint::svg(g, a2Mark(), box);
}

void Brand::drawVerifiedBadge(juce::Graphics& g, juce::Rectangle<float> box) {
  paint::svg(g, verifiedBadge(), box);
}

}  // namespace t3k::ui
