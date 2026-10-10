#include "ToneCard.h"

#include <algorithm>
#include <cmath>

#include "core/Brand.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Labels.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
const juce::String kDotSeparator = juce::String::fromUTF8(" \xC2\xB7 ");
// The verified badge beside the creator's name: 14px tall at its 17.5:20.
constexpr float kBadgeHeight = 14;
constexpr float kBadgeWidth = kBadgeHeight * 17.5f / 20.0f;
constexpr float kBadgeGap = 6;
}  // namespace

ToneCard::ToneCard(ImageLoader& images, const Tone& tone)
    : Clickable(tone.title), tone_(tone), image_(images) {

  image_.setCornerRadius(kImageCorner);
  image_.setTone(tone_.images.empty() ? juce::String() : tone_.images.front(), tone_.gear, /*local=*/false);
  addAndMakeVisible(image_);

  // The A2 mark only where the plugin can actually load the tone; NAM cards
  // without A2 models render disabled and unmarked.
  badge_.setFormat(labels::format(tone_.format), tone_.isNam() && !unavailable(tone_));
  addAndMakeVisible(badge_);

  addChildComponent(avatar_);
  if (tone_.user) {
    avatar_.setVisible(true);
    avatar_.setImage(images, tone_.user->avatarUrl);
  }
  syncState();
}

ToneCard::~ToneCard() = default;

void ToneCard::setLoading(bool loading) {
  if (loading == loading_) return;
  loading_ = loading;
  if (loading_) {
    busy_ = std::make_unique<BusyOverlay>(BusyOverlay::Align::centre);
    busy_->setCornerRadius(kCorner);
    addAndMakeVisible(*busy_);
    busy_->setBounds(getLocalBounds());
  } else {
    busy_.reset();
  }
  syncState();
}

void ToneCard::setDisabled(bool disabled) {
  if (disabled == disabled_) return;
  disabled_ = disabled;
  syncState();
}

