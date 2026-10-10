#include "KnobFace.h"

#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "core/Bitmap.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

// All in the 200x200 viewBox.
constexpr float kRimRadius = 100.0f;
constexpr float kChannelRadius = 98.4f;
constexpr float kArcRadius = 91.56f;
constexpr float kArcWidth = 10.12f;
constexpr float kShadowRadius = 86.4f;
constexpr std::array<float, 3> kFaceRadii{84.9f, 82.75f, 79.05f};
constexpr float kPointerOrbit = 51.0f;
constexpr float kPointerRadius = 14.4f;
constexpr float kPointerHoleRadius = 10.25f;

// A face layer is either a flat colour or a top-to-bottom two-stop ramp.
struct FaceFill {
  juce::Colour top, bottom;
  bool flat = false;
};
constexpr juce::uint32 rgb(juce::uint32 v) { return 0xff000000u | v; }
const std::array<FaceFill, 3> kPrimaryFills{
    FaceFill{juce::Colour(rgb(0xa8a8a8)), juce::Colour(rgb(0x3d3d3d))},
    FaceFill{juce::Colour(rgb(0xa9a9a9)), juce::Colour(rgb(0x242424))},
    FaceFill{juce::Colour(rgb(0x979797)), juce::Colour(rgb(0x232323))},
};
const std::array<FaceFill, 3> kSecondaryFills{
    FaceFill{juce::Colour(rgb(0xa3a3a3)), juce::Colour(rgb(0xa3a3a3)), true},
    FaceFill{juce::Colour(rgb(0xa9a9a9)), juce::Colour(rgb(0x000000))},
    FaceFill{juce::Colour(rgb(0x505050)), juce::Colour(rgb(0x000000))},
};
const juce::Colour kRim{rgb(0x1d1d1d)};

inline float rad(float deg) { return juce::degreesToRadians(deg); }

void fillCircle(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour) {
  g.setColour(colour);
  g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
}

// SVG objectBoundingBox gradient (x1=0,y1=0 -> x2=0,y2=1): runs top to
// bottom over the element's own bounds.
void fillCircle(juce::Graphics& g, juce::Point<float> c, float r, juce::ColourGradient gradient) {
  g.setGradientFill(gradient);
  g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
}

// Static base: never rotates, lighting stays put.
void drawBase(juce::Graphics& g, juce::Point<float> c, float s) {
  fillCircle(g, c, kRimRadius * s, kRim);
  fillCircle(g, c, kChannelRadius * s, theme::kBlack);
}

// Face stack: shadow ring, then the tone's three layers.
void drawFace(juce::Graphics& g, juce::Point<float> c, float s, KnobTone tone) {
  fillCircle(g, c, kShadowRadius * s, kRim);
  const auto& fills = tone == KnobTone::primary ? kPrimaryFills : kSecondaryFills;
  for (size_t i = 0; i < fills.size(); ++i) {
    const float r = kFaceRadii[i] * s;
    if (fills[i].flat)
      fillCircle(g, c, r, fills[i].top);
    else
      fillCircle(g, c, r, juce::ColourGradient::vertical(fills[i].top, c.y - r, fills[i].bottom, c.y + r));
  }
}

// The base and the face stack never change with the value, and the face is
// three gradient discs, which CoreGraphics shades pixel by pixel on every
// paint: at 60 drag events a second that is most of a knob's paint time.
// They are rasterised once per (device size, tone) and blitted; the arc
// (which sits between them) and the pointer are drawn live. A handful of
// knob sizes times the display scales in use keeps the cache tiny. The
// cache itself is editor-scoped, not a static (see KnobFaceCache).
using StaticLayers = KnobFaceCache::Entry;

const StaticLayers& staticLayers(std::vector<StaticLayers>& cache, float boxWidth, float pixelScale,
                                 KnobTone tone) {
  static constexpr size_t kMaxEntries = 32;
  const int devicePx = juce::roundToInt(boxWidth * pixelScale * 64.0f);  // 1/64 device px
  for (const auto& e : cache)
    if (e.devicePx == devicePx && e.tone == tone) return e;
  if (cache.size() >= kMaxEntries) cache.clear();

  StaticLayers e;
  e.devicePx = devicePx;
  e.tone = tone;
  const int side = static_cast<int>(std::ceil(boxWidth * pixelScale));
  const float s = boxWidth / 200.0f;
  const juce::Point<float> c(boxWidth / 2, boxWidth / 2);
  for (auto [image, face] : {std::pair{&e.base, false}, std::pair{&e.face, true}}) {
    *image = juce::Image(juce::Image::ARGB, side, side, true);
    juce::Graphics g(*image);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    if (face) drawFace(g, c, s, tone);
    else drawBase(g, c, s);
  }
  cache.push_back(std::move(e));
  return cache.back();
}

void blit(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> box, float pixelScale) {
  g.drawImageTransformed(image, juce::AffineTransform::scale(1.0f / pixelScale).translated(box.getPosition()));
}

}  // namespace

void drawKnobFace(juce::Graphics& g, juce::Rectangle<float> box, float angleDeg, float arcFromDeg,
                  KnobTone tone) {
  const float s = box.getWidth() / 200.0f;
  const auto c = box.getCentre();
  const auto R = [s](float r) { return r * s; };
  const float pixelScale = bitmap::pixelScale(g);
  // Pins the shared cache for this paint. Normally the editor's own hold
  // keeps it alive between paints; with no editor holding it (nothing in
  // the plugin, but a bare testbed call could) the layers are simply
  // rebuilt each time: correct, just slow.
  KnobFaceCacheHold cache;
  const auto& layers = staticLayers(cache->entries, box.getWidth(), pixelScale, tone);

  blit(g, layers.base, box, pixelScale);

  // Value arc from the zero reference out to the pointer. Endpoints are
  // ordered ascending so one clockwise sweep serves both a centred knob
  // (fills either way from noon) and a plain one (from bottom left).
  const float from = juce::jmin(arcFromDeg, angleDeg);
  const float to = juce::jmax(arcFromDeg, angleDeg);
  if (to - from >= 0.25f) {
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, R(kArcRadius), R(kArcRadius), 0.0f, rad(from), rad(to), true);
    g.setColour(theme::kBrandYellow);
    g.strokePath(arc, juce::PathStrokeType(R(kArcWidth), juce::PathStrokeType::curved,
                                           juce::PathStrokeType::butt));
  }

  blit(g, layers.face, box, pixelScale);

  // Pointer. Its bevel has directional light baked in (dark top, light
  // bottom: the "chamfered edge catches light from below" convention the
  // whole faceplate uses), so the gradient stays vertical in screen space
  // wherever the pointer has turned to.
  const float a = rad(angleDeg);
  const juce::Point<float> p(c.x + R(kPointerOrbit) * std::sin(a), c.y - R(kPointerOrbit) * std::cos(a));
  const float pr = R(kPointerRadius);
  auto bevel = juce::ColourGradient::vertical(juce::Colour(rgb(0x040404)), p.y - pr,
                                              juce::Colour(rgb(0xa9a9a9)), p.y + pr);
  bevel.addColour(0.20, juce::Colour(rgb(0x1b1b1b)));
  bevel.addColour(0.50, juce::Colour(rgb(0x626262)));
  bevel.addColour(0.78, juce::Colour(rgb(0xa9a9a9)));
  fillCircle(g, p, pr, bevel);
  fillCircle(g, p, R(kPointerHoleRadius), theme::kBlack);
}

}  // namespace t3k::ui
