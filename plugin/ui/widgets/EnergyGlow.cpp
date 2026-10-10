#include "EnergyGlow.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace t3k::ui {

EnergyGlow::EnergyGlow(MeterStore& meters, juce::String id) : meters_(meters), id_(std::move(id)) {
  meters_.addListener(this);
  level_ = meters_.level(id_);
}

EnergyGlow::~EnergyGlow() { meters_.removeListener(this); }

void EnergyGlow::meterChanged(const juce::String& id) {
  if (id != id_) return;
  const float level = meters_.level(id_);
  if (juce::exactlyEqual(level, level_)) return;
  level_ = level;
  if (onChange) onChange();
}

Glow EnergyGlow::glow() const {
  const auto energy = meter::energy(level_);
  if (energy.presence <= 0.0f) return {};
  return {energy.colour, kBlurMin + energy.presence * (kBlurMax - kBlurMin), kOpacity * energy.presence};
}

namespace {

// Edge distance of each pixel in a corner square of side `size`, measured
// to the rounded edge (the arc of radius `radius`, centred `radius` in from
// both edges) instead of to the straight ones: the glow hugs the rounded
// clip. A pixel on or past the arc is at distance 0 (the clip leaves it a
// sliver at most). Indexed like the band weights: entry d covers centre
// distances d..d+1, and top-left orientation; the other corners mirror.
struct CornerTable {
  float radius = -1;
  int size = 0;
  std::vector<int> d;

  void fit(float radiusPx) {
    if (juce::exactlyEqual(radius, radiusPx)) return;
    radius = radiusPx;
    size = static_cast<int>(std::ceil(radiusPx));
    d.assign(static_cast<size_t>(size * size), 0);
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float dx = std::max(0.0f, radiusPx - (static_cast<float>(x) + 0.5f));
        const float dy = std::max(0.0f, radiusPx - (static_cast<float>(y) + 0.5f));
        const float dist = radiusPx - std::sqrt(dx * dx + dy * dy);
        d[static_cast<size_t>(y * size + x)] = dist > 0.0f ? static_cast<int>(dist) : 0;
      }
    }
  }
};

}  // namespace

// An inset shadow of blur b is the outside region blurred by a Gaussian of
// radius ~b, clipped to the box: about half strength at the edge, fading
// to nothing b inside. Only the border band is touched, plus the corner
// squares when the corner radius reaches past it, since a pixel there can
// sit within the blur of the arc while a full band's width from the
// straight edges.
void Glow::compositeInto(juce::Image& image, float pixelScale, float cornerRadius) const {
  if (alpha <= 0.0f || blur <= 0.0f || !image.isValid()) return;
  jassert(image.getFormat() == juce::Image::ARGB);
  const float blurPx = blur * pixelScale;
  const int w = image.getWidth(), h = image.getHeight();
  const int band = std::min({static_cast<int>(std::ceil(blurPx)), w / 2, h / 2});

  // The falloff only depends on the edge distance, so it is tabulated once
  // per band pixel, in 8.8 fixed point. The tables are reused across ticks
  // (this runs per visible tile per meter frame); the corner one only
  // changes with the radius, so it is built once per size and scale.
  thread_local std::vector<int> weight;
  weight.assign(static_cast<size_t>(band), 0);
  for (int d = 0; d < band; ++d) {
    const float t = 1.0f - (static_cast<float>(d) + 0.5f) / blurPx;
    weight[static_cast<size_t>(d)] = t > 0.0f ? juce::roundToInt(alpha * 0.5f * t * t * 256.0f) : 0;
  }
  thread_local CornerTable corner;
  corner.fit(std::max(0.0f, cornerRadius * pixelScale));
  const int r = std::min({corner.size, w / 2, h / 2});
  const int reach = std::max(band, r);
  const int cr = colour.getRed(), cg = colour.getGreen(), cb = colour.getBlue();

  juce::Image::BitmapData data(image, juce::Image::BitmapData::readWrite);
  auto blend = [&](int x, int y) {
    int d = std::min({x, w - 1 - x, y, h - 1 - y});
    const int cx = std::min(x, w - 1 - x), cy = std::min(y, h - 1 - y);
    if (cx < r && cy < r) d = std::min(d, corner.d[static_cast<size_t>(cy * corner.size + cx)]);
    if (d >= band || weight[static_cast<size_t>(d)] == 0) return;
    const int k = weight[static_cast<size_t>(d)];
    auto* px = reinterpret_cast<juce::PixelARGB*>(data.getPixelPointer(x, y));
    const int a = px->getAlpha();
    // Screen on premultiplied channels: c + (a - c) · colour · k.
    auto screen = [&](int c, int glowC) { return c + (((a - c) * glowC * k) >> 16); };
    px->setARGB(static_cast<juce::uint8>(a), static_cast<juce::uint8>(screen(px->getRed(), cr)),
                static_cast<juce::uint8>(screen(px->getGreen(), cg)), static_cast<juce::uint8>(screen(px->getBlue(), cb)));
  };
  for (int y = 0; y < h; ++y) {
    if (y < band || y >= h - band) {
      for (int x = 0; x < w; ++x) blend(x, y);
    } else {
      // Corner rows widen to the corner squares; the rest is the band alone.
      const int edge = (y < reach || y >= h - reach) ? reach : band;
      for (int x = 0; x < edge; ++x) blend(x, y);
      for (int x = w - edge; x < w; ++x) blend(x, y);
    }
  }
}

}  // namespace t3k::ui
