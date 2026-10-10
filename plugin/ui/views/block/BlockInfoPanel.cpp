#include "BlockInfoPanel.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
constexpr const char* kSignedOutPrompt = "Sign in to TONE3000 to see tone details.";

// Makes / tags come straight off the network: trimmed non-empty names only.
std::vector<juce::String> clean(const std::vector<juce::String>& names) {
  std::vector<juce::String> out;
  for (const auto& n : names)
    if (n.trim().isNotEmpty()) out.push_back(n.trim());
  return out;
}

int ceilInt(float v) { return static_cast<int>(std::ceil(v)); }
}  // namespace

// MORE / LESS: mono 14px uppercase + a 16px chevron, gap 8.
class BlockInfoPanel::MoreButton : public Clickable {
public:
  MoreButton() : Clickable("More") {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setExpanded(false);
  }
  void setExpanded(bool expanded) {
    expanded_ = expanded;
    setButtonText(expanded ? "Less" : "More");
    setSize(juce::roundToInt(Fonts::width(font(), label())) + kGap + kChevron, juce::roundToInt(kLineHeight));
    repaint();
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds();
    const int textW = juce::roundToInt(Fonts::width(font(), label()));
    paint::text(g, label(), box.withWidth(textW + 1), font(), theme::kWhite);
    Icons::draw(g, expanded_ ? Icon::ChevronUp : Icon::ChevronDown,
                juce::Rectangle<float>(kChevron, kChevron)
                    .withCentre({static_cast<float>(textW + kGap) + kChevron / 2.0f, box.toFloat().getCentreY()}),
                theme::kWhite);
  }

private:
  static constexpr int kGap = 8, kChevron = 16;
  juce::String label() const { return expanded_ ? "LESS" : "MORE"; }
  static juce::Font font() { return Fonts::mono(kBodyPx); }
  bool expanded_ = false;
};

BlockInfoPanel::BlockInfoPanel()
    : prompt_(std::make_unique<PillButton>("Log In", PillButton::Style::filled)),
      more_(std::make_unique<MoreButton>()),
      view_(std::make_unique<PillButton>("View on TONE3000", PillButton::Style::outline)) {
  prompt_->onClick = [this] {
    if (!state_.authenticated) {
      if (onLogin) onLogin();
    } else if (onRetry) {
      onRetry();
    }
  };
  addChildComponent(*prompt_);

  more_->onClick = [this] {
    descExpanded_ = !descExpanded_;
    more_->setExpanded(descExpanded_);
    builtWidth_ = -1;
    if (onHeightChanged) onHeightChanged();
  };
  addChildComponent(*more_);

  view_->setMetrics({16, 8, 14, 8});
  view_->setCornerRadius(8.0f);
  view_->setLeadingIcon(Icon::ExternalLink, 16);
  view_->setHelpText(help::text(help::Key::viewOnT3k));
  view_->onClick = [this] {
    if (onOpenUrl) onOpenUrl(state_.pageUrl);
  };
  addChildComponent(*view_);
}

BlockInfoPanel::~BlockInfoPanel() = default;

void BlockInfoPanel::setState(State state) {
  // A new description collapses back to the clamp (DescriptionBlock's
  // effect on `text`).
  const auto oldDesc = state_.tone ? state_.tone->description : juce::String();
  const auto newDesc = state.tone ? state.tone->description : juce::String();
  if (oldDesc != newDesc) descExpanded_ = false;
  state_ = std::move(state);
  more_->setExpanded(descExpanded_);
  builtWidth_ = -1;
  if (getWidth() > 0) rebuild(getWidth());
  repaint();
}

std::vector<juce::String> BlockInfoPanel::makes() const {
  return state_.tone ? clean(state_.tone->makes) : std::vector<juce::String>{};
}

std::vector<juce::String> BlockInfoPanel::tags() const {
  return state_.tone ? clean(state_.tone->tags) : std::vector<juce::String>{};
}

int BlockInfoPanel::tagHeight() const {
  // 12px text at line-height 1.4 inside 8px pads and the 1px border.
  return ceilInt(kTagPx * 1.4f) + 2 * kTagPadY + 2;
}

int BlockInfoPanel::heightFor(int width) {
  if (width != builtWidth_) rebuild(width);
  return items_.empty() ? 0 : items_.back().bounds.getBottom();
}

BlockInfoPanel::Item& BlockInfoPanel::add(Item::Kind kind, float height) {
  if (!items_.empty()) cursorY_ += kGap;
  Item item;
  item.kind = kind;
  const int top = juce::roundToInt(cursorY_);
  item.bounds = {0, top, builtWidth_, juce::roundToInt(cursorY_ + height) - top};
  items_.push_back(std::move(item));
  cursorY_ += height;
  return items_.back();
}

// Section title, followed by the section's own 8px gap instead of the stack's 24.
void BlockInfoPanel::addTitle(const juce::String& text) {
  add(Item::Kind::title, kLineHeight).text = text;
  cursorY_ += kSectionGap - kGap;
}

