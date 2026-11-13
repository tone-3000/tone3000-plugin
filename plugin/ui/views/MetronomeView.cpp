#include "MetronomeView.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/TunerView.h"

namespace t3k::ui {

namespace {

constexpr int kBars = 6, kBarW = 40, kBarGap = 10, kBarH = 320, kSideGap = 24;
constexpr int kBarsW = kBars * kBarW + (kBars - 1) * kBarGap;
constexpr int kCentreW = 316;
constexpr int kRowW = 2 * kBarsW + 2 * kSideGap + kCentreW;
constexpr float kBpmPx = 120, kCaptionPx = 20;
constexpr int kKnobSize = theme::kKnobSizePrimary, kKnobColumn = 80, kKnobSpread = 130, kRowGap = 8;
constexpr float kButtonScale = 2;  // the power button, drawn at the standard chrome size doubled
// Bar colours from the centre outward: blue, yellow, red, two bars each.
const juce::Colour kBarColours[kBars] = {theme::kBrandBlue, theme::kBrandBlue, theme::kBrandYellow,
                                         theme::kBrandYellow, theme::kBrandRed,  theme::kBrandRed};

// Defaults: 100 BPM, loud enough to carry over a distorted amp.
constexpr float kVolumeDefault = 0.8f;
const float kTempoDefault = static_cast<float>(scales::bpm().fromDisplay(100));

Knob::Options dialKnob(const char* label, const KnobScale& scale, float def, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.size = kKnobSize;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  return o;
}

}  // namespace

MetronomeView::MetronomeView(Services& services)
    : close_(Icon::X, TunerView::kCloseBox, TunerView::kCloseGlyph),
      power_(services.backend, "metronomeEnabled", help::Key::metronomeToggle),
      tempo_(services.backend, "metronomeBpm",
             dialKnob("Tempo", scales::bpm(), kTempoDefault, help::Key::metronomeTempo)),
      volume_(services.backend, "metronomeVolume",
              dialKnob("Volume", scales::percent(), kVolumeDefault, help::Key::metronomeVolume)),
      backend_(services.backend),
      clock_(services.clock) {
  setOpaque(true);
  close_.setName("Close metronome");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  power_.onValueChange = [this](bool on) { setAnimating(on); };
  setAnimating(power_.value());
  tempo_.onValueChange = [this](float) { repaint(centre_); };
  tempo_.onReset = [this] { repaint(centre_); };
  const auto echoToKnob = tempo_.binding().onChange;
  tempo_.binding().onChange = [this, echoToKnob] {
    echoToKnob();
    repaint(centre_);
  };
  for (auto* child : std::initializer_list<juce::Component*>{&close_, &power_, &tempo_, &volume_})
    addAndMakeVisible(*child);
  // A focused knob would take the arrows at its own, coarser step.
  tempo_.addKeyListener(this);
  volume_.addKeyListener(this);
}

MetronomeView::~MetronomeView() { clock_.removeListener(this); }

void MetronomeView::step(ParamKnob& knob, const KnobScale& scale, double units) {
  auto& binding = knob.binding();
  const double display = scale.toDisplay(binding.normalised()) + units;
  binding.set(juce::jlimit(0.0f, 1.0f, static_cast<float>(scale.fromDisplay(display))));
}

bool MetronomeView::isToggleKey(const juce::KeyPress& key) {
  const auto mods = key.getModifiers();
  const bool plain = !mods.isCommandDown() && !mods.isAltDown() && !mods.isCtrlDown();
  return plain && (key.isKeyCode('M') || juce::CharacterFunctions::toUpperCase(key.getTextCharacter()) == 'M');
}

bool MetronomeView::keyPressed(const juce::KeyPress& key) {
  using KP = juce::KeyPress;
  const auto mods = key.getModifiers();
  if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown()) return false;
  const int units = mods.isShiftDown() ? 10 : 1;
  if (key.isKeyCode(KP::escapeKey) || isToggleKey(key))
    // Deferred: closing destroys this view, and a child's key listener may still be running.
    juce::MessageManager::callAsync([self = juce::Component::SafePointer<MetronomeView>(this)] {
      if (self != nullptr && self->onClose) self->onClose();
    });
  else if (key.isKeyCode(KP::spaceKey))
    power_.onClick();
  else if (key.isKeyCode(KP::upKey) || key.isKeyCode(KP::downKey))
    step(tempo_, scales::bpm(), key.isKeyCode(KP::upKey) ? units : -units);
  else if (key.isKeyCode(KP::rightKey) || key.isKeyCode(KP::leftKey))
    step(volume_, scales::percent(), key.isKeyCode(KP::rightKey) ? units : -units);
  else
    return false;
  return true;
}

