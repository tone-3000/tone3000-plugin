// Extra tone metadata under the detail card's title column (port of
// BlockInfoPanel.tsx): hairline, then either a sign-in / retry prompt or the
// description (3-line clamp with MORE/LESS), makes & models, and tag chips;
// another hairline and the "View on TONE3000" link close it. Empty sections
// are omitted. Height depends on width and content, so the owner lays it
// out via heightFor(width) before setBounds.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/TextFlow.h"
#include "model/Tone.h"
#include "widgets/PillButton.h"

namespace t3k::ui {

class BlockInfoPanel : public juce::Component {
public:
  BlockInfoPanel();
  ~BlockInfoPanel() override;

  struct State {
    bool authenticated = false;
    juce::String error;         // non-empty: the retry prompt
    std::optional<Tone> tone;   // the fetched catalog tone
    juce::String pageUrl;       // public tone page
  };
  void setState(State state);

  std::function<void()> onLogin;
  std::function<void()> onRetry;
  std::function<void(const juce::String& url)> onOpenUrl;
  // MORE / LESS changed the content height: the owner re-measures.
  std::function<void()> onHeightChanged;

  // Content height at `width` (rebuilds the flow for that width).
  int heightFor(int width);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class MoreButton;
  static constexpr int kGap = 24;
  static constexpr int kSectionGap = 8;
  static constexpr int kMakesGap = 4;
  static constexpr int kTagGap = 8;
  static constexpr int kPromptPadY = 8;
  static constexpr int kPromptGap = 16;
  static constexpr int kDescClampLines = 3;
  static constexpr float kBodyPx = 14;
  static constexpr float kLineHeight = kBodyPx * 1.4f;
  static constexpr float kTagPx = 12;
  static constexpr int kTagPadX = 16, kTagPadY = 8;

  // One laid-out item of the vertical stack, in local coordinates.
  struct Item {
    enum class Kind { hairline, prompt, title, body, makes, tags, control };
    Kind kind;
    juce::Rectangle<int> bounds;
    juce::String text;                              // title
    std::vector<std::unique_ptr<TextFlow>> flows;   // prompt / body: one; makes: one per row
    std::vector<juce::Rectangle<int>> boxes;        // makes rows / tag chips, relative to bounds
    std::vector<juce::String> labels;               // tag chips
    int maxLines = 0;                               // body clamp (0 = all)
  };

  void rebuild(int width);
  Item& add(Item::Kind kind, float height);
  void addTitle(const juce::String& text);
  std::vector<juce::String> makes() const;
  std::vector<juce::String> tags() const;
  int tagHeight() const;

  // Stack cursor in fractional pixels like the browser (a 19.6px title line
  // shifts everything below by 0.6, not 1); each item's bounds round once.
  float cursorY_ = 0;

  State state_;
  bool descExpanded_ = false;
  int builtWidth_ = -1;
  std::vector<Item> items_;
  std::unique_ptr<PillButton> prompt_;  // Log In / Try again
  std::unique_ptr<MoreButton> more_;
  std::unique_ptr<PillButton> view_;
};

}  // namespace t3k::ui
