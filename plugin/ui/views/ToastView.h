// The toast pill (port of Toast.tsx's view half): white, black bold text,
// centred horizontally above the faceplate.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Toast.h"

namespace t3k::ui {

class ToastView : public juce::Component, private Toast::Listener {
public:
  explicit ToastView(Toast& toast);
  ~ToastView() override;

  // Distance from the parent's bottom edge to the pill's bottom.
  void setBottomOffset(int bottom);
  void paint(juce::Graphics& g) override;
  void parentSizeChanged() override { layout(); }

private:
  void toastChanged() override;
  void layout();

  Toast& toast_;
  int bottom_ = 0;
};

}  // namespace t3k::ui