void ToneCard::syncState() {
  setEnabled(!disabled_);
  // JUCE has no not-allowed cursor; the dimmed card carries the meaning.
  setMouseCursor(disabled_ ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
  // opacity: DISABLED_OPACITY while disabled and not the card being picked.
  setAlpha(disabled_ && !loading_ ? theme::kDisabledOpacity : 1.0f);
  repaint();
}

float ToneCard::contentHeightFor(int width) {
  measure(width);
  return 2 * kPad + std::max(static_cast<float>(kImage), columnHeight_);
}

void ToneCard::setContentHeight(float height) {
  contentHeight_ = height;
  resized();
}

void ToneCard::measure(int width) {
  if (width == builtWidth_) return;
  builtWidth_ = width;
  const float colW = static_cast<float>(width - 2 * kPad - kImage - kGapX);
  title_ = std::make_unique<TextFlow>(Fonts::sans(kTitlePx, /*bold=*/true), kTitleLine, tone_.title, colW);
  const float titleH = title_->clampedHeight(kTitleLines);
  const float gearH = static_cast<float>(std::max(Fonts::normalLineHeight(kBodyPx), badge_.isVisible() ? badge_.getHeight() : 0));
  const float statsH = static_cast<float>(std::max(Fonts::normalLineHeight(kBodyPx), kStatIcon));
  columnHeight_ = titleH + kRowGap + gearH + kRowGap + statsH + (tone_.user ? kRowGap + kAvatar : 0);
}

// The grid stretches every card in a row to the tallest; the image and the
// text column each centre in that (fractional) height, align-items: center.
void ToneCard::resized() {
  measure(getWidth());
  const float x = kPad + kImage + kGapX;
  const float colW = static_cast<float>(getWidth() - 2 * kPad - kImage - kGapX);
  const float rowH = (contentHeight_ > 0 ? contentHeight_ : static_cast<float>(getHeight())) - 2 * kPad;
  image_.setBounds(kPad, kPad + design::snap((rowH - kImage) / 2), kImage, kImage);

  float y = kPad + (rowH - columnHeight_) / 2;
  const float titleH = title_->clampedHeight(kTitleLines);
  const float gearH = static_cast<float>(std::max(Fonts::normalLineHeight(kBodyPx), badge_.isVisible() ? badge_.getHeight() : 0));
  const float statsH = static_cast<float>(std::max(Fonts::normalLineHeight(kBodyPx), kStatIcon));
  titleBox_ = {x, y, colW, titleH};
  y += titleH + kRowGap;
  gearRow_ = {x, y, colW, gearH};
  y += gearH + kRowGap;
  statsRow_ = {x, y, colW, statsH};
  y += statsH;
  creatorRow_ = tone_.user ? juce::Rectangle<float>(x, y + kRowGap, colW, static_cast<float>(kAvatar)) : juce::Rectangle<float>();

  // Gear label shrinks first; the badge keeps its width (flex-shrink 0).
  const auto gearLabel = labels::gear(tone_.gear);
  const int badgeW = badge_.isVisible() ? badge_.getWidth() : 0;
  const float labelW = gearLabel.isEmpty() ? 0.0f : Fonts::width(Fonts::sans(kBodyPx), gearLabel);
  const float labelMax = gearRow_.getWidth() - (badgeW > 0 ? badgeW + kGearGap : 0);
  const float badgeX = gearRow_.getX() + (gearLabel.isEmpty() ? 0.0f : std::min(labelW, labelMax) + kGearGap);
  badge_.setTopLeftPosition(design::snap(badgeX),
                            design::snap(gearRow_.getY() + (gearRow_.getHeight() - badge_.getHeight()) / 2.0f));

  if (tone_.user) avatar_.setBounds(juce::roundToInt(creatorRow_.getX()), juce::roundToInt(creatorRow_.getY()), kAvatar, kAvatar);
  if (busy_) busy_->setBounds(getLocalBounds());
}

void ToneCard::paintButton(juce::Graphics& g, bool, bool) {
  paint::fill(g, getLocalBounds().toFloat(), kCorner, theme::kSurface);
  // The image box's raised backdrop shows through until artwork paints.
  paint::fill(g, image_.getBounds().toFloat(), kImageCorner, theme::kSurfaceRaised);

  if (title_) title_->draw(g, titleBox_.getTopLeft(), theme::kWhite, kTitleLines, /*ellipsis=*/true);

  const auto body = Fonts::sans(kBodyPx);
  const float line = static_cast<float>(Fonts::normalLineHeight(kBodyPx));

  // Gear label, ellipsised beside the badge.
  const auto gearLabel = labels::gear(tone_.gear);
  if (gearLabel.isNotEmpty()) {
    const int badgeW = badge_.isVisible() ? badge_.getWidth() : 0;
    const float labelMax = gearRow_.getWidth() - (badgeW > 0 ? badgeW + kGearGap : 0);
    paint::cssLine(g, gearLabel, gearRow_.getX(), gearRow_.getY() + (gearRow_.getHeight() - line) / 2, line, labelMax, body,
                   theme::kMuted);
  }

  // Counts (CountStat): 14px glyph, 6px gap, 13px number, all MUTED.
  {
    float x = statsRow_.getX();
    const float cy = statsRow_.getCentreY();
    const float textTop = statsRow_.getY() + (statsRow_.getHeight() - line) / 2;
    auto stat = [&](Icon icon, const juce::String& count) {
      Icons::draw(g, icon, juce::Rectangle<float>(kStatIcon, kStatIcon).withCentre({x + kStatIcon / 2.0f, cy}), theme::kMuted);
      x += kStatIconWidth;
      x += paint::cssLine(g, count, x, textTop, line, 200, body, theme::kMuted) + kStatsGap;
    };
    stat(Icon::Download, labels::count(tone_.downloadsCount));
    stat(Icon::FolderClosed, labels::count(tone_.catalogModelCount()));
  }

  // Creator: avatar, name, the verified badge for verified creators, and the
  // time ago (dot-separated when there is no badge between), one line: the
  // name ellipsises first, the rest keeps its width.
  if (tone_.user) {
    const auto& user = *tone_.user;
    const auto ago = labels::timeAgoShort(tone_.publishedAt);
    const float lineTop = creatorRow_.getY() + (creatorRow_.getHeight() - line) / 2;
    float x = creatorRow_.getX() + kAvatar + kCreatorGap;
    const float agoW = ago.isNotEmpty() ? Fonts::width(body, ago) : 0.0f;
    const float badgeW = user.isVerified ? kBadgeGap + kBadgeWidth : 0.0f;
    const float sepW = !user.isVerified && ago.isNotEmpty() ? Fonts::width(body, kDotSeparator) : 0.0f;
    const float nameMax = creatorRow_.getRight() - x - badgeW - sepW - agoW - (ago.isNotEmpty() ? kBadgeGap : 0.0f);
    x += paint::cssLine(g, user.name(), x, lineTop, line, std::max(0.0f, nameMax), body, theme::kMuted);
    if (user.isVerified) {
      x += kBadgeGap;
      Brand::drawVerifiedBadge(g, juce::Rectangle<float>(kBadgeWidth, kBadgeHeight).withCentre({x + kBadgeWidth / 2, creatorRow_.getCentreY()}));
      x += kBadgeWidth;
    }
    if (ago.isNotEmpty()) {
      if (!user.isVerified) x += paint::cssLine(g, kDotSeparator, x, lineTop, line, sepW + 1, body, theme::kMuted);
      else x += kBadgeGap;
      paint::cssLine(g, ago, x, lineTop, line, agoW + 1, body, theme::kMuted);
    }
  }
}

}  // namespace t3k::ui
