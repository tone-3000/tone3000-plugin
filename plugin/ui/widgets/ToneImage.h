// Tone artwork with recovery (port of GearIcon.tsx ToneImage): the image
// URL when it loads, otherwise the gear glyph centred on SURFACE (missing
// artwork, or the fetch failed: offline / tone3000.com down). Local-file
// blocks have no artwork: they show the gear glyph when native inferred a
// gear from the file (.nam metadata, cab-length IR), else a file glyph.
// Fills its bounds like a cover-fit <img>; the gallery tiles round its
// corners and screen-blend the live energy glow into it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EnergyGlow.h"
#include "services/ImageLoader.h"

namespace t3k::ui {

class ToneImage : public juce::Component {
public:
  explicit ToneImage(ImageLoader& loader);

  // `glyphSize` 0 = ~40% of the box, like the web's card fallbacks; the
  // gallery tiles pin it to 64.
  void setTone(const juce::String& imageUrl, const juce::String& gear, bool local,
               int glyphSize = 0);
  // Clip to rounded corners (the tile's overflow: hidden face).
  void setCornerRadius(float radius);
  // Inset energy glow composited over the art (screen blend).
  void setGlow(const Glow& glow);

  void paint(juce::Graphics& g) override;
  void resized() override { base_ = {}; }

private:
  // The cover-fitted art (or SURFACE + glyph) rasterised once at this size
  // and pixel scale, so a glow level change is a copy + border-band pass.
  void rebuildBase(float scale);

  ImageLoader& loader_;
  ImageLoader::Request request_;
  juce::String url_, gear_;
  bool local_ = false;
  int glyphSize_ = 0;
  float corner_ = 0.0f;
  juce::Image image_;
  juce::Image base_, composited_;
  float baseScale_ = 0.0f;
  Glow glow_, compositedGlow_;
};

}  // namespace t3k::ui
