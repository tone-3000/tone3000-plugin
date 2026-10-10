// Realtime inset energy glow for gallery tiles (BlockLed.tsx
// BlockEnergyBorder): blue→yellow→red by level, blur 2px→18px and alpha
// 0→0.75 with presence, from the live meter only (no clip latch). The web
// drew it as an inset box-shadow with `mix-blend-mode: screen`; here the
// tile's artwork composites it the same way (ToneImage::setGlow), so bright
// art is barely tinted and dark art lights up.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/MeterScale.h"
#include "services/MeterStore.h"

namespace t3k::ui {

struct Glow {
  juce::Colour colour;
  float blur = 0;   // px the falloff reaches into the box
  float alpha = 0;  // peak strength at the edge (0 = no glow)

  bool operator==(const Glow& o) const {
    return colour == o.colour && juce::exactlyEqual(blur, o.blur) && juce::exactlyEqual(alpha, o.alpha);
  }
  bool operator!=(const Glow& o) const { return !(*this == o); }

  // Screen-blend the inset glow into `image` (ARGB, rasterised at
  // `pixelScale` device pixels per logical px): result = base + (1 - base)
  // · colour · falloff(d), where d is the distance to the nearest edge and
  // the falloff is the blurred edge of an inset shadow (half strength at
  // the edge, none `blur` logical px in). The edge is the rounded rectangle
  // of `cornerRadius` logical px the image is clipped to, so the glow turns
  // the corners with it (the web's box-shadow followed border-radius).
  void compositeInto(juce::Image& image, float pixelScale, float cornerRadius = 0.0f) const;
};

class EnergyGlow : private MeterStore::Listener {
public:
  static constexpr float kBlurMin = 2.0f;
  static constexpr float kBlurMax = 18.0f;
  static constexpr float kOpacity = 0.75f;

  EnergyGlow(MeterStore& meters, juce::String id);
  ~EnergyGlow() override;

  Glow glow() const;
  std::function<void()> onChange;

private:
  void meterChanged(const juce::String& id) override;

  MeterStore& meters_;
  juce::String id_;
  float level_ = MeterStore::kFloorDb;
};

}  // namespace t3k::ui
