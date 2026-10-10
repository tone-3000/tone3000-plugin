#include "SignInScreen.h"

#include <algorithm>
#include <cmath>

#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
using Phase = ToneSession::AuthFlow::Phase;
using DeviceState = ToneSession::AuthFlow::Device::State;

// Copy and casing from the mockups.
constexpr const char* kBrowserTitle = "Sign in with your browser";
constexpr const char* kBrowserCopy = "Browser didn't open? Copy link to sign in, then return here.";
// After AuthFlow::browserProblem's sentence.
constexpr const char* kProblemHelp = "Copy link to sign in, then return here.";
constexpr const char* kOtherDevice = "Sign in on a different device";
constexpr const char* kPhoneTitle = "Sign in on your phone";
constexpr const char* kScan = "Scan the QR code to sign in, then return here.";
constexpr const char* kVisit = "Visit ", *kEnterCode = ", sign in, and enter this code:";
constexpr const char* kOr = "OR";
constexpr const char* kGettingCode = "Getting a code for your phone";
// After AuthFlow::Device::error's sentence.
constexpr const char* kDeviceHelp = "Get a new one, or finish on your browser.";
constexpr const char* kReturning = "Finishing sign-in";
constexpr const char* kCopyLink = "Copy Link", *kCopied = "Copied";
constexpr const char* kLinkCopied = "Sign-in link copied";

// Arial body copy at 14px, titles bold at 16px, both on CSS line-height 1.4;
// the user code in 24px bold mono.
constexpr float kCopyPx = 14, kTitlePx = 16, kCodePx = 24, kLineHeight = 1.4f;
constexpr int kDotsRow = 32;  // the mockup's frame round the dots
constexpr int kGap = 16, kTightGap = 8, kSectionGap = 32, kButtonGap = 12;
constexpr int kCopyIcon = 14, kTitleIcon = 24;

inline float lineHeight(float px) { return px * kLineHeight; }
inline int lines(const RichFlow& flow) { return static_cast<int>(std::ceil(flow.height())); }
}  // namespace

// TextLink
SignInScreen::TextLink::TextLink(const juce::String& label) : Clickable(label) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setSize(static_cast<int>(std::ceil(Fonts::width(Fonts::sans(kCopyPx), label))) + 2,
          static_cast<int>(std::ceil(lineHeight(kCopyPx))));
}

void SignInScreen::TextLink::paintButton(juce::Graphics& g, bool, bool) {
  paint::cssLine(g, getButtonText(), 0, 0, lineHeight(kCopyPx), static_cast<float>(getWidth()), Fonts::sans(kCopyPx),
                 theme::kGray, juce::Justification::horizontallyCentred);
}

// IconTitle
SignInScreen::IconTitle::IconTitle(Icon icon, const juce::String& title) : icon_(icon), title_(title) {
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // announced with the page
  const int textW = static_cast<int>(std::ceil(Fonts::width(Fonts::sans(kTitlePx, true), title_))) + 2;
  setSize(kTitleIcon + kTightGap + textW, std::max(kTitleIcon, static_cast<int>(std::ceil(lineHeight(kTitlePx)))));
}

void SignInScreen::IconTitle::paint(juce::Graphics& g) {
  Icons::draw(g, icon_, juce::Rectangle<float>(0, 0, kTitleIcon, kTitleIcon), theme::kWhite);
  paint::cssLine(g, title_, kTitleIcon + kTightGap, 0, lineHeight(kTitlePx),
                 static_cast<float>(getWidth() - kTitleIcon - kTightGap), Fonts::sans(kTitlePx, true), theme::kWhite);
}

// DeviceCards
SignInScreen::DeviceCards::DeviceCards() {
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // announced with the page
  qr_.setSize(kQrSize, kQrSize);
  addAndMakeVisible(qr_);
  const int orW = static_cast<int>(std::ceil(Fonts::width(Fonts::sans(kTitlePx, true), kOr)));
  setSize(2 * kCardWidth + 2 * kCardGap + orW, kCardHeight);
}

void SignInScreen::DeviceCards::setDevice(const ToneSession::AuthFlow::Device& device) {
  qr_.setText(device.verificationUriComplete);
  const float textW = kCardWidth - 2 * kCardPad;
  scan_ = std::make_unique<RichFlow>(RichText{TextRun::plain(kScan)}, kCopyPx, lineHeight(kCopyPx), textW);
  TextRun uri = TextRun::plain(typedUri(device.verificationUri));
  uri.underline = true;
  visit_ = std::make_unique<RichFlow>(RichText{TextRun::plain(kVisit), uri, TextRun::plain(kEnterCode)}, kCopyPx,
                                      lineHeight(kCopyPx), textW);
  code_ = device.userCode;
  spoken_ = juce::String(kScan) + " Or visit " + uri.text + kEnterCode + " " + code_;
  repaint();
}

void SignInScreen::DeviceCards::resized() { qr_.setTopLeftPosition(kCardPad, kCardPad); }

