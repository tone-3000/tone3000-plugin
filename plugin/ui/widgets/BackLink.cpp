#include "BackLink.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

BackLink::BackLink(const juce::String& label, help::Key help) : Clickable("Back"), label_(label.toUpperCase()) {
  setHelpText(help::text(help));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  if (label_.isEmpty()) {
    setSize(kIcon, kHeight);
    return;
  }
  // drawText ellipsises at the measured width; a hair of slack keeps it whole.
  const int textW = static_cast<int>(std::ceil(Fonts::width(Fonts::mono(kPx), label_))) + 2;
  setSize(kIcon + kGap + textW, kHeight);
}

void BackLink::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  Icons::draw(g, Icon::ArrowLeft, juce::Rectangle<float>(kIcon, kIcon).withCentre({kIcon / 2.0f, box.getCentreY()}),
              theme::kWhite);
  if (label_.isNotEmpty())
    paint::text(g, label_, getLocalBounds().withTrimmedLeft(kIcon + kGap), Fonts::mono(kPx), theme::kWhite);
}

}  // namespace t3k::ui
