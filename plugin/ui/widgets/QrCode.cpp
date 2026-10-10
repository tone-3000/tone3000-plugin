#include "QrCode.h"

#include <stdexcept>

#include "core/Theme.h"
#include "vendor/qrcodegen/qrcodegen.hpp"

namespace t3k::ui {

QrCode::QrCode() {
  setOpaque(false);
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // the code is read out as text beside it
}

void QrCode::setText(const juce::String& text) {
  modules_ = {};
  if (text.isNotEmpty()) {
    try {
      // Medium error correction: a phone reads it from a screen at an angle.
      const auto qr = qrcodegen::QrCode::encodeText(text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
      const int n = qr.getSize();
      modules_ = juce::Image(juce::Image::ARGB, n, n, true);
      juce::Image::BitmapData pixels(modules_, juce::Image::BitmapData::writeOnly);
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) pixels.setPixelColour(x, y, qr.getModule(x, y) ? theme::kBlack : theme::kWhite);
    } catch (const std::length_error&) {
      modules_ = {};  // too long for any version: the card stays blank
    }
  }
  repaint();
}

void QrCode::paint(juce::Graphics& g) {
  const auto card = getLocalBounds().toFloat();
  g.setColour(theme::kWhite);
  g.fillRoundedRectangle(card, kCorner);
  if (!modules_.isValid()) return;
  const int n = modules_.getWidth();
  const int inner = juce::jmin(getWidth(), getHeight()) - 2 * kPad;
  const int modulePx = juce::jmax(1, inner / n);
  const int side = modulePx * n;
  const auto box = juce::Rectangle<int>(side, side).withCentre(getLocalBounds().getCentre());
  g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
  g.drawImage(modules_, box.toFloat(), juce::RectanglePlacement::stretchToFit);
}

}  // namespace t3k::ui