void SignInScreen::DeviceCards::paint(juce::Graphics& g) {
  const auto left = juce::Rectangle<float>(0, 0, kCardWidth, kCardHeight);
  const auto right = left.withX(static_cast<float>(getWidth() - kCardWidth));
  paint::border(g, left, kCardCorner, theme::kBorder);
  paint::border(g, right, kCardCorner, theme::kBorder);
  // OR, centred in the space between the cards.
  paint::cssLine(g, kOr, left.getRight(), (kCardHeight - lineHeight(kTitlePx)) / 2, lineHeight(kTitlePx),
                 right.getX() - left.getRight(), Fonts::sans(kTitlePx, true), theme::kGray,
                 juce::Justification::horizontallyCentred);
  if (!scan_ || !visit_) return;
  const auto centred = juce::Justification::horizontallyCentred;
  scan_->draw(g, {kCardPad, static_cast<float>(kCardPad + kQrSize + kGap)}, theme::kWhite, centred);
  // The code card's text and code stack centred in it.
  const float codeLine = lineHeight(kCodePx);
  const float stack = visit_->height() + kGap + codeLine;
  const float top = std::round((kCardHeight - stack) / 2);
  visit_->draw(g, {right.getX() + kCardPad, top}, theme::kWhite, centred);
  paint::cssLine(g, code_, right.getX() + kCardPad, top + visit_->height() + kGap, codeLine,
                 kCardWidth - 2.0f * kCardPad, Fonts::mono(kCodePx, true), theme::kWhite, centred);
}

// SignInScreen
SignInScreen::SignInScreen(Services& services)
    : services_(services),
      back_({}, help::Key::signInBack),
      phoneTitle_(Icon::Smartphone, kPhoneTitle),
      otherDevice_(kOtherDevice),
      copyLink_(kCopyLink, PillButton::Style::outline),
      newCode_("New code", PillButton::Style::filled),
      retry_("Try again", PillButton::Style::filled),
      dismiss_("Dismiss", PillButton::Style::outline) {
  setOpaque(true);  // the page, not a scrim: nothing shows through
  setName("sign in");

  back_.onClick = [this] { back(); };
  addAndMakeVisible(back_);
  addChildComponent(dots_);
  addChildComponent(phoneTitle_);
  addChildComponent(deviceCards_);

  copyLink_.setLeadingIcon(Icon::Copy, kCopyIcon);
  copyLink_.setHelpText(help::text(help::Key::signInCopyLink));
  copyLink_.onClick = [this] { copyLink(); };
  otherDevice_.setHelpText(help::text(help::Key::signInPhone));
  otherDevice_.onClick = [this] { services_.session.startDeviceFlow(); };
  addChildComponent(otherDevice_);
  newCode_.setHelpText(help::text(help::Key::signInNewCode));
  newCode_.onClick = [this] { services_.session.startDeviceFlow(); };
  retry_.setHelpText(help::text(help::Key::signInRetry));
  retry_.onClick = [this] { services_.session.retryFlow(); };
  dismiss_.setHelpText(help::text(help::Key::signInDismiss));
  dismiss_.onClick = [this] { services_.session.clearAuthError(); };
  for (auto* b : {&copyLink_, &newCode_, &retry_, &dismiss_}) addChildComponent(*b);

  services_.session.addListener(this);
  rebuild();
}

SignInScreen::~SignInScreen() { services_.session.removeListener(this); }

// Actions
void SignInScreen::back() {
  auto& session = services_.session;
  if (session.authFlow().phase == Phase::error) session.clearAuthError();
  else session.cancelFlow();
}

void SignInScreen::copyLink() {
  juce::SystemClipboard::copyTextToClipboard(services_.session.authFlow().authorizeUrl);
  copyLink_.setLabel(kCopied);
  layoutColumn();
  help::announce(kLinkCopied);
  copiedDelay_.start(kCopiedMs, [this] {
    copyLink_.setLabel(kCopyLink);
    layoutColumn();
  });
}

void SignInScreen::authFlowChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self != nullptr) self->rebuild();
  });
}

// The column
SignInScreen::Row& SignInScreen::addComponent(juce::Component& c, int gapBefore) {
  c.setVisible(true);
  rows_.push_back({gapBefore, c.getHeight(), c.getWidth(), &c, -1, {}});
  return rows_.back();
}

SignInScreen::Row& SignInScreen::addText(const RichText& runs, float px, juce::Colour colour, int maxWidth,
                                         int gapBefore) {
  Text t;
  t.flow = std::make_unique<RichFlow>(runs, px, lineHeight(px), static_cast<float>(maxWidth));
  t.colour = colour;
  t.maxWidth = maxWidth;
  const int h = lines(*t.flow);
  texts_.push_back(std::move(t));
  rows_.push_back({gapBefore, h, maxWidth, nullptr, static_cast<int>(texts_.size()) - 1, {}});
  return rows_.back();
}

SignInScreen::Row& SignInScreen::addButtons(std::vector<PillButton*> buttons, int gapBefore) {
  int w = 0, h = 0;
  for (auto* b : buttons) {
    b->setVisible(true);
    w += (w > 0 ? kButtonGap : 0) + b->getWidth();
    h = std::max(h, b->getHeight());
  }
  rows_.push_back({gapBefore, h, w, nullptr, -1, std::move(buttons)});
  return rows_.back();
}

