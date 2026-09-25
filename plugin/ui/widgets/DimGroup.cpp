#include "DimGroup.h"

#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kFadeMs = 200;
}  // namespace

DimGroup::DimGroup() {
  setInterceptsMouseClicks(false, true);
  // Always on top: children the owner adds later (even while off) still
  // land beneath the shield.
  shield_.setInterceptsMouseClicks(true, false);
  shield_.setAccessible(false);
  shield_.setAlwaysOnTop(true);
  addChildComponent(shield_);
}

void DimGroup::setOff(bool off, bool animate) {
  if (off_ == off) return;
  off_ = off;
  // Off: the shield takes the pointer (hints resolve up to the group's own),
  // children see nothing.
  shield_.setVisible(off);
  fade_.animateTo(off ? theme::kDisabledOpacity : 1.0f, kFadeMs, animate);
}

void DimGroup::resized() { shield_.setBounds(getLocalBounds()); }

}  // namespace t3k::ui
