#include "Avatar.h"

#include "core/Bitmap.h"
#include "core/CustomIcons.h"
#include "core/Icons.h"
#include "core/Theme.h"

namespace t3k::ui {

void Avatar::setImage(ImageLoader& loader, const juce::String& url) {
  if (url == url_) return;
  url_ = url;
  image_ = {};
  cover_ = {};
  if (url_.isNotEmpty())
    loader.load(url_, ImageLoader::kAvatarSide, request_, [this](const juce::Image& image) {
      image_ = image;
      cover_ = {};
      repaint();
    });
  else
    request_.cancel();
  repaint();
}

void Avatar::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  if (!image_.isValid()) {
    Icons::draw(g, custom_icons::kAvatarFallback, box, theme::kGray);
    return;
  }
  const float scale = bitmap::pixelScale(g);
  if (!cover_.isValid() || !juce::approximatelyEqual(scale, coverScale_)) {
    cover_ = bitmap::cover(image_, getWidth(), getHeight(), scale);
    coverScale_ = scale;
  }
  // object-fit: cover inside a circular clip.
  juce::Path clip;
  clip.addEllipse(box);
  g.reduceClipRegion(clip);
  bitmap::draw(g, cover_, getLocalBounds());
}

}  // namespace t3k::ui
