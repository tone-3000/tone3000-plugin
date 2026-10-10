// Full-window modal chrome shared by the update notice, the connection
// modal and the OAuth overlay: CSS `rgba(0,0,0,.5)` over `backdrop-filter:
// blur(4px)`, content centred inside 24px padding, every press swallowed.
//
// The blur is a downscaled snapshot of what lies beneath the overlay layer
// (the host provides it), re-taken a few times a second so the meters keep
// moving softly behind the dialog as they did in the browser. At half
// resolution with a 2px kernel the refresh costs a couple of milliseconds.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

class ModalLayer : public juce::Component, private juce::Timer {
public:
  static constexpr int kPad = 24;
  static constexpr float kBlurPx = 4;
  static constexpr float kSnapshotScale = 0.5f;
  static constexpr int kRefreshMs = 125;
  static constexpr float kScrimAlpha = 0.5f;

  // Snapshot of everything beneath the overlay layer at `scale`.
  using Backdrop = std::function<juce::Image(float scale)>;

  explicit ModalLayer(Backdrop backdrop);
  ~ModalLayer() override;

  // The dialog body; it sizes itself and is kept centred.
  void setContent(juce::Component& content);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void childBoundsChanged(juce::Component* child) override;
  void visibilityChanged() override;
  void mouseDown(const juce::MouseEvent&) override {}  // swallow
  // A dialog to screen readers; the subclass names it with setTitle().
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  void timerCallback() override { refresh(); }
  void refresh();
  void centreContent();

  Backdrop backdrop_;
  juce::Image blurred_;
  juce::Component* content_ = nullptr;
};

}  // namespace t3k::ui
