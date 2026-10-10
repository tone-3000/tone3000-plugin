// A fast approximate Gaussian blur for backdrops (CSS `backdrop-filter:
// blur(Npx)`): three separable box passes over an ARGB image, in place.
// Premultiplied pixels average correctly, so edges against transparency
// don't darken. Cost is linear in pixels regardless of radius.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace t3k::ui {

// `radius` in image pixels (the CSS blur's standard deviation, roughly).
void blurImage(juce::Image& image, int radius);

}  // namespace t3k::ui
