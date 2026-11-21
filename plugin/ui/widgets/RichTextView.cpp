#include "RichTextView.h"

#include <cmath>

namespace t3k::ui {

// Keyboard/screen-reader targets for the already painted inline links.
// Mouse hit testing and painting remain with RichFlow.
class RichTextView::AccessibleLink : public juce::Component {
public:
  AccessibleLink(RichTextView& owner, const TextRun& run) : owner_(owner), href_(run.href) {
    setTitle(run.text);
    setDescription(run.href);
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(false, false);
  }
  bool keyPressed(const juce::KeyPress& key) override {
    if (key != juce::KeyPress::returnKey) return false;
    activate();
    return true;
  }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::hyperlink,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { activate(); }));
  }
private:
  void activate() { if (owner_.onLink) owner_.onLink(href_); }
  RichTextView& owner_;
  juce::String href_;
};

juce::String RichTextView::plainText() const {
  juce::String text;
  for (const auto& run : runs_) text += run.text;
  return text;
}

std::unique_ptr<juce::AccessibilityHandler> RichTextView::createAccessibilityHandler() {
  return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::staticText);
}

RichTextView::RichTextView(float px, float lineHeightPx, juce::Colour colour, juce::Justification align)
    : px_(px), lineHeightPx_(lineHeightPx), colour_(colour), align_(align) {}

void RichTextView::setText(RichText runs) {
  if (runs_ == runs) return;
  runs_ = std::move(runs);
  setTitle(plainText());
  links_.clear();
  for (const auto& run : runs_) {
    if (run.href.isEmpty()) continue;
    auto link = std::make_unique<AccessibleLink>(*this, run);
    addAndMakeVisible(*link);
    link->setBounds(getLocalBounds());
    links_.push_back(std::move(link));
  }
  flow_.reset();
  reflow();
  if (!autoHeight_) heightChanged();
}

void RichTextView::setColour(juce::Colour colour) {
  colour_ = colour;
  repaint();
}

void RichTextView::setWidth(int width) {
  setSize(width, getHeight());
  reflow();
}

int RichTextView::lineCount() const { return flow_ ? flow_->lineCount() : 0; }

const RichFlow& RichTextView::flowFor(float width) const {
  if (!flow_ || !juce::exactlyEqual(flowWidth_, width)) {
    flow_ = std::make_unique<RichFlow>(runs_, px_, lineHeightPx_, width);
    flowWidth_ = width;
  }
  return *flow_;
}

float RichTextView::heightFor(float width) const { return flowFor(width).height(); }

void RichTextView::reflow() {
  if (getWidth() <= 0) return;
  const int h = static_cast<int>(std::ceil(flowFor(static_cast<float>(getWidth())).height()));
  if (autoHeight_ && h != getHeight()) setSize(getWidth(), h);
  repaint();
}

void RichTextView::resized() {
  reflow();
  for (auto& link : links_) link->setBounds(getLocalBounds());
}

void RichTextView::paint(juce::Graphics& g) {
  if (flow_) flow_->draw(g, {0, subpixelTop()}, colour_, align_);
}

juce::String RichTextView::linkAt(juce::Point<int> p) const {
  return flow_ ? flow_->linkAt(p.toFloat(), {0, 0}, align_) : juce::String();
}

void RichTextView::mouseMove(const juce::MouseEvent& e) {
  setMouseCursor(linkAt(e.getPosition()).isNotEmpty() ? juce::MouseCursor::PointingHandCursor
                                                      : juce::MouseCursor::NormalCursor);
}

void RichTextView::mouseUp(const juce::MouseEvent& e) {
  if (!getLocalBounds().contains(e.getPosition()) || e.mouseWasDraggedSinceMouseDown()) return;
  const auto href = linkAt(e.getPosition());
  if (href.isNotEmpty() && onLink) onLink(href);
}

}  // namespace t3k::ui
