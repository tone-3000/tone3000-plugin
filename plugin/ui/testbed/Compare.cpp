#include "Compare.h"

#include <vector>

namespace t3k::ui::testbed {

CompareResult compareImages(const juce::Image& reference, const juce::Image& candidate,
                            juce::Image* diffOut, int tolerance) {
  CompareResult r;
  if (!reference.isValid() || !candidate.isValid()) {
    r.error = "image failed to load";
    return r;
  }
  if (reference.getWidth() != candidate.getWidth() || reference.getHeight() != candidate.getHeight()) {
    r.error = "size mismatch: reference " + juce::String(reference.getWidth()) + "x" +
              juce::String(reference.getHeight()) + ", candidate " +
              juce::String(candidate.getWidth()) + "x" + juce::String(candidate.getHeight());
    return r;
  }
  r.ok = true;
  r.width = reference.getWidth();
  r.height = reference.getHeight();
  if (diffOut != nullptr)
    *diffOut = juce::Image(juce::Image::RGB, r.width, r.height, false);

  const int tilesX = (r.width + kTile - 1) / kTile;
  std::vector<int> tileHits(static_cast<size_t>(tilesX * ((r.height + kTile - 1) / kTile)), 0);

  const juce::Image::BitmapData a(reference, juce::Image::BitmapData::readOnly);
  const juce::Image::BitmapData b(candidate, juce::Image::BitmapData::readOnly);
  for (int y = 0; y < r.height; ++y) {
    for (int x = 0; x < r.width; ++x) {
      const auto pa = a.getPixelColour(x, y);
      const auto pb = b.getPixelColour(x, y);
      const int d = juce::jmax(std::abs(pa.getRed() - pb.getRed()),
                               std::abs(pa.getGreen() - pb.getGreen()),
                               std::abs(pa.getBlue() - pb.getBlue()));
      const bool bad = d > tolerance;
      if (bad) {
        ++r.mismatched;
        ++tileHits[static_cast<size_t>((y / kTile) * tilesX + x / kTile)];
      }
      if (diffOut != nullptr) {
        const auto grey = static_cast<juce::uint8>(pa.getBrightness() * 60 + 40);
        diffOut->setPixelAt(x, y, bad ? juce::Colour(0xffff2d2d) : juce::Colour(grey, grey, grey));
      }
    }
  }
  for (size_t i = 0; i < tileHits.size(); ++i) {
    const int tx = static_cast<int>(i) % tilesX, ty = static_cast<int>(i) / tilesX;
    const int w = juce::jmin(kTile, r.width - tx * kTile), h = juce::jmin(kTile, r.height - ty * kTile);
    const double pct = 100.0 * tileHits[i] / (double(w) * h);
    if (pct > r.worstTilePercent) {
      r.worstTilePercent = pct;
      r.worstTile = {tx * kTile, ty * kTile};
    }
  }
  return r;
}

CompareResult comparePngFiles(const juce::File& reference, const juce::File& candidate,
                              const juce::File& diffOut) {
  juce::PNGImageFormat png;
  const auto ref = juce::ImageFileFormat::loadFrom(reference);
  const auto cand = juce::ImageFileFormat::loadFrom(candidate);
  juce::Image diff;
  auto result = compareImages(ref, cand, diffOut != juce::File() ? &diff : nullptr);
  if (result.ok && diffOut != juce::File()) {
    diffOut.deleteFile();
    juce::FileOutputStream out(diffOut);
    if (out.openedOk())
      png.writeImageToStream(diff, out);
  }
  return result;
}

}  // namespace t3k::ui::testbed
