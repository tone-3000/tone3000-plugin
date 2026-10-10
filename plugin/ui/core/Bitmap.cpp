#include "Bitmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace t3k::ui::bitmap {

namespace {

// Resample to exactly `w` × `h`. Bilinear filtering only looks at the four
// nearest source pixels, so a reduction past 2:1 is done as a chain of
// halvings first (each one a 2×2 box filter); the last step is then at
// most a 2:1 bilinear pass.
juce::Image resample(juce::Image image, int w, int h) {
  while (image.getWidth() >= 2 * w && image.getHeight() >= 2 * h)
    image = image.rescaled(image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::mediumResamplingQuality);
  if (image.getWidth() != w || image.getHeight() != h)
    image = image.rescaled(w, h, juce::Graphics::highResamplingQuality);
  return image;
}

}  // namespace

float pixelScale(const juce::Graphics& g) {
  return g.getInternalContext().getPhysicalPixelScaleFactor();
}

juce::Image cover(const juce::Image& image, int w, int h, float scale) {
  const int pw = std::max(1, static_cast<int>(std::ceil(w * scale)));
  const int ph = std::max(1, static_cast<int>(std::ceil(h * scale)));
  // The largest centred crop with the box's aspect ratio.
  const float k = std::min(image.getWidth() / static_cast<float>(pw), image.getHeight() / static_cast<float>(ph));
  const int cw = std::clamp(juce::roundToInt(pw * k), 1, image.getWidth());
  const int ch = std::clamp(juce::roundToInt(ph * k), 1, image.getHeight());
  const auto crop = image.getClippedImage({(image.getWidth() - cw) / 2, (image.getHeight() - ch) / 2, cw, ch});
  return resample(crop, pw, ph).convertedToFormat(juce::Image::ARGB);
}

void draw(juce::Graphics& g, const juce::Image& bitmap, juce::Rectangle<int> bounds) {
  if (!bitmap.isValid() || bounds.isEmpty()) return;
  g.drawImageTransformed(bitmap, juce::AffineTransform::scale(bounds.getWidth() / static_cast<float>(bitmap.getWidth()),
                                                             bounds.getHeight() / static_cast<float>(bitmap.getHeight()))
                                     .translated(bounds.getPosition().toFloat()));
}

void copyPixels(juce::Image& dst, const juce::Image& src) {
  jassert(dst.getBounds() == src.getBounds() && dst.getFormat() == src.getFormat());
  const juce::Image::BitmapData from(src, juce::Image::BitmapData::readOnly);
  juce::Image::BitmapData to(dst, juce::Image::BitmapData::writeOnly);
  const auto rowBytes = static_cast<size_t>(from.width) * static_cast<size_t>(from.pixelStride);
  for (int y = 0; y < from.height; ++y) std::memcpy(to.getLinePointer(y), from.getLinePointer(y), rowBytes);
}

}  // namespace t3k::ui::bitmap
