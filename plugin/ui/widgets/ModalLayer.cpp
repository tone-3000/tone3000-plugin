#include "ModalLayer.h"
#include <algorithm>

#include "core/Blur.h"
#include "core/Help.h"
#include "core/Theme.h"

namespace t3k::ui {

ModalLayer::ModalLayer(Backdrop backdrop) : backdrop_(std::move(backdrop)) {
  setOpaque(true);
  setWantsKeyboardFocus(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
}

void ModalLayer::focusFirstControl() {
  focusRequested_ = true;
  pendingFocus_ = true;
  if (!isShowing() || getPeer() == nullptr || !getPeer()->isFocused()) return;
  const auto order = juce::KeyboardFocusTraverser().getAllComponents(this);
  if (order.empty()) grabKeyboardFocus();
  else order.front()->grabKeyboardFocus();
  pendingFocus_ = !hasKeyboardFocus(true);
  if (!pendingFocus_) help::announce(getTitle() + ". " + getDescription());
}

void ModalLayer::timerCallback() {
  refresh();
  const auto* focused = getCurrentlyFocusedComponent();
  const bool hostHasFocus = focused == nullptr || focused->isParentOf(this);
  if (pendingFocus_ || (focusRequested_ && hostHasFocus)) focusFirstControl();
}

bool ModalLayer::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    if (onEscape) onEscape();
    return true;
  }
  if (!key.isKeyCode(juce::KeyPress::tabKey)) return false;
  const auto order = juce::KeyboardFocusTraverser().getAllComponents(this);
  if (order.empty()) return true;
  const auto at = std::find(order.begin(), order.end(), getCurrentlyFocusedComponent());
  const int count = static_cast<int>(order.size());
  const int index = at == order.end() ? (key.getModifiers().isShiftDown() ? 0 : -1)
                                    : static_cast<int>(at - order.begin());
  order[static_cast<size_t>((index + (key.getModifiers().isShiftDown() ? count - 1 : 1)) % count)]->grabKeyboardFocus();
  return true;
}

std::unique_ptr<juce::AccessibilityHandler> ModalLayer::createAccessibilityHandler() {
  return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::dialogWindow);
}

ModalLayer::~ModalLayer() { stopTimer(); }

void ModalLayer::setContent(juce::Component& content) {
  content_ = &content;
  addAndMakeVisible(content);
  resized();
}

void ModalLayer::visibilityChanged() {
  if (isVisible()) {
    refresh();
    startTimer(kRefreshMs);
  } else {
    stopTimer();
    blurred_ = juce::Image();
  }
}

void ModalLayer::refresh() {
  if (!backdrop_ || getWidth() <= 0) return;
  blurred_ = backdrop_(kSnapshotScale);
  blurImage(blurred_, juce::roundToInt(kBlurPx * kSnapshotScale));
  repaint();
}

void ModalLayer::paint(juce::Graphics& g) {
  if (blurred_.isValid()) {
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImage(blurred_, getLocalBounds().toFloat());
  } else {
    g.fillAll(theme::kBlack);
  }
  g.fillAll(juce::Colours::black.withAlpha(kScrimAlpha));
}

void ModalLayer::resized() {
  centreContent();
  if (isVisible()) refresh();
}

void ModalLayer::childBoundsChanged(juce::Component* child) {
  if (child == content_) centreContent();
}

void ModalLayer::centreContent() {
  if (content_ == nullptr) return;
  // The content keeps the size it gave itself (the design is never narrower
  // than a dialog plus padding); it is only ever moved.
  const auto area = getLocalBounds().reduced(kPad);
  const auto target = area.getCentre() - juce::Point<int>(content_->getWidth() / 2, content_->getHeight() / 2);
  if (target != content_->getPosition()) content_->setTopLeftPosition(target);
}

}  // namespace t3k::ui
