#include "FormControls.h"

#include "core/Paint.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

// PillToggle
PillToggle::PillToggle() : Clickable({}), progress_(*this, [this](float) { repaint(); }) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setToggleable(true);  // a screen reader hears on / off
  setSize(kWidth, kHeight);
  onClick = [this] {
    setValue(!on_);
    if (onChange) onChange(on_);
  };
}

void PillToggle::setValue(bool on, bool animate) {
  if (on == on_ && !progress_.running()) return;
  on_ = on;
  setToggleState(on, juce::dontSendNotification);
  if (animate)
    progress_.animateTo(on ? 1.0f : 0.0f, kAnimMs);
  else
    progress_.snap(on ? 1.0f : 0.0f);
}

void PillToggle::paintButton(juce::Graphics& g, bool, bool) {
  const auto track = getLocalBounds().toFloat();
  const float t = progress_.value();
  const float radius = kHeight / 2.0f;
  paint::fill(g, track, radius, form::kToggleOff.interpolatedWith(form::kToggleOn, t));

  // inset 0 2px 4px rgba(0,0,0,.15): a soft dark band along the top edge
  // (and a faint one along the bottom), clipped to the pill.
  {
    juce::Graphics::ScopedSaveState state(g);
    juce::Path pill;
    pill.addRoundedRectangle(track, radius);
    g.reduceClipRegion(pill);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.13f), 0, 0,
                                           juce::Colours::transparentBlack, 0, 7.0f, false));
    g.fillRect(track.withHeight(7.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, 0, track.getBottom() - 3.0f,
                                           juce::Colours::black.withAlpha(0.04f), 0, track.getBottom(), false));
    g.fillRect(track.withTop(track.getBottom() - 3.0f));
  }

  // Knob: translateX(16px) as it turns on; 0 1px 2px rgba(0,0,0,.3) shadow.
  const float x = kInset + t * (kWidth - 2 * kInset - kKnob);
  const juce::Rectangle<float> knob(x, static_cast<float>(kInset), static_cast<float>(kKnob),
                                    static_cast<float>(kKnob));
  g.setColour(juce::Colours::black.withAlpha(0.10f));
  g.fillEllipse(knob.translated(0, 1).expanded(1.0f));
  g.setColour(juce::Colours::black.withAlpha(0.12f));
  g.fillEllipse(knob.translated(0, 1));
  g.setColour(theme::kWhite);
  g.fillEllipse(knob);
}

// ChoiceIndicator
void ChoiceIndicator::paint(juce::Graphics& g, juce::Rectangle<float> box, bool selected, bool square) {
  const float ring = 2.0f;
  const auto colour = selected ? theme::kWhite : form::kToggleOff;
  if (square) {
    paint::border(g, box, 5.0f, colour, ring);
  } else {
    g.setColour(colour);
    g.drawEllipse(box.reduced(ring / 2), ring);
  }
  if (!selected) return;
  const auto dot = juce::Rectangle<float>(8, 8).withCentre(box.getCentre());
  g.setColour(theme::kWhite);
  if (square)
    g.fillRoundedRectangle(dot, 2.0f);
  else
    g.fillEllipse(dot);
}

// SegmentedControl
class SegmentedControl::Cell : public Clickable {
public:
  Cell(SegmentedControl& owner, juce::String label, int index)
      : Clickable(label), owner_(owner), label_(std::move(label)), index_(index) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
      if (owner_.selected_ == index_) return;
      owner_.setSelected(index_);
      if (owner_.onChange) owner_.onChange(index_);
    };
  }

  int preferredWidth() const {
    return juce::roundToInt(Fonts::width(font(), label_)) + 2 * kCellPadX;
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const bool on = owner_.selected_ == index_;
    if (on) paint::fill(g, getLocalBounds().toFloat(), 6.0f, juce::Colours::white.withAlpha(0.16f));
    const float lineH = static_cast<float>(Fonts::normalLineHeight(kCellPx));
    g.setColour(on ? theme::kWhite : theme::kMuted);
    g.setFont(font());
    g.drawSingleLineText(label_, kCellPadX, juce::roundToInt(kCellPadY + Fonts::cssBaseline(font(), lineH)));
  }

