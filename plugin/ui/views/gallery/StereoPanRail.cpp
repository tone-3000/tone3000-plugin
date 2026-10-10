#include "StereoPanRail.h"

#include "GalleryGeometry.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/KnobScale.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kHeight = gallery::kStereoTileSize * 2 + gallery::kLaneGap;
// Web knob control height (36 knob + 8 gap + 17 label); Knob::heightFor adds
// the 2px editor overflow below, which isn't layout.
constexpr int kKnobVisualHeight = theme::kKnobSizeSecondary + theme::kKnobLabelGap + Knob::kLabelSlot;
constexpr int kWrapHeight = kKnobVisualHeight + StereoPanRail::kChipGap + theme::kTextBoxHeight;

Knob::Options panOptions(bool left) {
  Knob::Options o;
  o.label = left ? "Pan L" : "Pan R";
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.variant = left ? Knob::Variant::panLeft : Knob::Variant::panRight;
  o.min = left ? 0.0f : 0.5f;
  o.max = left ? 0.5f : 1.0f;
  o.scale = &scales::pan(left);
  o.defaultValue = left ? 0.0f : 1.0f;
  o.help = left ? help::Key::panLeft : help::Key::panRight;
  o.labelBright = true;
  return o;
}
}  // namespace

StereoPanRail::Lane::Lane(Services& services, bool left)
    : pan(services.backend, left ? "chainPanLeft" : "chainPanRight", panOptions(left)),
      chips({{"S", left ? help::Key::soloLeft : help::Key::soloRight},
             {juce::String::fromUTF8("\xC3\x98"), left ? help::Key::invertLeft : help::Key::invertRight}},
            SegmentedText::armed()),
      solo(services.backend, left ? "chainSoloLeft" : "chainSoloRight"),
      invert(services.backend, left ? "chainInvertLeft" : "chainInvertRight") {
  panOff.addAndMakeVisible(pan);
  panOff.setHelpText(help::text(help::Key::panMonoSum));
}

void StereoPanRail::MonoChip::paint(juce::Graphics& g) {
  paint::capText(g, "MONO", getLocalBounds().toFloat(), Fonts::mono(11), 11, theme::kMuted);
}

StereoPanRail::StereoPanRail(Services& services)
    : services_(services),
      left_(services, true),
      right_(services, false),
      linked_(services.backend, "chainPanLinked"),
      link_(Icon::Link, ChromeIconButton::Tone::link, help::Key::panLink),
      swap_(Icon::ArrowUpDown, ChromeIconButton::Tone::plain, help::Key::swapChains) {
  for (auto* lane : {&left_, &right_}) {
    addAndMakeVisible(lane->panOff);
    addAndMakeVisible(lane->chips);
    lane->solo.onChange = [this] { syncChips(); };
    lane->invert.onChange = [this] { syncChips(); };
  }
  // Linked: a move on one pan mirrors onto the other (anchored on the mover).
  left_.pan.onValueChange = [this](float v) {
    if (linked_.boolValue()) right_.pan.binding().set(1.0f - v);
  };
  right_.pan.onValueChange = [this](float v) {
    if (linked_.boolValue()) left_.pan.binding().set(1.0f - v);
  };
  // Solo is exclusive: engaging one clears the other.
  left_.chips.onCellClick = [this](int i) {
    if (i == 0) toggleSolo(true);
    else left_.invert.set(!left_.invert.boolValue());
    syncChips();
  };
  right_.chips.onCellClick = [this](int i) {
    if (i == 0) toggleSolo(false);
    else right_.invert.set(!right_.invert.boolValue());
    syncChips();
  };

  addAndMakeVisible(link_);
  addAndMakeVisible(swap_);
  addChildComponent(mono_);
  mono_.setHelpText(help::text(help::Key::monoSum));
  linked_.onChange = [this] { link_.setOn(linked_.boolValue()); };
  link_.setOn(linked_.boolValue());
  link_.onClick = [this] {
    const bool next = !linked_.boolValue();
    linked_.set(next);
    link_.setOn(next);
    // Re-linking snaps back to a symmetric image, anchored on the left pan.
    if (next) right_.pan.binding().set(1.0f - left_.pan.binding().normalised());
  };
  swap_.onClick = [this] { services_.chain.swapChains(); };

  syncChips();
  setSize(kWidth, kHeight);
}

