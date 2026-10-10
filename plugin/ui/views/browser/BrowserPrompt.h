// The browser's centred call-to-action column (ToneBrowser.tsx SignInPrompt,
// the stream error state and Trending's footer): an optional TONE3000 mark,
// centred white copy wrapped at a max width, and one pill button, 16px
// apart inside 48px / 24px padding. Height follows the copy.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/TextFlow.h"
#include "widgets/PillButton.h"

namespace t3k::ui {

class BrowserPrompt : public juce::Component {
public:
  static constexpr int kPadY = 48, kPadX = 24, kGap = 16, kMarkHeight = 28;
  static constexpr float kCopyPx = 14;

  // Takes the button, already styled (Browse, Sign in, Try again).
  BrowserPrompt(bool mark, const juce::String& copy, int copyMaxWidth, std::unique_ptr<PillButton> button);
  ~BrowserPrompt() override;

  PillButton& button() { return *button_; }
  int heightFor(int width);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void layout(int width);

  bool mark_;
  juce::String copy_;
  int copyMaxW_;
  std::unique_ptr<PillButton> button_;
  int builtWidth_ = -1;
  std::unique_ptr<TextFlow> flow_;
  juce::Rectangle<float> markBox_, copyBox_;
  int height_ = 0;
};

}  // namespace t3k::ui
