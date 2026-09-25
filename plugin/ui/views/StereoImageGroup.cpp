#include "StereoImageGroup.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {

// Default +15 ms R on the bipolar ±24 ms offset (matches the APVTS default).
constexpr float kSpreadOffsetDefault = 0.8125f;
constexpr float kAlignOffsetDefault = 0.5f;

constexpr int kRowGap = 10;
// The advert stands as tall as the plate's primary knobs, centred on the
// secondary-knob centreline (label + gap + radius above the baseline).
constexpr int kAdvertHeight = theme::kKnobSizePrimary;
constexpr int kSecondaryCentreY = theme::kKnobLabelGap + 14 + theme::kKnobSizeSecondary / 2;
// Chrome boxes in a bottom-aligned row sit on that same centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);
// Arrow glyph: a 24x10 viewBox drawn into a 20x7 box (uniform fit).
constexpr float kArrowW = 20.0f, kArrowH = 7.0f, kArrowGap = 8.0f;

struct Copy {
  const char* label;
  help::Key advert, offset, power;
  float offsetDefault;
  bool arrowsInward;
};
constexpr Copy copyFor(ImageFeature f) {
  using K = help::Key;
  return f == ImageFeature::spread
             ? Copy{"SPREAD", K::spreadAdvert, K::spreadOffset, K::spreadPower, kSpreadOffsetDefault, false}
             : Copy{"ALIGN", K::alignAdvert, K::alignOffset, K::alignPower, kAlignOffsetDefault, true};
}

Knob::Options offsetKnob(const Copy& copy) {
  Knob::Options o;
  o.label = "Offset";
  o.size = theme::kKnobSizePrimary;
  o.variant = Knob::Variant::bipolar;
  o.scale = &scales::offsetMs();
  o.defaultValue = copy.offsetDefault;
  o.help = copy.offset;
  return o;
}

}  // namespace