private:
  static juce::Font font() { return Fonts::sans(kCellPx, true); }
  SegmentedControl& owner_;
  juce::String label_;
  int index_;
};

SegmentedControl::SegmentedControl(std::vector<juce::String> labels) {
  int i = 0;
  for (auto& label : labels) {
    cells_.push_back(std::make_unique<Cell>(*this, std::move(label), i++));
    addAndMakeVisible(*cells_.back());
  }
  setSize(preferredWidth(), preferredHeight());
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::setSelected(int index) {
  selected_ = index;
  repaint();
}

int SegmentedControl::preferredWidth() const {
  int w = 2 * (kPad + 1);
  for (const auto& cell : cells_) w += cell->preferredWidth();
  return w;
}

int SegmentedControl::preferredHeight() {
  return 2 * (kPad + 1) + 2 * kCellPadY + Fonts::normalLineHeight(kCellPx);
}

void SegmentedControl::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, 8.0f, juce::Colour(0xff0a0a0a));
  paint::border(g, box, 8.0f, form::kFieldBorder);
}

void SegmentedControl::resized() {
  auto area = getLocalBounds().reduced(kPad + 1);
  for (auto& cell : cells_) cell->setBounds(area.removeFromLeft(cell->preferredWidth()));
}

// FormButton
FormButton::Look FormButton::outlined() {
  return {form::kFieldBorder, form::kFieldRadius, form::kFieldPadX, form::kFieldPadY, form::kFieldPx, false,
          theme::kWhite};
}

FormButton::Look FormButton::cta() {
  return {theme::kWhite, form::kCardRadius, 16, 12, 15, false, theme::kWhite};
}

FormButton::Look FormButton::text(float px, bool bold, juce::Colour colour, int padX, int padY,
                                  juce::Justification align) {
  return {std::nullopt, 0, padX, padY, px, bold, colour, align};
}

FormButton::FormButton(juce::String label, Look look) : Clickable(label), label_(std::move(label)), look_(look) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void FormButton::setLabel(const juce::String& label) {
  label_ = label;
  setName(label);
  repaint();
}

void FormButton::setTextColour(juce::Colour colour) {
  look_.text = colour;
  repaint();
}

void FormButton::setLeadingIcon(Icon icon, float px, int gap) {
  icon_ = LeadingIcon{icon, px, gap};
  repaint();
}

int FormButton::preferredWidth() const {
  const int border = look_.border ? 1 : 0;
  const int iconW = icon_ ? juce::roundToInt(icon_->px) + icon_->gap : 0;
  return juce::roundToInt(Fonts::width(font(), label_)) + iconW + 2 * (look_.padX + border);
}

int FormButton::preferredHeight() const {
  const int border = look_.border ? 1 : 0;
  return Fonts::normalLineHeight(look_.fontPx) + 2 * (look_.padY + border);
}

void FormButton::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const int border = look_.border ? 1 : 0;
  if (look_.border) paint::border(g, box, look_.radius, *look_.border);

  const auto f = font();
  const float lineH = static_cast<float>(Fonts::normalLineHeight(look_.fontPx));
  const float textW = Fonts::width(f, label_);
  const float iconW = icon_ ? icon_->px + icon_->gap : 0;
  const float contentW = textW + iconW;
  float x = border + look_.padX;
  if (look_.align.testFlags(juce::Justification::horizontallyCentred)) x = (box.getWidth() - contentW) / 2;
  else if (look_.align.testFlags(juce::Justification::right)) x = box.getWidth() - border - look_.padX - contentW;
  const float lineTop = (box.getHeight() - lineH) / 2;

  if (icon_) {
    Icons::draw(g, icon_->icon, juce::Rectangle<float>(icon_->px, icon_->px).withCentre({x + icon_->px / 2, box.getCentreY()}),
                look_.text);
    x += iconW;
  }
  g.setColour(look_.text);
  g.setFont(f);
  g.drawSingleLineText(label_, juce::roundToInt(x), juce::roundToInt(lineTop + Fonts::cssBaseline(f, lineH)));
}

}  // namespace t3k::ui
