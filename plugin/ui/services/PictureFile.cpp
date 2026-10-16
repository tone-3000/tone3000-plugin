#include "PictureFile.h"

#include "src/webp/decode.h"

namespace t3k::ui::picture_file {

namespace {

juce::Image loadWebP(const juce::File& file) {
  juce::MemoryBlock bytes;
  if (!file.loadFileAsData(bytes)) return {};
  const auto* data = static_cast<const uint8_t*>(bytes.getData());
  int w = 0, h = 0;
  if (!WebPGetInfo(data, bytes.getSize(), &w, &h) || w <= 0 || h <= 0) return {};
  // A picture for a block, not a poster: past 8192 x 8192 it isn't decoded
  // (the RGBA buffer and the image would take gigabytes).
  constexpr int kMaxSide = 8192;
  if (w > kMaxSide || h > kMaxSide) return {};
  juce::Image image(juce::Image::ARGB, w, h, false);
  juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
  // Decoded as straight-alpha RGBA, then set pixel by pixel (setPixelColour
  // premultiplies, the way JUCE's ARGB images keep their pixels).
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  if (WebPDecodeRGBAInto(data, bytes.getSize(), rgba.data(), rgba.size(), w * 4) == nullptr) return {};
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const auto* p = &rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
      pixels.setPixelColour(x, y, juce::Colour(p[0], p[1], p[2], p[3]));
    }
  return image;
}

}  // namespace

juce::Image load(const juce::File& file) {
  if (file.hasFileExtension(".webp")) return loadWebP(file);
  return juce::ImageFileFormat::loadFrom(file);
}

juce::String patterns() { return "*.png;*.jpg;*.jpeg;*.gif;*.webp"; }

}  // namespace t3k::ui::picture_file
