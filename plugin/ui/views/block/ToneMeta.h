// The detail card's tone column (ChainBlock.tsx "Tone info"): title (2-line
// clamp), gear label + format badge, download / bookmark / model counts,
// and the creator line; the info view appends BlockInfoPanel underneath.
// Height follows content and width, so the card asks heightFor(width).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

#include "BlockInfoPanel.h"
#include "core/TextFlow.h"
#include "model/ChainState.h"
#include "services/ImageLoader.h"
#include "widgets/Avatar.h"
#include "widgets/FormatBadge.h"

namespace t3k::ui {

class ToneMeta : public juce::Component {
public:
  explicit ToneMeta(ImageLoader& images);
  ~ToneMeta() override;

  struct Counts {
    int downloads = 0;
    int favorites = 0;
    bool favorited = false;
    int models = 0;
    // Signed in: the bookmark tally is a toggle.
    bool favoriteToggle = false;
  };

  void setTone(const ToneSummary& tone);
  void setCounts(const Counts& counts);
  // Info view: 24px gap then the panel; the tone view hides it.
  void setInfoVisible(bool visible);
  BlockInfoPanel& info() { return *info_; }

  std::function<void()> onToggleFavorite;
  // The info panel grew or shrank: the card re-measures via heightFor().
  std::function<void()> onHeightChanged;

  int heightFor(int width);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class BookmarkButton;
  static constexpr int kGroupGap = 16;
  static constexpr int kTitleGap = 8;
  static constexpr int kInfoGap = 24;
  static constexpr float kTitlePx = 18;
  static constexpr float kTitleLine = kTitlePx * 1.4f;
  static constexpr int kTitleLines = 2;
  static constexpr int kGearRowGap = 16;
  static constexpr int kStatIcon = 16;
  static constexpr int kStatGap = 8;
  static constexpr int kStatsGap = 24;
  static constexpr int kAvatar = 32;
  static constexpr int kCreatorGap = 12;
  static constexpr float kBodyPx = 14;

  void rebuild(int width);
  void loadAvatar();

  ImageLoader& images_;
  ToneSummary tone_;
  Counts counts_;
  bool infoVisible_ = false;
  int builtWidth_ = -1;

  // Laid-out rows (local coordinates).
  std::unique_ptr<TextFlow> title_;
  juce::Rectangle<int> titleBox_, gearRow_, statsRow_, creatorRow_;
  int contentHeight_ = 0;

  FormatBadge badge_;
  std::unique_ptr<BookmarkButton> bookmark_;
  Avatar avatar_;
  std::unique_ptr<BlockInfoPanel> info_;
};

}  // namespace t3k::ui