juce::String SignInScreen::typedUri(const juce::String& uri) {
  auto typed = uri.fromFirstOccurrenceOf("://", false, false);
  if (typed.isEmpty()) typed = uri;
  if (typed.startsWith("www.")) typed = typed.substring(4);
  return typed.trimCharactersAtEnd("/");
}

void SignInScreen::rebuild() {
  const auto& flow = services_.session.authFlow();
  rows_.clear();
  texts_.clear();
  for (juce::Component* c : std::initializer_list<juce::Component*>{&dots_, &phoneTitle_, &deviceCards_, &otherDevice_,
                                                                     &copyLink_, &newCode_, &retry_, &dismiss_})
    c->setVisible(false);
  juce::String headline;  // what a screen reader hears of this state

  if (flow.phase == Phase::error) {
    headline = flow.error.isNotEmpty() ? flow.error : juce::String(kDefaultError);
    addText(headline, kCopyPx, theme::kWhite, kWideMaxWidth, 0);
    addButtons({&retry_, &dismiss_}, kGap);
  } else if (flow.phase == Phase::returning) {
    headline = kReturning;
    addComponent(dots_, 0).height = kDotsRow;
    addText(headline, kCopyPx, theme::kWhite, kCopyMaxWidth, kGap);
  } else if (flow.device && flow.device->state == DeviceState::requesting) {
    headline = kGettingCode;
    addComponent(dots_, 0).height = kDotsRow;
    addText(headline, kCopyPx, theme::kWhite, kCopyMaxWidth, kGap);
  } else if (flow.device && flow.device->state == DeviceState::waiting) {
    // The device page (mockup 13342:46055).
    deviceCards_.setDevice(*flow.device);
    headline = juce::String(kPhoneTitle) + ". " + deviceCards_.spoken();
    addComponent(phoneTitle_, 0);
    addComponent(deviceCards_, kSectionGap);
  } else if (flow.device && flow.device->state == DeviceState::failed) {
    headline = flow.device->error + " " + kDeviceHelp;
    addText(headline, kCopyPx, theme::kWhite, kWideMaxWidth, 0);
    addButtons({&newCode_, &copyLink_}, kGap);
  } else {
    // Waiting on the browser (mockup 13342:46017), the fallbacks under it.
    const juce::String copy = flow.browserProblem.isNotEmpty() ? flow.browserProblem + " " + kProblemHelp
                                                               : juce::String(kBrowserCopy);
    headline = juce::String(kBrowserTitle) + ". " + copy;
    addComponent(dots_, 0).height = kDotsRow;
    addText(RichText{TextRun::strong(kBrowserTitle)}, kTitlePx, theme::kWhite, kWideMaxWidth, kGap);
    addText(copy, kCopyPx, theme::kWhite, kWideMaxWidth, kGap);
    addButtons({&copyLink_}, kGap);
    addComponent(otherDevice_, kSectionGap);
  }

  columnHeight_ = 0;
  for (const auto& row : rows_) columnHeight_ += row.gapBefore + row.height;
  layoutColumn();
  if (headline != announced_) {
    announced_ = headline;
    help::announce(headline);
  }
}

// Layout
void SignInScreen::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  for (const auto& t : texts_)
    t.flow->draw(g, t.box.getTopLeft(), t.colour, juce::Justification::horizontallyCentred);
}

void SignInScreen::resized() {
  const int w = getWidth();
  const int colW = std::min(design::kWidth - 2 * kPadX, w);
  back_.setTopLeftPosition((w - colW) / 2, kPadTop);
  layoutColumn();
}

// The rows stack centred in the space under the ← row (the mockup centres
// the dots and copy on the page).
void SignInScreen::layoutColumn() {
  const int top = kPadTop + BackLink::kHeight;
  const int cx = getWidth() / 2;
  int y = std::max(top + kGap, top + (getHeight() - top - columnHeight_) / 2);
  for (auto& row : rows_) {
    y += row.gapBefore;
    if (row.component != nullptr) {
      row.component->setTopLeftPosition(cx - row.component->getWidth() / 2,
                                        y + (row.height - row.component->getHeight()) / 2);
    } else if (row.text >= 0) {
      auto& t = texts_[static_cast<size_t>(row.text)];
      t.box = {static_cast<float>(cx - t.maxWidth / 2), static_cast<float>(y), static_cast<float>(t.maxWidth),
               t.flow->height()};
    } else {
      // Widths may have moved (Copy Link ↔ Copied): re-measure the row.
      int w = 0;
      for (auto* b : row.buttons) w += (w > 0 ? kButtonGap : 0) + b->getWidth();
      int x = cx - w / 2;
      for (auto* b : row.buttons) {
        b->setTopLeftPosition(x, y + (row.height - b->getHeight()) / 2);
        x += b->getWidth() + kButtonGap;
      }
    }
    y += row.height;
  }
  repaint();
}

}  // namespace t3k::ui
