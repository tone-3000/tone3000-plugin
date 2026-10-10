#include "UpdateNotice.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
struct RemindOption {
  int days;
  const char* label;
};
constexpr RemindOption kRemindOptions[] = {{1, "1 day"}, {7, "7 days"}, {30, "30 days"}};

// Bare underlined text button (`background: transparent; border: none;
// text-decoration: underline`).
class LinkButton : public Clickable {
public:
  LinkButton(const juce::String& label, float px) : Clickable(label), label_(label), font_(Fonts::sans(px)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setSize(static_cast<int>(std::ceil(Fonts::width(font_, label_))), Fonts::normalLineHeight(font_));
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    g.setColour(theme::kWhite);
    const float baseline = Fonts::cssBaseline(font_, static_cast<float>(getHeight()));
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font_, label_, 0, baseline);
    glyphs.draw(g);
    g.fillRect(juce::Rectangle<float>(0, baseline + font_.getDescent() * 0.35f, Fonts::width(font_, label_), 1.0f));
  }

private:
  juce::String label_;
  juce::Font font_;
};
}  // namespace

class UpdateNotice::Card : public juce::Component {
public:
  Card(UpdateNotice& owner, const UpdateInfo& info)
      : owner_(owner),
        title_("Update available: v" + info.version),
        message_(kMessagePx, kMessageLineHeight, theme::kMuted, juce::Justification::centred),
        download_("Download update", PillButton::Style::filled),
        close_(Icon::X, theme::kIconBoxSize, 16) {
    setOpaque(false);
    message_.setText(Html::toRichText(info.messageHtml));
    message_.onLink = [](const juce::String& href) { juce::URL(href).launchInDefaultBrowser(); };
    download_.onClick = [url = info.url] { juce::URL(url).launchInDefaultBrowser(); };
    close_.setActive(false);
    close_.setName("Close");
    close_.onClick = [this] { owner_.remind(kDismissDays); };
    for (const auto& option : kRemindOptions) {
      auto button = std::make_unique<LinkButton>(option.label, kRemindPx);
      button->onClick = [this, days = option.days] { owner_.remind(days); };
      addAndMakeVisible(*button);
      remind_.push_back(std::move(button));
    }
    addAndMakeVisible(message_);
    addAndMakeVisible(download_);
    addAndMakeVisible(close_);
    layout();
  }

  // Content-driven height: 1px border, 24px padding, 16px gaps.
  void layout() {
    const int contentW = kCardW - 2 * (1 + kCardPad);
    message_.setWidth(juce::jmin(kMessageMaxW, contentW));
    const int titleH = Fonts::normalLineHeight(kTitlePx);
    const int remindH = Fonts::normalLineHeight(kRemindPx);
    const int h = 2 * (1 + kCardPad) + titleH + kGap + message_.getHeight() + kGap + download_.getHeight() + kGap +
                  remindH;
    setSize(kCardW, h);
  }

  void resized() override {
    auto column = getLocalBounds().reduced(1 + kCardPad);
    close_.setTopRightPosition(getWidth() - 1 - kCloseInset, 1 + kCloseInset);
    titleBox_ = column.removeFromTop(Fonts::normalLineHeight(kTitlePx));
    column.removeFromTop(kGap);
    message_.setTopLeftPosition(column.getCentreX() - message_.getWidth() / 2, column.getY());
    column.removeFromTop(message_.getHeight() + kGap);
    download_.setTopLeftPosition(column.getCentreX() - download_.getWidth() / 2, column.getY());
    column.removeFromTop(download_.getHeight() + kGap);
    // "Remind me in" + three links, 10px apart, centred as one row.
    const auto remindFont = Fonts::sans(kRemindPx);
    remindLabelW_ = static_cast<int>(std::ceil(Fonts::width(remindFont, kRemindLabel)));
    int rowW = remindLabelW_;
    for (const auto& b : remind_) rowW += kRemindGap + b->getWidth();
    remindRow_ = column.removeFromTop(Fonts::normalLineHeight(kRemindPx)).withSizeKeepingCentre(rowW, Fonts::normalLineHeight(kRemindPx));
    int x = remindRow_.getX() + remindLabelW_;
    for (const auto& b : remind_) {
      x += kRemindGap;
      b->setTopLeftPosition(x, remindRow_.getY());
      x += b->getWidth();
    }
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, kCardRadius, theme::kSurface);
    paint::border(g, box, kCardRadius, theme::kBorder);
    const auto titleFont = Fonts::sans(kTitlePx);
    g.setColour(theme::kWhite);
    juce::GlyphArrangement title;
    title.addLineOfText(titleFont, title_, titleBox_.getCentreX() - Fonts::width(titleFont, title_) / 2,
                        titleBox_.getY() + Fonts::cssBaseline(titleFont, static_cast<float>(titleBox_.getHeight())));
    title.draw(g);
    const auto remindFont = Fonts::sans(kRemindPx);
    g.setColour(theme::kMuted);
    juce::GlyphArrangement label;
    label.addLineOfText(remindFont, kRemindLabel, static_cast<float>(remindRow_.getX()),
                        remindRow_.getY() + Fonts::cssBaseline(remindFont, static_cast<float>(remindRow_.getHeight())));
    label.draw(g);
  }

private:
  static constexpr const char* kRemindLabel = "Remind me in";

  UpdateNotice& owner_;
  juce::String title_;
  RichTextView message_;
  PillButton download_;
  IconButton close_;
  std::vector<std::unique_ptr<LinkButton>> remind_;
  juce::Rectangle<int> titleBox_, remindRow_;
  int remindLabelW_ = 0;
};

UpdateNotice::UpdateNotice(Backdrop backdrop, const UpdateInfo& info)
    : ModalLayer(std::move(backdrop)), card_(std::make_unique<Card>(*this, info)) {
  setName("update notice");
  setTitle("Update available");
  setContent(*card_);
}

UpdateNotice::~UpdateNotice() = default;

}  // namespace t3k::ui