// Advert
// What sits in the slot while the feature is off: an outline pill CTA
// (pillButtonStyle) flanked by elongated hollow triangles, pointing out for
// Spread (◁ SPREAD ▷) and in for Align (▷ ALIGN ◁).
class StereoImageGroup::Advert : public Clickable {
public:
  Advert(const char* label, bool arrowsInward, help::Key help)
      : Clickable(label), label_(label), inward_(arrowsInward) {
    setSize(kWidth, kAdvertHeight);
    setHelpText(help::text(help));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void mouseDown(const juce::MouseEvent& e) override {
    if (isSecondaryPress(e))
      forwardSecondaryPress(*this, e);
    else
      juce::Button::mouseDown(e);
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds().toFloat();
    paint::border(g, box, box.getHeight() / 2, theme::kWhite);

    const auto font = Fonts::sans(12);
    const float textW = Fonts::width(font, label_);
    const float total = kArrowW + kArrowGap + textW + kArrowGap + kArrowW;
    float x = box.getCentreX() - total / 2;
    const float cy = box.getCentreY();

    // Left glyph points left for Spread (outward), right for Align (inward).
    drawArrow(g, {x, cy - kArrowH / 2, kArrowW, kArrowH}, /*pointsRight=*/inward_);
    x += kArrowW + kArrowGap;
    g.setFont(font);
    g.setColour(theme::kWhite);
    g.drawText(label_, juce::Rectangle<float>(x, box.getY(), textW, box.getHeight()),
               juce::Justification::centred, false);
    x += textW + kArrowGap;
    drawArrow(g, {x, cy - kArrowH / 2, kArrowW, kArrowH}, /*pointsRight=*/!inward_);
  }

private:
  // SpreadArrow: path "M2 2 L20.5 5 L2 8 Z" in a 0 0 24 10 viewBox, stroke
  // 1.15, mitre joins; ~3.5:1 length:height.
  static void drawArrow(juce::Graphics& g, juce::Rectangle<float> box, bool pointsRight) {
    juce::Path p;
    p.addTriangle(2.0f, 2.0f, 20.5f, 5.0f, 2.0f, 8.0f);
    const float s = juce::jmin(box.getWidth() / 24.0f, box.getHeight() / 10.0f);
    auto t = juce::AffineTransform::scale(s).translated(box.getCentreX() - 12.0f * s,
                                                        box.getCentreY() - 5.0f * s);
    if (!pointsRight) t = juce::AffineTransform::scale(-1.0f, 1.0f, 12.0f, 5.0f).followedBy(t);
    g.setColour(theme::kWhite);
    g.strokePath(p, juce::PathStrokeType(1.15f * s, juce::PathStrokeType::mitered), t);
  }

  juce::String label_;
  bool inward_;
};

// Group
StereoImageGroup::StereoImageGroup(Services& services, ImageFeature feature)
    : services_(services),
      feature_(feature),
      enabled_(services.backend, imageFeaturePrefix(feature) + "Enabled"),
      advert_(std::make_unique<Advert>(copyFor(feature).label, copyFor(feature).arrowsInward,
                                       copyFor(feature).advert)),
      offset_(services.backend, imageFeaturePrefix(feature) + "Offset", offsetKnob(copyFor(feature))),
      power_(Icon::Power, ChromeIconButton::Tone::power, copyFor(feature).power),
      deck_(services, feature) {
  advert_->onClick = [this] {
    enabled_.set(true);
    syncEnabled();
  };
  power_.onClick = [this] {
    enabled_.set(false);
    syncEnabled();
  };
  enabled_.onChange = [this] { syncEnabled(); };

  // Alt/Option-click on Offset also restores the advanced deck defaults, so
  // one gesture resets the feature, not just the knob.
  offset_.onReset = [this] { ImageDeckPanel::resetDeck(services_.backend, feature_); };
  // Touch-and-hold on the knob is the right-click of the platform.
  offset_.onLongPress = [this] { toggleDeck(); };

  if (feature == ImageFeature::align) {
    auto_ = std::make_unique<ChromeIconButton>(Icon::Equal, ChromeIconButton::Tone::armed,
                                               help::Key::autoAlign);
    auto_->setOn(services_.autoAlign.listening());
    auto_->onClick = [this] { services_.autoAlign.toggle(); };
    services_.autoAlign.addListener(this);
    addChildComponent(*auto_);
  }

  addChildComponent(*advert_);
  addChildComponent(offset_);
  addChildComponent(power_);
  syncEnabled();
  setSize(kWidth, height());  // after auto_ exists: this is the layout pass that places it
}

StereoImageGroup::~StereoImageGroup() {
  if (auto_ != nullptr) services_.autoAlign.removeListener(this);
  deck_.close();
}

void StereoImageGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void StereoImageGroup::autoMeasureChanged() {
  if (auto_ != nullptr) auto_->setOn(services_.autoAlign.listening());
}

void StereoImageGroup::syncEnabled() {
  const bool on = enabled_.boolValue();
  if (offset_.isVisible() == on && advert_->isVisible() == !on) return;
  advert_->setVisible(!on);
  offset_.setVisible(on);
  power_.setVisible(on);
  if (auto_ != nullptr) auto_->setVisible(on);
  // The deck re-anchors to whichever face is showing.
  if (deck_.isOpen()) {
    deck_.close();
    toggleDeck();
  }
}

void StereoImageGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
    return;
  }
  // Anchored to the Offset knob its left edge tracks the knob; in the advert
  // state it flush-rights to the group.
  if (enabled_.boolValue())
    deck_.open(offset_, Popover::Align::left, ImageDeckPanel::kGap, 0, Popover::Placement::above);
  else
    deck_.open(*advert_, Popover::Align::right, ImageDeckPanel::kGap, 0, Popover::Placement::above);
}

void StereoImageGroup::resized() {
  const int baseline = getHeight() - Knob::kEditorOverflow;  // the label slot's bottom edge

  // Advert: vertically centred on the secondary-knob centreline.
  advert_->setTopLeftPosition(0, baseline - kSecondaryCentreY - kAdvertHeight / 2);

  // Centred [auto|spacer] [knob] [power] row: the spacer mirrors the power
  // button's box so the knob lands dead centre in the slot while the power
  // keeps the plate-standard gap to its knob.
  const int rowW = theme::kIconBoxSize + kRowGap + offset_.knobSize() + kRowGap + theme::kIconBoxSize;
  int x = (getWidth() - rowW) / 2;
  const int chromeY = baseline - theme::kIconBoxSize + kChromeLift;
  if (auto_ != nullptr) auto_->setTopLeftPosition(x, chromeY);
  x += theme::kIconBoxSize + kRowGap;
  offset_.setTopLeftPosition(x, getHeight() - offset_.getHeight());
  x += offset_.knobSize() + kRowGap;
  power_.setTopLeftPosition(x, chromeY);
}

}  // namespace t3k::ui