void BlockInfoPanel::rebuild(int width) {
  builtWidth_ = width;
  cursorY_ = 0;
  items_.clear();
  prompt_->setVisible(false);
  more_->setVisible(false);
  view_->setVisible(false);
  if (width <= 0) return;

  const float w = static_cast<float>(width);
  const auto body = Fonts::sans(kBodyPx);

  add(Item::Kind::hairline, 1);

  const bool signedOut = !state_.authenticated;
  const bool errored = !signedOut && state_.error.isNotEmpty();
  const auto description = state_.tone ? state_.tone->description.trim() : juce::String();
  const auto makeNames = makes();
  const auto tagNames = tags();
  const bool hasSections = description.isNotEmpty() || !makeNames.empty() || !tagNames.empty();

  if (signedOut || errored) {
    // Centred copy, 16px gap, filled pill; 8px pads top and bottom.
    auto flow = std::make_unique<TextFlow>(body, kLineHeight, signedOut ? kSignedOutPrompt : state_.error, w);
    const float textH = flow->height();
    prompt_->setLabel(signedOut ? "Log In" : "Try again");
    prompt_->setHelpText(signedOut ? help::text(help::Key::toneInfoLogin) : juce::String());
    const float itemH = kPromptPadY + textH + kPromptGap + prompt_->getHeight() + kPromptPadY;
    auto& item = add(Item::Kind::prompt, itemH);
    item.flows.push_back(std::move(flow));
    const float itemTop = cursorY_ - itemH;
    prompt_->setTopLeftPosition((width - prompt_->getWidth()) / 2,
                                juce::roundToInt(itemTop + kPromptPadY + textH + kPromptGap));
    prompt_->setVisible(true);
  } else if (hasSections) {
    if (description.isNotEmpty()) {
      addTitle("Description");
      auto flow = std::make_unique<TextFlow>(body, kLineHeight, description, w, /*preLine=*/true);
      const bool overflows = flow->overflows(kDescClampLines);
      const int lines = descExpanded_ ? flow->lineCount() : juce::jmin(kDescClampLines, flow->lineCount());
      auto& item = add(Item::Kind::body, kLineHeight * static_cast<float>(lines));
      item.maxLines = descExpanded_ ? 0 : kDescClampLines;
      item.flows.push_back(std::move(flow));
      if (overflows) {
        cursorY_ += kSectionGap - kGap;
        auto& more = add(Item::Kind::control, more_->getHeight());
        more_->setTopLeftPosition(0, more.bounds.getY());
        more_->setVisible(true);
      }
    }
    if (!makeNames.empty()) {
      addTitle("Makes & Models");
      // A make's name can pack several lines; each gets its own row.
      std::vector<juce::String> rows;
      for (const auto& make : makeNames) {
        juce::StringArray parts;
        parts.addLines(make);
        for (const auto& p : parts)
          if (p.trim().isNotEmpty()) rows.push_back(p.trim());
      }
      Item staged;
      int ry = 0;
      for (const auto& row : rows) {
        auto flow = std::make_unique<TextFlow>(body, kLineHeight, row, w);
        const int fh = ceilInt(flow->height());
        staged.boxes.emplace_back(0, ry, width, fh);
        staged.flows.push_back(std::move(flow));
        ry += fh + kMakesGap;
      }
      auto& item = add(Item::Kind::makes, ry - kMakesGap);
      item.flows = std::move(staged.flows);
      item.boxes = std::move(staged.boxes);
    }
    if (!tagNames.empty()) {
      addTitle("Tags");
      const auto tagFont = Fonts::sans(kTagPx);
      const int th = tagHeight();
      Item staged;
      int cx = 0, cy = 0;
      for (const auto& tag : tagNames) {
        const int cw = juce::roundToInt(Fonts::width(tagFont, tag)) + 2 * kTagPadX + 2;
        if (cx > 0 && cx + cw > width) {  // flex-wrap
          cx = 0;
          cy += th + kTagGap;
        }
        staged.boxes.emplace_back(cx, cy, cw, th);
        staged.labels.push_back(tag);
        cx += cw + kTagGap;
      }
      auto& item = add(Item::Kind::tags, cy + th);
      item.boxes = std::move(staged.boxes);
      item.labels = std::move(staged.labels);
    }
  }

  if (state_.pageUrl.isNotEmpty()) {
    if (signedOut || errored || hasSections) add(Item::Kind::hairline, 1);
    auto& item = add(Item::Kind::control, view_->getHeight());
    view_->setTopLeftPosition(0, item.bounds.getY());
    view_->setVisible(true);
  }
}

void BlockInfoPanel::resized() {
  if (getWidth() != builtWidth_) rebuild(getWidth());
}

void BlockInfoPanel::paint(juce::Graphics& g) {
  for (const auto& item : items_) {
    const auto b = item.bounds;
    const auto origin = b.getTopLeft().toFloat();
    switch (item.kind) {
      case Item::Kind::hairline:
        paint::hairlineH(g, static_cast<float>(b.getX()), static_cast<float>(b.getRight()),
                         static_cast<float>(b.getY()), theme::kBorder);
        break;
      case Item::Kind::prompt:
        item.flows.front()->draw(g, origin.translated(0, kPromptPadY), theme::kWhite, 0, false,
                                 juce::Justification::horizontallyCentred);
        break;
      case Item::Kind::title:
        paint::text(g, item.text, b, Fonts::sans(kBodyPx, true), theme::kWhite);
        break;
      case Item::Kind::body:
        item.flows.front()->draw(g, origin, theme::kGray, item.maxLines);
        break;
      case Item::Kind::makes:
        for (size_t i = 0; i < item.flows.size(); ++i)
          item.flows[i]->draw(g, origin + item.boxes[i].getTopLeft().toFloat(), theme::kGray);
        break;
      case Item::Kind::tags:
        for (size_t i = 0; i < item.boxes.size(); ++i) {
          const auto box = item.boxes[i].translated(b.getX(), b.getY()).toFloat();
          paint::border(g, box, box.getHeight() / 2, theme::kBorder);
          paint::text(g, item.labels[i], box.toNearestInt(), Fonts::sans(kTagPx), theme::kWhite,
                      juce::Justification::centred);
        }
        break;
      case Item::Kind::control:
        break;  // the child button paints itself
    }
  }
}

}  // namespace t3k::ui
