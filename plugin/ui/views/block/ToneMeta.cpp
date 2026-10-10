#include "ToneMeta.h"

#include <cmath>

#include "core/CustomIcons.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Labels.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
// Icon + count, the ToneBrowser stat pattern: 16px GRAY glyph, 8px gap,
// 14px MUTED number.
int statWidth(const juce::String& count) {
  return 16 + 8 + juce::roundToInt(Fonts::width(Fonts::sans(14), count));
}

void paintStat(juce::Graphics& g, juce::Rectangle<int> row, int x, Icon icon, const char* svg, juce::Colour iconColour,
               const juce::String& count) {
  const auto glyph = juce::Rectangle<float>(16, 16).withCentre({x + 8.0f, row.toFloat().getCentreY()});
  if (svg != nullptr)
    Icons::draw(g, svg, glyph, iconColour);
  else
    Icons::draw(g, icon, glyph, iconColour);
  paint::text(g, count, {x + 16 + 8, row.getY(), 200, row.getHeight()}, Fonts::sans(14), theme::kMuted);
}
}  // namespace

// Bookmark tally as a toggle (signed in): outline idle, white fill when
// favorited; the whole icon + count is the button.
class ToneMeta::BookmarkButton : public Clickable {
public:
  BookmarkButton() : Clickable("Bookmark") {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void set(int count, bool favorited) {
    count_ = labels::count(count);
    favorited_ = favorited;
    setHelpText(help::text(favorited ? help::Key::unfavoriteTone : help::Key::favoriteTone));
    setSize(statWidth(count_), 16);
    repaint();
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    paintStat(g, getLocalBounds(), 0, Icon::Bookmark, favorited_ ? custom_icons::kBookmarkFilled : nullptr,
              favorited_ ? theme::kWhite : theme::kGray, count_);
  }

private:
  juce::String count_;
  bool favorited_ = false;
};

ToneMeta::ToneMeta(ImageLoader& images)
    : images_(images), bookmark_(std::make_unique<BookmarkButton>()), info_(std::make_unique<BlockInfoPanel>()) {
  addAndMakeVisible(badge_);
  bookmark_->onClick = [this] {
    if (onToggleFavorite) onToggleFavorite();
  };
  addAndMakeVisible(*bookmark_);
  addChildComponent(avatar_);
  info_->onHeightChanged = [this] {
    builtWidth_ = -1;
    if (onHeightChanged) onHeightChanged();
  };
  addChildComponent(*info_);
}

ToneMeta::~ToneMeta() = default;

void ToneMeta::setTone(const ToneSummary& tone) {
  const bool avatarChanged = !tone_.user || !tone.user || tone_.user->avatarUrl != tone.user->avatarUrl;
  tone_ = tone;
  badge_.setFormat(labels::format(tone_.format), tone_.isNam());
  if (avatarChanged) loadAvatar();
  builtWidth_ = -1;
  if (getWidth() > 0) rebuild(getWidth());
  repaint();
}

void ToneMeta::setCounts(const Counts& counts) {
  counts_ = counts;
  bookmark_->set(counts_.favorites, counts_.favorited);
  bookmark_->setInterceptsMouseClicks(counts_.favoriteToggle, false);
  bookmark_->setMouseCursor(counts_.favoriteToggle ? juce::MouseCursor::PointingHandCursor
                                                   : juce::MouseCursor::NormalCursor);
  if (!counts_.favoriteToggle) bookmark_->setHelpText({});
  repaint();
}

void ToneMeta::setInfoVisible(bool visible) {
  if (infoVisible_ == visible) return;
  infoVisible_ = visible;
  info_->setVisible(visible);
  builtWidth_ = -1;
  if (getWidth() > 0) rebuild(getWidth());
}

void ToneMeta::loadAvatar() {
  avatar_.setImage(images_, tone_.user ? tone_.user->avatarUrl : juce::String());
}

int ToneMeta::heightFor(int width) {
  if (width != builtWidth_) rebuild(width);
  return contentHeight_;
}

void ToneMeta::rebuild(int width) {
  builtWidth_ = width;
  if (width <= 0) return;
  int y = 0;

  // Title: 18px bold, line-height 1.4, two lines max.
  title_ = std::make_unique<TextFlow>(Fonts::sans(kTitlePx, true), kTitleLine, tone_.title, static_cast<float>(width));
  const int titleH = static_cast<int>(std::ceil(title_->clampedHeight(kTitleLines)));
  titleBox_ = {0, y, width, titleH};
  y += titleH;

  // Gear label + badge (centred row, as tall as the badge).
  const auto gearLabel = labels::gear(tone_.gear);
  const bool hasGearRow = gearLabel.isNotEmpty() || badge_.isVisible();
  if (hasGearRow) {
    y += kTitleGap;
    const int rowH = std::max(Fonts::normalLineHeight(kBodyPx), badge_.isVisible() ? badge_.getHeight() : 0);
    gearRow_ = {0, y, width, rowH};
    int x = 0;
    if (gearLabel.isNotEmpty()) x += juce::roundToInt(Fonts::width(Fonts::sans(kBodyPx), gearLabel)) + kGearRowGap;
    badge_.setTopLeftPosition(x, gearRow_.getY() + (rowH - badge_.getHeight()) / 2);
    y += rowH;
  } else {
    gearRow_ = {};
  }

  // Counts (catalog tones only).
  if (!tone_.local) {
    y += kGroupGap;
    statsRow_ = {0, y, width, kStatIcon};
    const int downloadsW = statWidth(labels::count(counts_.downloads));
    bookmark_->setTopLeftPosition(downloadsW + kStatsGap, y);
    y += kStatIcon;
  } else {
    statsRow_ = {};
  }
  bookmark_->setVisible(!tone_.local);

  // Creator line.
  if (tone_.user) {
    y += kGroupGap;
    creatorRow_ = {0, y, width, kAvatar};
    avatar_.setBounds(0, y, kAvatar, kAvatar);
    y += kAvatar;
  } else {
    creatorRow_ = {};
  }
  avatar_.setVisible(tone_.user.has_value());

  if (infoVisible_) {
    y += kInfoGap;
    const int h = info_->heightFor(width);
    info_->setBounds(0, y, width, h);
    y += h;
  }
  contentHeight_ = y;
}

void ToneMeta::resized() {
  if (getWidth() != builtWidth_) rebuild(getWidth());
}

void ToneMeta::paint(juce::Graphics& g) {
  if (title_) title_->draw(g, titleBox_.getTopLeft().toFloat(), theme::kWhite, kTitleLines, /*ellipsis=*/true);

  if (!gearRow_.isEmpty()) {
    const auto gearLabel = labels::gear(tone_.gear);
    if (gearLabel.isNotEmpty()) paint::text(g, gearLabel, gearRow_, Fonts::sans(kBodyPx), theme::kMuted);
  }

  if (!statsRow_.isEmpty()) {
    int x = 0;
    const auto downloads = labels::count(counts_.downloads);
    paintStat(g, statsRow_, x, Icon::Download, nullptr, theme::kGray, downloads);
    x += statWidth(downloads) + kStatsGap;
    x += bookmark_->getWidth() + kStatsGap;
    paintStat(g, statsRow_, x, Icon::FolderClosed, nullptr, theme::kGray, labels::count(counts_.models));
  }

  if (!creatorRow_.isEmpty() && tone_.user) {
    const auto font = Fonts::sans(kBodyPx);
    const int x = kAvatar + kCreatorGap;
    const int nameW = juce::roundToInt(Fonts::width(font, tone_.user->username));
    paint::text(g, tone_.user->username, {x, creatorRow_.getY(), nameW + 1, creatorRow_.getHeight()}, font, theme::kGray);
    const auto ago = labels::timeAgoShort(tone_.publishedAt);
    if (ago.isNotEmpty())
      paint::text(g, juce::String::fromUTF8(" \xC2\xB7 ") + ago,
                  {x + nameW, creatorRow_.getY(), 200, creatorRow_.getHeight()}, font, theme::kMuted);
  }
}

}  // namespace t3k::ui
