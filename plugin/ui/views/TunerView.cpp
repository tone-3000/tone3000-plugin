#include "TunerView.h"

#include "core/AlphaTween.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Pitch.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// Bar colours from the centre outward.
const juce::Colour kSideColours[pitch::kBarsPerSide] = {theme::kBrandBlue, theme::kBrandYellow, theme::kBrandYellow,
                                                        theme::kBrandRed,  theme::kBrandRed,    theme::kBrandRed};
constexpr int kBarsW = pitch::kBarsPerSide * TunerView::kBarW + (pitch::kBarsPerSide - 1) * TunerView::kBarGap;
constexpr int kRowW = 2 * kBarsW + 2 * TunerView::kSideGap + TunerView::kCentreW;
constexpr int kColumnH = 2 * TunerView::kTriangleH + 2 * TunerView::kCentreGap + static_cast<int>(TunerView::kNotePx);
// Accidental: 0.35em hung off the letter's right edge, 0.05em down.
constexpr float kAccidentalEm = 0.35f, kAccidentalTopEm = 0.05f;
// Frequency line: 13px mono pulled 6px up into the letter's descender space.
constexpr float kSubPx = 13;
constexpr int kSubLift = 6;
constexpr int kTriangleFadeMs = 90;

// Bars lit per side: flat lights the left, sharp the right, in tune both blues.
struct Lit {
  int left = 0, right = 0;
};
Lit litFor(const TunerFeed::State& s) {
  if (!s.hasSignal) return {};
  const float absCents = std::abs(s.cents);
  if (absCents <= pitch::kInTuneCents) return {1, 1};
  const int n = pitch::litCount(absCents);
  return s.cents < 0 ? Lit{n, 0} : Lit{0, n};
}

// "329.6 Hz +12¢", the line under the letter.
juce::String subText(const TunerFeed::State& s) {
  const int cents = juce::roundToInt(s.cents);
  return juce::String(s.frequency, 1) + " Hz " + (cents >= 0 ? "+" : "") + juce::String(cents) +
         juce::String::fromUTF8("\xc2\xa2");
}
}  // namespace

// 61×53 BRAND_BLUE triangle, faded in/out over 90ms like the web's opacity
// transition.
class TunerView::Triangle : public juce::Component {
public:
  explicit Triangle(bool pointsUp) : up_(pointsUp) {
    setInterceptsMouseClicks(false, false);
    tween_.snap(0);
  }
  void setLit(bool lit) {
    if (lit == lit_) return;
    lit_ = lit;
    tween_.animateTo(lit ? 1.0f : 0.0f, kTriangleFadeMs);
  }
  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    juce::Path p;
    if (up_) {
      p.addTriangle(b.getCentreX(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getBottom());
    } else {
      p.addTriangle(b.getX(), b.getY(), b.getRight(), b.getY(), b.getCentreX(), b.getBottom());
    }
    g.setColour(theme::kBrandBlue);
    g.fillPath(p);
  }

private:
  bool up_, lit_ = false;
  AlphaTween tween_{*this};
};

TunerView::TunerView(Services& services)
    : feed_(services.backend, services.clock), up_(std::make_unique<Triangle>(true)), down_(std::make_unique<Triangle>(false)) {
  setOpaque(true);
  close_.setName("Close tuner");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(close_);
  addAndMakeVisible(*up_);
  addAndMakeVisible(*down_);
  feed_.onChange = [this] { feedChanged(); };
  feedChanged();
}

TunerView::~TunerView() = default;

// The feed notifies on every smoothed-cents change, most of which move
// nothing visible; only the parts whose drawn state changed get dirtied,
// never the whole takeover.
void TunerView::feedChanged() {
  const auto& s = feed_.state();
  const bool inTune = s.hasSignal && std::abs(s.cents) <= pitch::kInTuneCents;
  const bool flat = s.hasSignal && s.cents < -pitch::kInTuneCents;
  const bool sharp = s.hasSignal && s.cents > pitch::kInTuneCents;
  // Top triangle points down: "tune down" when sharp; bottom points up when flat.
  down_->setLit(inTune || sharp);
  up_->setLit(inTune || flat);

  const auto lit = litFor(s);
  if (lit.left != shownLeftLit_) {
    shownLeftLit_ = lit.left;
    repaint(leftRow_);
  }
  if (lit.right != shownRightLit_) {
    shownRightLit_ = lit.right;
    repaint(rightRow_);
  }
  const auto readout = s.hasSignal ? s.note + "\n" + subText(s) : juce::String();
  if (readout != shownReadout_) {
    shownReadout_ = readout;
    repaint(readoutArea());
  }
}

