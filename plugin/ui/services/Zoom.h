// The window zoom: the one scale the shell's AffineTransform applies to the
// root (1x..design::kMaxScale, or below 1 when a host squeezes the window).
// Views lay out in design space and never need it; the exception is what
// should hold its size on screen while the window grows and use the room
// for more instead (the tone browser's grid), which counter-scales by it.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui {

class Zoom {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void zoomChanged() = 0;
  };

  double factor() const { return factor_; }

  // The shell, on every fit of the root.
  void set(double factor) {
    if (juce::approximatelyEqual(factor, factor_)) return;
    factor_ = factor;
    listeners_.call([](Listener& l) { l.zoomChanged(); });
  }

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

private:
  double factor_ = 1.0;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