StereoPanRail::~StereoPanRail() = default;

void StereoPanRail::toggleSolo(bool left) {
  auto& mine = left ? left_.solo : right_.solo;
  auto& other = left ? right_.solo : left_.solo;
  const bool next = !mine.boolValue();
  mine.set(next);
  if (next) other.set(false);
}

void StereoPanRail::syncChips() {
  for (auto* lane : {&left_, &right_}) {
    lane->chips.setOn(0, lane->solo.boolValue());
    lane->chips.setOn(1, lane->invert.boolValue());
  }
}

// Mono sum: the pans can't be heard, so they dim and go inert (their wrapper
// carries the hint that says why); the [S|Ø] chips stay live (solo and
// polarity act inside the sum); the pill swaps link/swap for the MONO chip.
void StereoPanRail::setMonoSum(bool monoSum) {
  if (monoSum_ == monoSum) return;
  monoSum_ = monoSum;
  left_.panOff.setOff(monoSum);
  right_.panOff.setOff(monoSum);
  link_.setVisible(!monoSum);
  swap_.setVisible(!monoSum);
  mono_.setVisible(monoSum);
  repaint();
}

// Column: [region: spacer | wrap | connector(margin-top 10)] pill
// [region: connector(margin-bottom 10) | wrap | spacer]; the spacer and the
// connector split each region's slack equally.
void StereoPanRail::resized() {
  const int inner = getWidth() - 1;  // padding-right 1
  const int cx = inner / 2;
  const int regionH = (getHeight() - kPillHeight) / 2;
  const float slack = (regionH - kWrapHeight - kConnectorMargin) / 2.0f;

  auto layoutLane = [&](Lane& lane, int wrapTop) {
    lane.wrap = {0, wrapTop, inner, kWrapHeight};
    lane.panOff.setBounds(0, wrapTop, inner, Knob::heightFor(theme::kKnobSizeSecondary));
    lane.pan.setBounds(lane.panOff.getLocalBounds());
    lane.chips.setTopLeftPosition(cx - lane.chips.getWidth() / 2,
                                  wrapTop + kKnobVisualHeight + kChipGap);
  };
  layoutLane(left_, design::snap(slack));
  pill_ = {cx - kPillWidth / 2, regionH, kPillWidth, kPillHeight};
  layoutLane(right_, pill_.getBottom() + design::snap(kConnectorMargin + slack));

  // Pill content: 1px border + 3/5 padding, icon boxes 4 apart.
  const auto content = pill_.reduced(1).reduced(5, 3);
  link_.setBounds(content.getX(), content.getY(), theme::kIconBoxSize, theme::kIconBoxSize);
  swap_.setBounds(content.getX() + theme::kIconBoxSize + 4, content.getY(), theme::kIconBoxSize,
                  theme::kIconBoxSize);
  mono_.setBounds(content);
}

void StereoPanRail::paint(juce::Graphics& g) {
  const float x = (getWidth() - 1) / 2.0f - 0.5f;  // 1px hairline on the centre column
  g.setColour(theme::kBorder);
  g.fillRect(juce::Rectangle<float>(x, static_cast<float>(left_.wrap.getBottom() + kConnectorMargin), 1.0f,
                                    static_cast<float>(pill_.getY() - left_.wrap.getBottom() - kConnectorMargin)));
  g.fillRect(juce::Rectangle<float>(x, static_cast<float>(pill_.getBottom()), 1.0f,
                                    static_cast<float>(right_.wrap.getY() - kConnectorMargin - pill_.getBottom())));
  paint::border(g, pill_.toFloat(), kPillHeight / 2.0f, theme::kBorder);
}

}  // namespace t3k::ui
