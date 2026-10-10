#include "BusyOverlay.h"

#include "core/Blur.h"

namespace t3k::ui {

BusyOverlay::BusyOverlay(Align align) : align_(align) {
  // Swallows the pointer like the web's covering div.
  setInterceptsMouseClicks(true, false);
  addAndMakeVisible(dots_);
  setAccessible(false);  // the owner announces what is loading
}

BusyOverlay::~BusyOverlay() { stopTimer(); }

void BusyOverlay::visibilityChanged() {
  if (snapshotting_) return;  // our own hide / show around the snapshot
  if (isVisible()) {
    refresh();
    startTimer(kRefreshMs);
  } else {
    stopTimer();
    blurred_ = juce::Image();
  }
}

void BusyOverlay::parentHierarchyChanged() {
  if (isVisible()) refresh();
}

void BusyOverlay::refresh() {
  auto* parent = getParentComponent();
  if (parent == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
  snapshotting_ = true;
  setVisible(false);
  blurred_ = parent->createComponentSnapshot(getBounds(), false, kSnapshotScale);
  setVisible(true);
  snapshotting_ = false;
  blurImage(blurred_, juce::roundToInt(kBlurPx * kSnapshotScale));
  repaint();
}

void BusyOverlay::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  juce::Path clip;
  clip.addRoundedRectangle(box, radius_);
  g.reduceClipRegion(clip);
  if (blurred_.isValid()) {
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImage(blurred_, box);
  }
  g.fillAll(juce::Colours::black.withAlpha(0.5f));
}

void BusyOverlay::resized() {
  const int x = (getWidth() - dots_.getWidth()) / 2;
  const int y = align_ == Align::top ? kTopPad : (getHeight() - dots_.getHeight()) / 2;
  dots_.setTopLeftPosition(x, y);
  if (isVisible()) refresh();
}

}  // namespace t3k::ui
