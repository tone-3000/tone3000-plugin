#include "Blur.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace t3k::ui {

namespace {

// Box half-widths whose triple convolution approximates a Gaussian of
// standard deviation `sigma` (Peter Kovesi's "boxesForGauss").
std::array<int, 3> boxesForGauss(float sigma) {
  constexpr int n = 3;
  const float wIdeal = std::sqrt(12 * sigma * sigma / n + 1);
  int wl = static_cast<int>(std::floor(wIdeal));
  if (wl % 2 == 0) --wl;
  const int wu = wl + 2;
  const float mIdeal = (12 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4.0f * wl - 4);
  const int m = juce::roundToInt(mIdeal);
  std::array<int, 3> sizes{};
  for (int i = 0; i < n; ++i) sizes[static_cast<size_t>(i)] = ((i < m ? wl : wu) - 1) / 2;
  return sizes;
}

// One box pass along a line of `count` pixels with stride `stride` (in
// pixels), 4 channels each, from `src` into `dst`. Edges clamp.
void boxLine(const juce::uint8* src, juce::uint8* dst, int count, int stride, int r) {
  const int bytes = stride * 4;
  const float scale = 1.0f / static_cast<float>(2 * r + 1);
  for (int c = 0; c < 4; ++c) {
    const juce::uint8* s = src + c;
    juce::uint8* d = dst + c;
    const int first = s[0], last = s[(count - 1) * bytes];
    int sum = (r + 1) * first;
    for (int j = 0; j < r; ++j) sum += s[juce::jmin(j, count - 1) * bytes];
    for (int i = 0; i < count; ++i) {
      const int add = i + r < count ? s[(i + r) * bytes] : last;
      const int drop = i - r - 1 >= 0 ? s[(i - r - 1) * bytes] : first;
      sum += add - drop;
      d[i * bytes] = static_cast<juce::uint8>(juce::roundToInt(sum * scale));
    }
  }
}

}  // namespace

void blurImage(juce::Image& image, int radius) {
  if (radius <= 0 || image.isNull()) return;
  if (image.getFormat() != juce::Image::ARGB) image = image.convertedToFormat(juce::Image::ARGB);
  const int w = image.getWidth(), h = image.getHeight();
  const size_t rowBytes = static_cast<size_t>(w) * 4;
  juce::Image::BitmapData data(image, juce::Image::BitmapData::readWrite);
  // Work on a tightly packed copy: BitmapData's line stride may be padded.
  std::vector<juce::uint8> a(rowBytes * static_cast<size_t>(h)), b(a.size());
  const auto row = [rowBytes](std::vector<juce::uint8>& buf, int y) { return &buf[static_cast<size_t>(y) * rowBytes]; };
  for (int y = 0; y < h; ++y) std::memcpy(row(a, y), data.getLinePointer(y), rowBytes);

  for (const int r : boxesForGauss(static_cast<float>(radius))) {
    if (r <= 0) continue;
    for (int y = 0; y < h; ++y) boxLine(row(a, y), row(b, y), w, 1, r);  // horizontal: a → b
    for (int x = 0; x < w; ++x)                                          // vertical: b → a
      boxLine(&b[static_cast<size_t>(x) * 4], &a[static_cast<size_t>(x) * 4], h, w, r);
  }
  for (int y = 0; y < h; ++y) std::memcpy(data.getLinePointer(y), row(a, y), rowBytes);
}

}  // namespace t3k::ui
