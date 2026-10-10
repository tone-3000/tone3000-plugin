#include "ToneImage.h"

#include <algorithm>
#include <cmath>

#include "core/Bitmap.h"
#include "core/GearGlyphs.h"
#include "core/Icons.h"
#include "core/Theme.h"

namespace t3k::ui {

ToneImage::ToneImage(ImageLoader& loader) : loader_(loader) {
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // artwork; the tile or card carries the name
  setOpaque(true);
}

void ToneImage::setTone(const juce::String& imageUrl, const juce::String& gear, bool local,
                        int glyphSize) {
  gear_ = gear;
  local_ = local;
  glyphSize_ = glyphSize;
  if (imageUrl != url_) {
    // A new URL (tone swap / model switch) gets a fresh chance to load.
    url_ = imageUrl;
    image_ = {};
    if (!local_ && url_.isNotEmpty())
      loader_.load(url_, ImageLoader::kArtSide, request_, [this](const juce::Image& image) {
        image_ = image;
        base_ = {};
        repaint();
      });
    else
      request_.cancel();
  }
  base_ = {};
  repaint();
}

void ToneImage::setCornerRadius(float radius) {
  corner_ = radius;
  setOpaque(radius <= 0.0f);
  composited_ = {};  // the glow follows the corners
  repaint();
}

void ToneImage::setGlow(const Glow& glow) {
  if (glow == glow_) return;
  // Only the border band changes (compositeInto leaves the interior alone),
  // so dirty just that: four strips as wide as the wider of the old and new
  // falloffs, rounded out a pixel. A meter tick then repaints ~1/3 of the
  // tile instead of all of it, and everything stacked under the tile gets
  // the same smaller clip. The glow follows the rounded corners in past a
  // narrow band, so the top and bottom strips are at least a corner deep:
  // that covers the corner squares in the same four rects (every extra
  // dirty rect is its own paint pass on macOS, and costs more than the
  // pixels it saves).
  const int band = static_cast<int>(std::ceil(std::max(glow_.blur, glow.blur))) + 1;
  const int cap = std::max(band, static_cast<int>(std::ceil(corner_)));
  glow_ = glow;
  auto box = getLocalBounds();
  if (cap * 2 >= std::min(box.getWidth(), box.getHeight())) {
    repaint();
    return;
  }
  repaint(box.removeFromTop(cap));
  repaint(box.removeFromBottom(cap));
  repaint(box.removeFromLeft(band));
  repaint(box.removeFromRight(band));
}

void ToneImage::rebuildBase(float scale) {
  const int w = getWidth(), h = getHeight();
  if (w <= 0 || h <= 0) return;
  baseScale_ = scale;
  composited_ = {};
  if (!local_ && image_.isValid()) {
    base_ = bitmap::cover(image_, w, h, scale);
    return;
  }
  base_ = juce::Image(juce::Image::ARGB, static_cast<int>(std::ceil(w * scale)),
                      static_cast<int>(std::ceil(h * scale)), true);
  juce::Graphics g(base_);
  g.addTransform(juce::AffineTransform::scale(scale));
  const auto box = getLocalBounds().toFloat();
  g.setColour(theme::kSurface);
  g.fillRect(box);
  const float size = glyphSize_ > 0 ? static_cast<float>(glyphSize_)
                                    : static_cast<float>(juce::roundToInt(box.getWidth() * 0.4f));
  // Local files carry a gear only when native could infer one from the file
  // (see finishLocalToneLoad); otherwise the generic file glyph, never
  // svgFor's catalog default of an amp.
  Icons::draw(g, local_ && gear_.isEmpty() ? gear::kFileIcon : gear::svgFor(gear_),
              box.withSizeKeepingCentre(size, size), theme::kGray);
}

void ToneImage::paint(juce::Graphics& g) {
  const float scale = bitmap::pixelScale(g);
  if (!base_.isValid() || !juce::approximatelyEqual(scale, baseScale_)) rebuildBase(scale);
  if (!base_.isValid()) return;
  if (corner_ > 0.0f) {
    juce::Path clip;
    clip.addRoundedRectangle(getLocalBounds().toFloat(), corner_);
    g.reduceClipRegion(clip);
  }
  if (glow_.alpha <= 0.0f) {
    bitmap::draw(g, base_, getLocalBounds());
    return;
  }
  // Re-blend only when the glow moved; a repaint for any other reason
  // (hover chrome, LED) reuses the last composite.
  if (!composited_.isValid() || compositedGlow_ != glow_) {
    // The buffer is reused across glow ticks; only the pixels are refreshed.
    if (composited_.getBounds() != base_.getBounds())
      composited_ = juce::Image(juce::Image::ARGB, base_.getWidth(), base_.getHeight(), false);
    bitmap::copyPixels(composited_, base_);
    glow_.compositeInto(composited_, scale, corner_);
    compositedGlow_ = glow_;
  }
  bitmap::draw(g, composited_, getLocalBounds());
}

}  // namespace t3k::ui
