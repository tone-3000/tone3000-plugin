#include "Pointer.h"

#include "core/Design.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace t3k::ui {

namespace {

// Touch is the primary input until a mouse says otherwise. Windows knows a
// tablet from a laptop (a convertible with its keyboard off is in slate
// mode) and whether the screen itself is a digitizer; a machine without the
// sensor reports slate too, so the digitizer check keeps a plain desktop on
// the mouse. Linux has no such answer short of enumerating XInput devices,
// so a Linux tablet turns the affordances on with its first touch.
bool touchFirst() {
  if (design::kCoarsePointer) return true;
#if JUCE_WINDOWS
  const bool integratedTouch = (GetSystemMetrics(SM_DIGITIZER) & NID_INTEGRATED_TOUCH) != 0;
  return integratedTouch && GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0;
#else
  return false;
#endif
}

}  // namespace

Pointer::Pointer() : coarse_(touchFirst()) {}

void Pointer::sawInput(bool touch) {
  // A touch platform is touch throughout (an iPad's trackpad included, as
  // the web treated it).
  if (design::kCoarsePointer || touch == coarse_) return;
  coarse_ = touch;
  listeners_.call([](Listener& l) { l.pointerChanged(); });
}

PointerTracker::PointerTracker(Pointer& pointer, juce::Component& root) : pointer_(pointer), root_(root) {
  root_.addMouseListener(this, true);
}

PointerTracker::~PointerTracker() { root_.removeMouseListener(this); }

}  // namespace t3k::ui
