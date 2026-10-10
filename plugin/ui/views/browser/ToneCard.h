// Two-up tone card in the browser grid (ToneBrowser.tsx ToneCard): square
// artwork on the left; title, gear + format, counts and creator stacked on
// the right, centred against the image. Disabled cards (NAM tones with no
// A2 model, or any card while another pick resolves) dim and refuse the
// click; the card being picked carries the busy overlay instead.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/TextFlow.h"
#include "model/Tone.h"
#include "services/ImageLoader.h"
#include "widgets/Avatar.h"
#include "widgets/BusyOverlay.h"
#include "widgets/Clickable.h"
#include "widgets/FormatBadge.h"
#include "widgets/ToneImage.h"

namespace t3k::ui {

class ToneCard : public Clickable {
public:
  static constexpr int kPad = 12;
  static constexpr int kImage = 112;
  static constexpr float kCorner = 12;
  static constexpr int kStatIcon = 14;
  static constexpr int kStatGap = 6;
  static constexpr int kStatIconWidth = kStatIcon + kStatGap;

  ToneCard(ImageLoader& images, const Tone& tone);
  ~ToneCard() override;

  const Tone& tone() const { return tone_; }
  // NAM tones without an A2 model: the plugin can't load them.
  static bool unavailable(const Tone& tone) { return tone.isNam() && tone.a2ModelsCount == 0; }

  // Natural height at `width` (padding included): the image, or a taller
  // text column, fractional like the grid row it sizes.
  float contentHeightFor(int width);
  // The grid row's (fractional) height this card is stretched to; its
  // bounds are that snapped to the pixel grid.
  void setContentHeight(float height);
  void setLoading(bool loading);
  void setDisabled(bool disabled);

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  void resized() override;

private:
  static constexpr int kGapX = 16;
  static constexpr float kImageCorner = 8;
  static constexpr int kRowGap = 8;
  static constexpr float kTitlePx = 14;
  static constexpr float kTitleLine = kTitlePx * 1.3f;
  static constexpr int kTitleLines = 2;
  static constexpr float kBodyPx = 13;
  static constexpr int kGearGap = 10;
  static constexpr int kStatsGap = 16;
  static constexpr int kAvatar = 22;
  static constexpr int kCreatorGap = 8;

  // Wrap the title and total the text column for `width`.
  void measure(int width);
  void syncState();

  Tone tone_;
  bool loading_ = false;
  bool disabled_ = false;

  ToneImage image_;
  FormatBadge badge_;
  Avatar avatar_;
  std::unique_ptr<BusyOverlay> busy_;

  // Text column measured at the last width; rows placed by resized() (card
  // coordinates, fractional like the browser's centred flex column).
  int builtWidth_ = -1;
  std::unique_ptr<TextFlow> title_;
  float columnHeight_ = 0;
  float contentHeight_ = 0;
  juce::Rectangle<float> titleBox_, gearRow_, statsRow_, creatorRow_;
};

}  // namespace t3k::ui
