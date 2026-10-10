// "Update available" dialog (UpdateNotice.tsx), shown at most once per
// editor-open when the startup version check finds a newer published build.
// A 420px SURFACE card: title, the remote message (formatting tags only,
// links open in the system browser), the filled "Download update" pill and
// a "Remind me in 1 day / 7 days / 30 days" row. Closing without picking is
// the shortest snooze: the notice is deliberately persistent until the user
// updates. One layer below the connection modal, so a connectivity problem
// always wins.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "services/UpdateCheck.h"
#include "widgets/IconButton.h"
#include "widgets/ModalLayer.h"
#include "widgets/PillButton.h"
#include "widgets/RichTextView.h"

namespace t3k::ui {

class UpdateNotice : public ModalLayer {
public:
  static constexpr int kCardW = 420, kCardPad = 24, kCardRadius = 16, kGap = 16;
  static constexpr int kMessageMaxW = 340;
  static constexpr float kTitlePx = 14, kMessagePx = 13, kMessageLineHeight = 13 * 1.5f, kRemindPx = 12;
  static constexpr int kRemindGap = 10, kCloseInset = 12;
  static constexpr int kDismissDays = 1;

  UpdateNotice(Backdrop backdrop, const UpdateInfo& info);
  ~UpdateNotice() override;

  std::function<void(int days)> onRemindLater;

private:
  class Card;
  void remind(int days) {
    if (onRemindLater) onRemindLater(days);
  }

  std::unique_ptr<Card> card_;
};

}  // namespace t3k::ui