// Follows the audio's own beat position, so the bars peak with the click.
void MetronomeView::tick() {
  pulse_.set(backend_.getMetronomeBeats());
  repaint(leftRow_);
  repaint(rightRow_);
}

void MetronomeView::setAnimating(bool on) {
  clock_.removeListener(this);
  pulse_.reset();
  if (on) clock_.addListener(this);
  repaint(leftRow_);
  repaint(rightRow_);
}

void MetronomeView::placeScaled(juce::Component& button, int x, int y) {
  button.setTopLeftPosition(x, y);
  button.setTransform(juce::AffineTransform::scale(kButtonScale, kButtonScale, static_cast<float>(x), static_cast<float>(y)));
}

void MetronomeView::resized() {
  const auto area = getLocalBounds();
  close_.setBounds(area.getRight() - TunerView::kCloseRight - TunerView::kCloseBox,
                   area.getY() + TunerView::kCloseTop, TunerView::kCloseBox, TunerView::kCloseBox);

  const int x0 = area.getX() + (area.getWidth() - kRowW) / 2;
  const int y0 = area.getY() + (area.getHeight() - kBarH) / 2;
  leftRow_ = {x0, y0, kBarsW, kBarH};
  rightRow_ = {x0 + kRowW - kBarsW, y0, kBarsW, kBarH};
  centre_ = {x0 + kBarsW + kSideGap, y0, kCentreW, kBarH};

  // Top to bottom: the BPM, the two knobs, power.
  const int knobH = Knob::heightFor(kKnobSize);
  const int bpmH = static_cast<int>(kBpmPx + kCaptionPx);
  const int powerSize = static_cast<int>(power_.getWidth() * kButtonScale);
  const int knobsY = centre_.getY() + bpmH + kRowGap;
  tempo_.setBounds(centre_.getCentreX() - (kKnobSpread + kKnobColumn) / 2, knobsY, kKnobColumn, knobH);
  volume_.setBounds(centre_.getCentreX() + (kKnobSpread - kKnobColumn) / 2, knobsY, kKnobColumn, knobH);
  placeScaled(power_, centre_.getCentreX() - powerSize / 2, knobsY + knobH + 2 * kRowGap);
}

void MetronomeView::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  const auto clip = g.getClipBounds();
  if (clip.intersects(leftRow_)) paintBars(g, true, leftRow_);
  if (clip.intersects(rightRow_)) paintBars(g, false, rightRow_);
  if (clip.intersects(centre_)) paintReadout(g);
}

void MetronomeView::paintBars(juce::Graphics& g, bool left, juce::Rectangle<int> row) const {
  const int lit = left == pulse_.leftSide() ? static_cast<int>(std::ceil(pulse_.level() * kBars)) : 0;
  for (int slot = 0; slot < kBars; ++slot) {
    const int index = left ? kBars - 1 - slot : slot;  // 0 = innermost
    const auto b = juce::Rectangle<int>(row.getX() + slot * (kBarW + kBarGap), row.getY(), kBarW, kBarH).toFloat();
    // The short edge faces the centre so both sides recede toward the BPM.
    const float outerX = left ? b.getX() : b.getRight(), innerX = left ? b.getRight() : b.getX();
    juce::Path p;
    p.startNewSubPath(outerX, b.getY());
    p.lineTo(innerX, b.getY() + b.getHeight() * TunerView::kTaperTop);
    p.lineTo(innerX, b.getY() + b.getHeight() * TunerView::kTaperBottom);
    p.lineTo(outerX, b.getBottom());
    p.closeSubPath();
    g.setColour(index < lit ? kBarColours[index] : theme::kSurfaceRaised);
    g.fillPath(p);
  }
}

void MetronomeView::paintReadout(juce::Graphics& g) const {
  const double bpm = scales::bpm().toDisplay(tempo_.value());
  const auto number = juce::String(juce::roundToInt(bpm));
  const auto numberBox = centre_.withHeight(static_cast<int>(kBpmPx));
  const auto font = Fonts::sans(kBpmPx, true);
  g.setColour(theme::kWhite);
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(font, number, numberBox.getCentreX() - Fonts::width(font, number) / 2,
                       numberBox.getY() + Fonts::cssBaseline(font, kBpmPx));
  glyphs.draw(g);
  paint::capText(g, "BPM", numberBox.withTop(numberBox.getBottom()).withHeight(static_cast<int>(kCaptionPx)).toFloat(),
                 Fonts::mono(kCaptionPx), kCaptionPx, theme::kGray, juce::Justification::centred);
}

}  // namespace t3k::ui
