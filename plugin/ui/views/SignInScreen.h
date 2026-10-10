// The sign-in takeover (Select Tone mockups 13342:46017 and 13342:46055):
// the page the plugin shows from the moment a sign-in starts until the
// session lands, whichever CTA started it (the account menu lands back on
// the chain, the tone browser's on the browser). It covers everything under
// the header as the tone browser does: a bare ← top-left that abandons the
// sign-in, and in the middle the loading dots over "Sign in with your
// browser".
//
// The browser is opened best effort, and there is no telling from here
// whether it came up (when it did, it covers the plugin anyway), so the
// page always offers the two ways round it under the title, both fed by the
// session's AuthFlow:
//   Copy Link       the authorize URL on the clipboard, to paste into any
//                   browser on this machine (the loopback redirect works
//                   from whichever browser completes it)
//   Sign in on a different device
//                   the device flow, on its own page: a QR code for the
//                   phone's camera, or the code to type at
//                   tone3000.com/activate for anyone who can't scan, polled
//                   until approved or expired
// The browser path stays live throughout, so finishing in the browser
// still works from the device page. A failed flow shows its reason with Try
// again / Dismiss in place of the dots.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "core/DelayedCall.h"
#include "core/RichText.h"
#include "services/Services.h"
#include "widgets/BackLink.h"
#include "widgets/Clickable.h"
#include "widgets/LoadingDots.h"
#include "widgets/PillButton.h"
#include "widgets/QrCode.h"

namespace t3k::ui {

class SignInScreen : public juce::Component, private ToneSession::Listener {
public:
  // The ← row sits where the tone browser's does.
  static constexpr int kPadTop = 24, kPadX = 32;
  // How long Copy Link reads "Copied".
  static constexpr int kCopiedMs = 2000;
  // The device page's two cards (mockup 13342:46055): 224 wide with 16px
  // padding round a 192px QR code; the code card matches the QR card's
  // height, and OR sits in the 32px gaps between them.
  static constexpr int kCardWidth = 224, kCardHeight = 280, kCardPad = 16, kCardGap = 32;
  static constexpr float kCardCorner = 16;
  static constexpr int kQrSize = 192;
  static constexpr int kCopyMaxWidth = 256, kWideMaxWidth = 400;
  static constexpr const char* kDefaultError = "Something went wrong completing TONE3000 sign-in.";

  explicit SignInScreen(Services& services);
  ~SignInScreen() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  // A line of grey copy that is a button ("Sign in on a different device"):
  // the hand cursor is its one affordance, no hover or pressed look (mockup).
  class TextLink : public Clickable {
  public:
    explicit TextLink(const juce::String& label);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  };

  // An icon beside a bold title (the device page's "Sign in on your phone").
  class IconTitle : public juce::Component {
  public:
    IconTitle(Icon icon, const juce::String& title);
    void paint(juce::Graphics& g) override;

  private:
    Icon icon_;
    juce::String title_;
  };

  // The device page's body: the QR card, OR, and the card with the code to
  // type at the verification URI (underlined to read as an address, not a
  // link: it is for typing into another device's browser).
  class DeviceCards : public juce::Component {
  public:
    DeviceCards();
    void setDevice(const ToneSession::AuthFlow::Device& device);
    // What a screen reader hears of the cards.
    juce::String spoken() const { return spoken_; }
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    QrCode qr_;
    std::unique_ptr<RichFlow> scan_, visit_;
    juce::String code_, spoken_;
  };

  // One centred row of the column: a component, a text block, or the
  // pill buttons side by side.
  struct Text {
    std::unique_ptr<RichFlow> flow;
    juce::Colour colour;
    int maxWidth;
    juce::Rectangle<float> box;
  };
  struct Row {
    int gapBefore, height, width;
    juce::Component* component = nullptr;
    int text = -1;  // index into texts_
    std::vector<PillButton*> buttons;
  };

  void sessionChanged() override {}
  void authFlowChanged() override;
  // Rebuild the column for the flow's state.
  void rebuild();
  void layoutColumn();
  void back();
  void copyLink();
  Row& addComponent(juce::Component& c, int gapBefore);
  Row& addText(const RichText& runs, float px, juce::Colour colour, int maxWidth, int gapBefore);
  Row& addText(const juce::String& text, float px, juce::Colour colour, int maxWidth, int gapBefore) {
    return addText(RichText{TextRun::plain(text)}, px, colour, maxWidth, gapBefore);
  }
  Row& addButtons(std::vector<PillButton*> buttons, int gapBefore);
  // The verification URI as a person types it ("tone3000.com/activate").
  static juce::String typedUri(const juce::String& uri);

  Services& services_;
  BackLink back_;
  LoadingDots dots_;
  IconTitle phoneTitle_;
  DeviceCards deviceCards_;
  TextLink otherDevice_;
  PillButton copyLink_, newCode_, retry_, dismiss_;
  std::vector<Text> texts_;
  std::vector<Row> rows_;
  int columnHeight_ = 0;
  DelayedCall copiedDelay_;
  juce::String announced_;
};

}  // namespace t3k::ui
