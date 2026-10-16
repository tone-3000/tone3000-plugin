// A loading look that waits before it shows. A local capture prepares in a
// blink, so stepping through a folder's models would flash the artwork dim
// (and a spinner) and straight back on every switch; a load that is still
// going after the grace gets the look as before.
#pragma once

#include "core/DelayedCall.h"

#include <functional>

namespace t3k::ui {

class BusyGrace {
public:
  static constexpr int kGraceMs = 300;

  // Whether to show the loading look now. A load just started waits: once
  // kGraceMs have passed, `show` runs (re-sync the view, which asks again).
  bool shown(bool busy, std::function<void()> show) {
    if (!busy) {
      wait_.cancel();
      shown_ = false;
      return false;
    }
    if (!shown_ && !wait_.pending())
      wait_.start(kGraceMs, [this, show = std::move(show)] {
        shown_ = true;
        if (show) show();
      });
    return shown_;
  }

private:
  DelayedCall wait_;
  bool shown_ = false;
};

}  // namespace t3k::ui