// The note box plus where its text spills: the accidental hangs off the
// letter's right edge and the frequency line runs below the box.
juce::Rectangle<int> TunerView::readoutArea() const {
  return noteBox_.expanded(kCentreW / 2, 0).withHeight(noteBox_.getHeight() + 2 * static_cast<int>(kSubPx));
}

void TunerView::resized() {
  const auto area = getLocalBounds();
  close_.setBounds(area.getRight() - kCloseRight - kCloseBox, area.getY() + kCloseTop, kCloseBox, kCloseBox);

  const int x0 = area.getX() + (area.getWidth() - kRowW) / 2;
  const int y0 = area.getY() + (area.getHeight() - kColumnH) / 2;
  const int barY = y0 + design::snap((kColumnH - kBarH) / 2.0f);
  leftRow_ = {x0, barY, kBarsW, kBarH};
  rightRow_ = {x0 + kRowW - kBarsW, barY, kBarsW, kBarH};

  const int colX = x0 + kBarsW + kSideGap;
  const int triX = colX + (kCentreW - kTriangleW) / 2;
  down_->setBounds(triX, y0, kTriangleW, kTriangleH);
  noteBox_ = {colX, y0 + kTriangleH + kCentreGap, kCentreW, static_cast<int>(kNotePx)};
  up_->setBounds(triX, noteBox_.getBottom() + kCentreGap, kTriangleW, kTriangleH);
}

void TunerView::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  const auto& s = feed_.state();
  const auto lit = litFor(s);
  const auto clip = g.getClipBounds();
  if (clip.intersects(leftRow_)) paintBars(g, Side::left, leftRow_, lit.left);
  if (clip.intersects(rightRow_)) paintBars(g, Side::right, rightRow_, lit.right);
  if (s.hasSignal && clip.intersects(readoutArea())) paintReadout(g, noteBox_);
}

void TunerView::paintBars(juce::Graphics& g, Side side, juce::Rectangle<int> row, int litCount) const {
  for (int slot = 0; slot < pitch::kBarsPerSide; ++slot) {
    // Bars run outermost → innermost on the left, mirrored on the right.
    const int index = side == Side::left ? pitch::kBarsPerSide - 1 - slot : slot;
    const auto b = juce::Rectangle<int>(row.getX() + slot * (kBarW + kBarGap), row.getY(), kBarW, kBarH).toFloat();
    // The short edge faces the centre so both sides recede toward the note.
    const float outerX = side == Side::left ? b.getX() : b.getRight();
    const float innerX = side == Side::left ? b.getRight() : b.getX();
    juce::Path p;
    p.startNewSubPath(outerX, b.getY());
    p.lineTo(innerX, b.getY() + b.getHeight() * kTaperTop);
    p.lineTo(innerX, b.getY() + b.getHeight() * kTaperBottom);
    p.lineTo(outerX, b.getBottom());
    p.closeSubPath();
    g.setColour(index < litCount ? kSideColours[index] : theme::kSurfaceRaised);
    g.fillPath(p);
  }
}

void TunerView::paintReadout(juce::Graphics& g, juce::Rectangle<int> box) const {
  const auto& s = feed_.state();
  const auto letterFont = Fonts::sans(kNotePx, true);
  const auto letter = s.note.substring(0, 1);
  const auto accidental = s.note.substring(1);
  const float letterW = Fonts::width(letterFont, letter);
  const float letterX = box.getCentreX() - letterW / 2;
  const float top = static_cast<float>(box.getY());

  g.setColour(theme::kWhite);
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(letterFont, letter, letterX, top + Fonts::cssBaseline(letterFont, kNotePx));
  if (accidental.isNotEmpty()) {
    const float accPx = kNotePx * kAccidentalEm;
    const auto accFont = Fonts::sans(accPx, true);
    glyphs.addLineOfText(accFont, accidental, letterX + letterW,
                         top + kNotePx * kAccidentalTopEm + Fonts::cssBaseline(accFont, accPx));
  }
  glyphs.draw(g);

  // Frequency and cents, centred under the letter.
  const auto sub = subText(s);
  const auto subFont = Fonts::mono(kSubPx);
  const float subTop = static_cast<float>(box.getBottom() - kSubLift);
  const float subBaseline = subTop + Fonts::cssBaseline(subFont, static_cast<float>(Fonts::normalLineHeight(subFont)));
  juce::GlyphArrangement subGlyphs;
  subGlyphs.addLineOfText(subFont, sub, box.getCentreX() - Fonts::width(subFont, sub) / 2, subBaseline);
  g.setColour(theme::kGray);
  subGlyphs.draw(g);
}

}  // namespace t3k::ui
