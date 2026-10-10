#include "BlockEqView.h"

#include <cmath>

#include "EditableChip.h"
#include "core/AlphaTween.h"
#include "core/Design.h"
#include "core/EqMath.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "core/Wheel.h"
#include "widgets/SegmentedText.h"

namespace t3k::ui {

namespace {

using eq::kGraphH;
using eq::kGraphW;

constexpr int kBodyPadding = 16;
constexpr int kDimMs = 200;
// Controls stay black/white/gray (like the knobs); the only colour in the EQ
// is the brand-gradient spectrum behind everything.
const juce::Colour kCurveColour{0xff8e8e93};
const juce::Colour kAxisLabel = juce::Colour(235, 235, 245).withAlpha(0.35f);
constexpr float kAxisFontPx = 9;

// Live spectrum (SpectrumBackdrop.tsx): a filled area in the brand meter
// ramp, blue at the floor → yellow → red at the top, 30% opaque. Display
// window -80..0 dB across the graph height. Filled only: at idle every bin
// sits on the floor, so a stroked curve would draw a hairline there.
juce::Path spectrumArea(const std::vector<float>& bins) {
  constexpr float topDb = 0, bottomDb = -80;
  juce::Path area;
  area.startNewSubPath(0, kGraphH);
  for (size_t i = 0; i < bins.size(); ++i) {
    const float x = static_cast<float>(i) / static_cast<float>(bins.size() - 1) * kGraphW;
    const float t = juce::jlimit(0.0f, 1.0f, (bins[i] - bottomDb) / (topDb - bottomDb));
    area.lineTo(x, kGraphH * (1 - t));
  }
  area.lineTo(kGraphW, kGraphH);
  area.closeSubPath();
  return area;
}

juce::ColourGradient spectrumRamp() {
  juce::ColourGradient ramp(theme::kBrandBlue, 0, kGraphH, theme::kBrandRed, 0, 0, false);
  ramp.addColour(0.4, theme::kBrandYellow);
  ramp.addColour(0.7, theme::kBrandRed);
  return ramp;
}
constexpr double kGridFreqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
const char* const kGridLabels[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};

// Fixed grid of the graph view: log-spaced Hz rules with labels, ±7.5 dB
// rules and the 0 dB line.
void paintGrid(juce::Graphics& g) {
  for (size_t i = 0; i < std::size(kGridFreqs); ++i) {
    const float x = static_cast<float>(eq::freqToNorm(kGridFreqs[i]) * kGraphW);
    g.setColour(juce::Colour(235, 235, 245).withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(x - 0.5f, 0, 1, kGraphH));
    // SVG <text y> is the baseline.
    g.setColour(kAxisLabel);
    g.setFont(Fonts::sans(kAxisFontPx));
    g.drawSingleLineText(kGridLabels[i], juce::roundToInt(x + 4), kGraphH - kBodyPadding);
  }
  for (double db : {-7.5, 7.5}) {
    const float y = static_cast<float>(eq::gainToY(db));
    g.setColour(juce::Colour(235, 235, 245).withAlpha(0.05f));
    g.fillRect(juce::Rectangle<float>(0, y - 0.5f, kGraphW, 1));
  }
  const float zero = static_cast<float>(eq::gainToY(0));
  g.setColour(juce::Colour(235, 235, 245).withAlpha(0.18f));
  g.fillRect(juce::Rectangle<float>(0, zero - 0.5f, kGraphW, 1));
}

// Web WheelEvent / pointer deltas: Shift = 8x finer.
double fineFactor(const juce::ModifierKeys& mods) { return mods.isShiftDown() ? 1.0 / 8.0 : 1.0; }

// Layers of the body, bottom to top: grid, spectrum, then the editor
// (Graph or Sliders). The spectrum ticks 30x a second, so it is the only
// layer a tick invalidates; the others are rasterised once and blitted
// (setBufferedToImage), which keeps the grid's text out of the per-frame
// cost. Pure decoration: no mouse, no accessibility.
class GridLayer : public juce::Component {
public:
  GridLayer() {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
    setBufferedToImage(true);  // measured: the blit beats redrawing the labels
  }
  void paint(juce::Graphics& g) override {
    g.addTransform(juce::AffineTransform::scale(1.0f, eq::kSvgStretch));
    paintGrid(g);
  }
};

// A gradient fill costs CoreGraphics a per-pixel axial shade on every tick
// (a third of the EQ view's paint time, profiled). The ramp never changes,
// so it is rasterised once at device resolution, 30% opacity baked in, and
// each tick clips to the spectrum's area and blits it 1:1.
class SpectrumLayer : public juce::Component {
public:
  explicit SpectrumLayer(const SpectrumFeed& feed) : feed_(feed) {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
  }

  void paint(juce::Graphics& g) override {
    const auto& bins = feed_.bins();
    if (bins.size() < 2) return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    ensureRamp(scale);
    g.reduceClipRegion(spectrumArea(bins), juce::AffineTransform::scale(1.0f, eq::kSvgStretch));
    g.drawImageTransformed(ramp_, juce::AffineTransform::scale(1.0f / scale));
  }

private:
  void ensureRamp(float scale) {
    if (ramp_.isValid() && juce::exactlyEqual(rampScale_, scale)) return;
    rampScale_ = scale;
    ramp_ = juce::Image(juce::Image::ARGB, juce::roundToInt(kGraphW * scale),
                        juce::roundToInt(eq::kBodyH * scale), true);
    juce::Graphics g(ramp_);
    g.addTransform(juce::AffineTransform::scale(scale, scale * eq::kSvgStretch));
    g.setGradientFill(spectrumRamp());
    g.setOpacity(0.3f);
    g.fillRect(juce::Rectangle<float>(0, 0, kGraphW, kGraphH));
  }

  const SpectrumFeed& feed_;
  juce::Image ramp_;
  float rampScale_ = 0;
};

}  // namespace

// Graph view
class BlockEqView::Graph : public juce::Component {
public:
  explicit Graph(BlockEqView& owner) : owner_(owner) {
    for (auto* chip : {&freq_, &gain_, &q_}) chrome_.addAndMakeVisible(*chip);
    freq_.onCommit = [this](const juce::String& raw) { commitFreq(raw); };
    gain_.onCommit = [this](const juce::String& raw) { commitGain(raw); };
    q_.onCommit = [this](const juce::String& raw) { commitQ(raw); };
    addAndMakeVisible(chrome_);
    rebuildTypeSelector();
    setSize(kGraphW, eq::kBodyH);
  }

  void bandsChanged() {
    const auto& bands = owner_.bands();
    if (selected_ >= static_cast<int>(bands.size())) {
      selected_ = 0;
      rebuildTypeSelector();
    } else {
      syncTypeSelector();
    }
    syncChips();
    repaint();
  }

  void enabledChanged(bool animate) {
    const bool on = owner_.eqEnabled();
    fade_.animateTo(on ? 1.0f : theme::kDisabledOpacity, kDimMs, animate);
    setInterceptsMouseClicks(on, on);
  }

  void paint(juce::Graphics& g) override {
    const auto& bands = owner_.bands();
    paintSvg(g, bands);

    // Band readout, top-left (HTML, so unstretched): "Band N · Type" with a
    // soft text shadow.
    if (!bands.empty()) {
      const auto font = Fonts::sans(11);
      const juce::String band = "Band " + juce::String(selected_ + 1);
      const juce::String rest = juce::String::fromUTF8(" \xC2\xB7 ") + eq::typeLabel(bands[static_cast<size_t>(selected_)].type);
      const int bandW = juce::roundToInt(Fonts::width(font, band));
      const juce::Rectangle<int> row(kBodyPadding, kBodyPadding, 300, juce::roundToInt(11 * 1.2f));
      g.setFont(font);
      g.setColour(juce::Colours::black.withAlpha(0.9f));
      g.drawText(band + rest, row.translated(0, 1), juce::Justification::centredLeft, false);
      g.setColour(theme::kWhite);
      g.drawText(band, row, juce::Justification::centredLeft, false);
      g.setColour(theme::kMuted);
      g.drawText(rest, row.translated(bandW, 0), juce::Justification::centredLeft, false);
    }
  }

  // The SVG layer: curve and dots in graph space, stretched over the body.
  void paintSvg(juce::Graphics& g, const std::vector<EqBand>& bands) {
    const juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::scale(1.0f, eq::kSvgStretch));
    // Curve: one sample per horizontal pixel so even a Q=10 spike is drawn
    // cleanly (coarse sampling clips the notch into a flat-bottomed wedge).
    static const std::vector<double> freqs = [] {
      std::vector<double> f;
      for (int i = 0; i < kGraphW; ++i) f.push_back(eq::normToFreq(i / static_cast<double>(kGraphW - 1)));
      return f;
    }();
    const auto response = eq::responseDb(bands, owner_.sampleRate(), freqs);
    juce::Path line;
    for (int i = 0; i < kGraphW; ++i) {
      const float x = static_cast<float>(i) / static_cast<float>(kGraphW - 1) * kGraphW;
      const float y = static_cast<float>(juce::jlimit(-8.0, kGraphH + 8.0, eq::gainToY(response[static_cast<size_t>(i)])));
      if (i == 0)
        line.startNewSubPath(x, y);
      else
        line.lineTo(x, y);
    }
    auto area = line;
    const float zeroY = static_cast<float>(eq::gainToY(0));
    area.lineTo(kGraphW, zeroY);
    area.lineTo(0, zeroY);
    area.closeSubPath();
    g.setColour(kCurveColour.withAlpha(0.1f));
    g.fillPath(area);
    g.setColour(kCurveColour);
    g.strokePath(line, juce::PathStrokeType(1.25f));

    for (size_t i = 0; i < bands.size(); ++i) {
      const auto c = dotCentre(static_cast<int>(i));
      const bool sel = static_cast<int>(i) == selected_;
      if (sel) {
        g.setColour(theme::kWhite.withAlpha(0.9f));
        g.drawEllipse(juce::Rectangle<float>(18, 18).withCentre(c), 1.5f);
      }
      const auto dot = juce::Rectangle<float>(11, 11).withCentre(c);
      g.setColour(sel ? theme::kWhite : juce::Colour(0xffb8b8be));
      g.fillEllipse(dot);
      g.setColour(theme::kBlack);
      g.drawEllipse(dot, 1.5f);
    }
  }

  void resized() override {
    // Bottom-left floating row, 12px above the BODY_PADDING floor (of the
    // 275px body) so the chips clear the Hz axis labels.
    int x = 0;
    const int h = theme::kTextBoxHeight;
    typeSel_->setTopLeftPosition(x, 0);
    x += typeSel_->getWidth() + kChromeGap;
    for (auto* chip : {&freq_, &gain_, &q_}) {
      chip->setTopLeftPosition(x, 0);
      x += chip->getWidth() + kChromeGap;
    }
    chrome_.setBounds(kBodyPadding, eq::kBodyH - (kBodyPadding + 12) - h, x - kChromeGap, h);
  }

  // Dots
  void mouseMove(const juce::MouseEvent& e) override {
    const bool over = dotAt(graphPoint(e)).has_value();
    setMouseCursor(over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    setHelpText(over ? help::text(help::Key::eqDot) : juce::String());
  }

  void mouseDown(const juce::MouseEvent& e) override {
    const auto hit = dotAt(graphPoint(e));
    setViewportIgnoreDragFlag(hit.has_value());  // a dot drag edits; the graph around it pans
    if (!hit) return;
    select(*hit);
    // Touch: the second tap of a double tap resets and ends the gesture.
    if (e.source.isTouch() && e.getNumberOfClicks() == 2) {
      resetBand(*hit);
      return;
    }
    if (e.mods.isAltDown()) {
      resetBand(*hit);
      return;
    }
    drag_ = Drag{*hit, graphPoint(e).toDouble()};
    owner_.setDragging(true);
    pin_ = std::make_unique<HintPin>(owner_.services().hints, help::text(help::Key::eqDot));
  }

  // Delta-based (not absolute) so Shift = 8x finer can toggle mid-drag
  // without the dot jumping.
  void mouseDrag(const juce::MouseEvent& e) override {
    if (!drag_) return;
    const auto& bands = owner_.bands();
    const int index = drag_->index;
    if (index >= static_cast<int>(bands.size())) return;
    const auto p = graphPoint(e).toDouble();
    const double fine = fineFactor(e.mods);
    const double dX = (p.x - drag_->last.x) * fine;
    const double dGain = (eq::yToGain(p.y) - eq::yToGain(drag_->last.y)) * fine;
    const double dY = (p.y - drag_->last.y) * fine;
    drag_->last = p;

    auto band = bands[static_cast<size_t>(index)];
    const auto [lo, hi] = owner_.freqRange(index);
    const double norm = juce::jlimit(0.0, 1.0, eq::freqToNorm(band.freqHz) + dX / kGraphW);
    band.freqHz = juce::jlimit(lo, hi, eq::normToFreq(norm));
    if (eq::hasGain(band.type)) {
      band.gainDb = juce::jlimit(-kEqMaxAbsGainDb, kEqMaxAbsGainDb, band.gainDb + dGain);
    } else {
      // Cuts have no gain, so vertical drag tunes Q instead (up = tighter).
      band.q = juce::jlimit(kEqMinQ, kEqMaxQ, band.q * std::exp(-dY * 0.02));
    }
    owner_.updateBand(index, band);
  }

  void mouseUp(const juce::MouseEvent&) override {
    if (!drag_) return;
    drag_.reset();
    pin_.reset();
    owner_.setDragging(false);
  }

  // Wheel tunes the selected band's Q.
  void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
    const auto& bands = owner_.bands();
    if (selected_ >= static_cast<int>(bands.size())) return;
    auto band = bands[static_cast<size_t>(selected_)];
    band.q = juce::jlimit(kEqMinQ, kEqMaxQ, band.q * std::exp(-wheel::pixelsY(wheel) * 0.003 * fineFactor(e.mods)));
    owner_.updateBand(selected_, band);
  }

private:
  static constexpr int kChromeGap = 8;
  static constexpr float kDotRadius = 5.5f;

  struct Drag {
    int index;
    juce::Point<double> last;
  };

  // Pointer position in graph (SVG viewBox) space.
  static juce::Point<float> graphPoint(const juce::MouseEvent& e) {
    return {e.position.x, e.position.y / eq::kSvgStretch};
  }

  juce::Point<float> dotCentre(int index) const {
    const auto& band = owner_.bands()[static_cast<size_t>(index)];
    const double cy = eq::hasGain(band.type) ? eq::gainToY(band.gainDb) : eq::gainToY(0);
    return {static_cast<float>(eq::freqToNorm(band.freqHz) * kGraphW), static_cast<float>(cy)};
  }

  // Topmost (last drawn) dot under the point.
  std::optional<int> dotAt(juce::Point<float> p) const {
    const int n = static_cast<int>(owner_.bands().size());
    for (int i = n - 1; i >= 0; --i)
      if (dotCentre(i).getDistanceFrom(p) <= kDotRadius + 0.75f) return i;
    return std::nullopt;
  }

  void select(int index) {
    if (selected_ == index) return;
    selected_ = index;
    rebuildTypeSelector();
    syncChips();
    repaint();
  }

  // Alt/Option-click (touch: double tap): neutralise the band's effect
  // (gain or Q) while keeping its frequency.
  void resetBand(int index) {
    auto band = owner_.bands()[static_cast<size_t>(index)];
    if (eq::hasGain(band.type))
      band.gainDb = 0;
    else
      band.q = eq::typeDefaultQ(band.type).value_or(0.71);
    owner_.updateBand(index, band);
  }

  // Curve type: outer bands choose shelf vs pass; bells show their single
  // option so the selected shape is always visible. The cells follow the
  // selected band; a type change only moves the selection, so a click never
  // rebuilds the control it came from (that would free its running closure).
  void rebuildTypeSelector() {
    const auto options = eqBandTypeOptions(selected_);
    std::vector<SegmentedText::Cell> cells;
    for (auto type : options)
      cells.emplace_back(eq::typeLabel(type), help::bandType(eq::typeLabel(type)), eq::typeGlyphSvg(type),
                         static_cast<float>(theme::kIconSize));
    typeSel_ = std::make_unique<SegmentedText>(cells, SegmentedText::selection());
    typeSel_->setInteractive(options.size() > 1);
    typeSel_->onCellClick = [this, options](int i) { changeType(options[static_cast<size_t>(i)]); };
    chrome_.addAndMakeVisible(*typeSel_);
    syncTypeSelector();
    resized();
  }

  void syncTypeSelector() {
    const auto& bands = owner_.bands();
    if (bands.empty()) return;
    const auto options = eqBandTypeOptions(selected_);
    for (size_t i = 0; i < options.size(); ++i)
      if (options[i] == bands[static_cast<size_t>(selected_)].type) typeSel_->select(static_cast<int>(i));
  }

  void changeType(EqBandType type) {
    auto band = owner_.bands()[static_cast<size_t>(selected_)];
    band.type = type;
    if (auto q = eq::typeDefaultQ(type)) band.q = *q;
    owner_.updateBand(selected_, band);
  }

  // Value widths fit each chip's longest reading ("999 Hz", "-15.0 dB",
  // "10.00") so the row never shifts.
  void syncChips() {
    const auto& bands = owner_.bands();
    if (bands.empty()) return;
    const auto& band = bands[static_cast<size_t>(selected_)];
    freq_.setText(eq::formatFreq(band.freqHz), juce::String(juce::roundToInt(band.freqHz)));
    const bool gain = eq::hasGain(band.type);
    gain_.setText(gain ? juce::String(band.gainDb, 1) + " dB" : juce::String::fromUTF8("\xE2\x80\x94"),
                  juce::String(band.gainDb, 1));
    gain_.setDisabledLook(!gain);
    q_.setText(juce::String(band.q, 2), juce::String(band.q, 2));
  }

  void commitFreq(const juce::String& raw) {
    const auto parsed = eq::parseFreq(raw);
    if (!parsed) return;
    auto band = owner_.bands()[static_cast<size_t>(selected_)];
    const auto [lo, hi] = owner_.freqRange(selected_);
    band.freqHz = juce::jlimit(lo, hi, *parsed);
    owner_.updateBand(selected_, band);
  }
  void commitGain(const juce::String& raw) {
    const auto parsed = eq::parseDecimal(raw);
    if (!parsed) return;
    auto band = owner_.bands()[static_cast<size_t>(selected_)];
    band.gainDb = juce::jlimit(-kEqMaxAbsGainDb, kEqMaxAbsGainDb, *parsed);
    owner_.updateBand(selected_, band);
  }
  void commitQ(const juce::String& raw) {
    const auto parsed = eq::parseDecimal(raw);
    if (!parsed) return;
    auto band = owner_.bands()[static_cast<size_t>(selected_)];
    band.q = juce::jlimit(kEqMinQ, kEqMaxQ, *parsed);
    owner_.updateBand(selected_, band);
  }

  BlockEqView& owner_;
  int selected_ = 1;
  std::optional<Drag> drag_;
  std::unique_ptr<HintPin> pin_;
  juce::Component chrome_;
  std::unique_ptr<SegmentedText> typeSel_;
  EditableChip freq_{"Freq", 42, help::Key::eqFreqChip};
  EditableChip gain_{"Gain", 52, help::Key::eqGainChip};
  EditableChip q_{"Q", 34, help::Key::eqQChip};
  AlphaTween fade_{*this};
};

// Sliders view
class BlockEqView::Sliders : public juce::Component, private juce::Timer {
public:
  explicit Sliders(BlockEqView& owner) : owner_(owner) { setSize(kGraphW, eq::kBodyH); }

  void bandsChanged() { repaint(); }

  void enabledChanged(bool animate) {
    const bool on = owner_.eqEnabled();
    fade_.animateTo(on ? 1.0f : theme::kDisabledOpacity, kDimMs, animate);
    setInterceptsMouseClicks(on, on);
  }

  void paint(juce::Graphics& g) override {
    const auto& bands = owner_.bands();
    const auto travel = travelBounds();

    // dB grid + numerals, spanning the travel edge to edge. Numerals sit
    // just under the lines in the left gutter.
    for (int db : {15, 10, 5, 0, -5, -10, -15}) {
      const float y = std::round(yForGain(db));
      g.setColour(juce::Colour(235, 235, 245).withAlpha(db == 0 ? 0.18f : 0.06f));
      g.fillRect(juce::Rectangle<float>(0, y, kGraphW, 1));
      g.setColour(kAxisLabel);
      g.setFont(Fonts::sans(kAxisFontPx));
      const juce::String label = db > 0 ? "+" + juce::String(db) : juce::String(db);
      // translateY(2rem) under the rule; JUCE's ascent sits a pixel lower
      // than the browser's line box, hence +1.
      g.drawText(label, juce::Rectangle<int>(kBodyPadding, juce::roundToInt(y) + 1, 40, juce::roundToInt(kAxisFontPx)),
                 juce::Justification::topLeft, false);
    }

    for (size_t i = 0; i < bands.size(); ++i) {
      const auto& band = bands[i];
      const bool editable = eq::hasGain(band.type);
      const float cx = columnCentre(static_cast<int>(i));
      const double gain = editable ? band.gainDb : 0.0;
      const float t = static_cast<float>((gain + kEqMaxAbsGainDb) / (2 * kEqMaxAbsGainDb));
      if (!editable) g.beginTransparencyLayer(theme::kDisabledOpacity);

      // Track spans exactly -15..+15.
      const auto track = juce::Rectangle<float>(kTrackW, static_cast<float>(travel.getHeight()))
                             .withCentre({cx, travel.toFloat().getCentreY()});
      g.setColour(juce::Colour(50, 50, 50).withAlpha(0.8f));
      g.fillRect(track);

      // Value fill: bottom of the travel up to the cap, dark → white toward
      // the cap, exactly like the knob sweep.
      if (t > 0.001f) {
        const float h = travel.getHeight() * t;
        const auto fill = juce::Rectangle<float>(track.getX(), track.getBottom() - h, kTrackW, h);
        juce::ColourGradient ramp(juce::Colour(30, 30, 30).withAlpha(0.3f), 0, fill.getBottom(), theme::kWhite, 0,
                                  fill.getY(), false);
        ramp.addColour(0.2, juce::Colour(50, 50, 50).withAlpha(0.6f));
        ramp.addColour(0.4, juce::Colour(100, 100, 100).withAlpha(0.75f));
        ramp.addColour(0.6, juce::Colour(160, 160, 160).withAlpha(0.85f));
        ramp.addColour(0.8, juce::Colour(220, 220, 220).withAlpha(0.9f));
        g.setGradientFill(ramp);
        g.fillRect(fill);
      }

      // Fader cap (knob-handle style: white with a black centre line), on
      // whole pixels like the browser's div so its edges stay crisp.
      const float cy = static_cast<float>(travel.getY()) + travel.getHeight() * (1 - t);
      const auto cap = juce::Rectangle<int>(kCapW, kCapH).withCentre({design::snap(cx), design::snap(cy)});
      juce::ColourGradient capRamp(theme::kWhite, 0, static_cast<float>(cap.getY()), juce::Colour(0xffd6d6db), 0,
                                   static_cast<float>(cap.getBottom()), false);
      g.setGradientFill(capRamp);
      g.fillRect(cap);
      g.setColour(theme::kBlack);
      g.drawRect(cap, 1);
      g.fillRect(cap.getX() + 1, cap.getCentreY() - 1, kCapW - 2, 2);
      if (!editable) g.endTransparencyLayer();
    }

    // Frequency labels, centred under their fader. The outer bands carry
    // their curve glyph; a dragged band shows its live gain instead.
    const auto row = labelRow();
    for (size_t i = 0; i < bands.size(); ++i) {
      const auto& band = bands[i];
      const float cx = columnCentre(static_cast<int>(i));
      const bool readout = readoutIndex_ == static_cast<int>(i) && eq::hasGain(band.type);
      const auto font = Fonts::sans(kAxisFontPx);
      if (readout) {
        const juce::String text = (band.gainDb > 0 ? "+" : "") + juce::String(band.gainDb, 1) + " dB";
        paint::text(g, text, row.withWidth(200).withCentre({juce::roundToInt(cx), row.getCentreY()}), font,
                    theme::kWhite, juce::Justification::centred);
        continue;
      }
      const bool glyph = i == 0 || i + 1 == bands.size();
      const auto text = eq::formatFreq(band.freqHz);
      const float textW = Fonts::width(font, text);
      const float total = textW + (glyph ? kGlyphW + kGlyphGap : 0.0f);
      float x = cx - total / 2;
      if (glyph) {
        Icons::draw(g, eq::typeGlyphSvg(band.type),
                    juce::Rectangle<float>(kGlyphW, kGlyphH).withCentre({x + kGlyphW / 2, row.toFloat().getCentreY()}),
                    kAxisLabel);
        x += kGlyphW + kGlyphGap;
      }
      paint::text(g, text, juce::Rectangle<int>(juce::roundToInt(x), row.getY(), juce::roundToInt(textW) + 2, row.getHeight()),
                  font, kAxisLabel);
    }
  }

  void mouseMove(const juce::MouseEvent& e) override {
    const int col = columnAt(e.position);
    const bool editable = col >= 0 && eq::hasGain(owner_.bands()[static_cast<size_t>(col)].type);
    setMouseCursor(editable ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    setHelpText(col < 0 ? juce::String() : help::text(editable ? help::Key::eqFader : help::Key::eqFaderPass));
  }

  void mouseDown(const juce::MouseEvent& e) override {
    const int col = columnAt(e.position);
    const bool onFader = col >= 0 && eq::hasGain(owner_.bands()[static_cast<size_t>(col)].type);
    setViewportIgnoreDragFlag(onFader);  // a fader drag edits; the gaps pan
    if (!onFader) return;
    // Touch double tap / Alt-click: reset, and end the gesture there so the
    // grab-jump of a fresh drag doesn't move it straight back off 0.
    if ((e.source.isTouch() && e.getNumberOfClicks() == 2) || e.mods.isAltDown()) {
      setGain(col, 0);
      return;
    }
    drag_ = col;
    lastY_ = e.position.y;
    stopTimer();
    readoutIndex_ = col;
    pin_ = std::make_unique<HintPin>(owner_.services().hints, help::text(help::Key::eqFader));
    owner_.setDragging(true);
    // Plain grab jumps the cap to the pointer; a Shift-grab holds position
    // so fine adjustment starts from the current value.
    if (!e.mods.isShiftDown()) setGain(col, gainAt(e.position.y));
    repaint();
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    if (drag_ < 0) return;
    const float dY = e.position.y - lastY_;
    lastY_ = e.position.y;
    if (e.mods.isShiftDown()) {
      const double dGain = (-dY / travelBounds().getHeight()) * 2 * kEqMaxAbsGainDb / 8.0;
      setGain(drag_, owner_.bands()[static_cast<size_t>(drag_)].gainDb + dGain);
    } else {
      setGain(drag_, gainAt(e.position.y));
    }
  }

  void mouseUp(const juce::MouseEvent&) override {
    if (drag_ < 0) return;
    drag_ = -1;
    pin_.reset();
    owner_.setDragging(false);
    startTimer(kReadoutHoldMs);
  }

  void mouseDoubleClick(const juce::MouseEvent& e) override {
    const int col = columnAt(e.position);
    if (col >= 0 && eq::hasGain(owner_.bands()[static_cast<size_t>(col)].type)) setGain(col, 0);
  }

private:
  static constexpr int kSize = 30;  // fader scale (the card knobs' size prop)
  static constexpr int kCapW = kSize;
  static constexpr int kCapH = 14;  // round(size * 0.47)
  static constexpr float kTrackW = 8;  // max(4, round(size * 0.27))
  static constexpr int kThumbPad = 7;  // ceil(capH / 2): keeps the cap inside the travel
  static constexpr int kFreqRowH = 16;
  static constexpr int kAxisInset = 4;
  static constexpr float kGlyphW = 12, kGlyphH = 11, kGlyphGap = 4;
  static constexpr int kReadoutHoldMs = 250;

  // The fader travel (cap-centre range): the region under the top pad and
  // above the label row, less the thumb pad each end.
  juce::Rectangle<int> travelBounds() const {
    return {0, kBodyPadding + kThumbPad, kGraphW, eq::kBodyH - kBodyPadding - kAxisInset - kFreqRowH - 2 * kThumbPad};
  }
  juce::Rectangle<int> labelRow() const { return {0, eq::kBodyH - kAxisInset - kFreqRowH, kGraphW, kFreqRowH}; }
  float columnWidth() const { return static_cast<float>(kGraphW - 2 * kBodyPadding) / kEqNumBands; }
  float columnCentre(int i) const { return kBodyPadding + (static_cast<float>(i) + 0.5f) * columnWidth(); }
  int columnAt(juce::Point<float> p) const {
    const auto region = juce::Rectangle<int>(kBodyPadding, kBodyPadding, kGraphW - 2 * kBodyPadding,
                                             eq::kBodyH - kBodyPadding - kAxisInset - kFreqRowH);
    if (!region.toFloat().contains(p)) return -1;
    const int col = static_cast<int>((p.x - kBodyPadding) / columnWidth());
    return juce::jlimit(0, static_cast<int>(owner_.bands().size()) - 1, col);
  }
  float yForGain(double db) const {
    const auto travel = travelBounds();
    return static_cast<float>(travel.getY()) + static_cast<float>(travel.getHeight()) *
                                                   static_cast<float>(1 - (db + kEqMaxAbsGainDb) / (2 * kEqMaxAbsGainDb));
  }
  double gainAt(float y) const {
    const auto travel = travelBounds();
    const double t = juce::jlimit(0.0, 1.0, (y - travel.getY()) / static_cast<double>(std::max(1, travel.getHeight())));
    return (1 - t) * 2 * kEqMaxAbsGainDb - kEqMaxAbsGainDb;
  }
  void setGain(int index, double gainDb) {
    auto band = owner_.bands()[static_cast<size_t>(index)];
    band.gainDb = juce::jlimit(-kEqMaxAbsGainDb, kEqMaxAbsGainDb, gainDb);
    owner_.updateBand(index, band);
  }
  void timerCallback() override {
    stopTimer();
    readoutIndex_ = -1;
    repaint();
  }

  BlockEqView& owner_;
  int drag_ = -1;
  float lastY_ = 0;
  int readoutIndex_ = -1;
  std::unique_ptr<HintPin> pin_;
  AlphaTween fade_{*this};
};

// BlockEqView
BlockEqView::BlockEqView(Services& services, std::string blockId)
    : services_(services),
      blockId_(std::move(blockId)),
      feed_(services.backend, services.clock, blockId_),
      grid_(std::make_unique<GridLayer>()),
      spectrum_(std::make_unique<SpectrumLayer>(feed_)),
      graph_(std::make_unique<Graph>(*this)),
      sliders_(std::make_unique<Sliders>(*this)) {
  // Every pixel is painted here (black under the layers), so nothing
  // beneath the view needs repainting on a spectrum tick.
  setOpaque(true);
  feed_.onChange = [this] { spectrum_->repaint(); };
  // The spectrum repaints 30x a second; the curve and slider layers above
  // it only change on interaction, so they blit from a cache.
  graph_->setBufferedToImage(true);
  sliders_->setBufferedToImage(true);
  addChildComponent(*grid_);
  addAndMakeVisible(*spectrum_);
  addChildComponent(*graph_);
  addAndMakeVisible(*sliders_);
  setSize(kGraphW, eq::kBodyH);
}

BlockEqView::~BlockEqView() = default;

void BlockEqView::setBands(const std::vector<EqBand>& bands) {
  if (dragging_) return;
  bands_ = bands;
  graph_->bandsChanged();
  sliders_->bandsChanged();
}

void BlockEqView::setEqEnabled(bool enabled) {
  const bool first = !isShowing();
  enabled_ = enabled;
  graph_->enabledChanged(!first);
  sliders_->enabledChanged(!first);
}

void BlockEqView::setView(View view) {
  view_ = view;
  grid_->setVisible(view == View::graph);
  graph_->setVisible(view == View::graph);
  sliders_->setVisible(view == View::sliders);
  repaint();
}

void BlockEqView::setSampleRate(double sampleRate) {
  if (juce::exactlyEqual(sampleRate_, sampleRate)) return;
  sampleRate_ = sampleRate;
  graph_->repaint();
}

void BlockEqView::updateBand(int index, const EqBand& band) {
  if (index < 0 || index >= static_cast<int>(bands_.size())) return;
  bands_[static_cast<size_t>(index)] = band;
  services_.chain.setBlockEqBand(blockId_, index, band);
  graph_->bandsChanged();
  sliders_->bandsChanged();
}

std::pair<double, double> BlockEqView::freqRange(int index) const {
  const double lo = index > 0 ? bands_[static_cast<size_t>(index - 1)].freqHz * 1.02 : kEqMinFreqHz;
  const double hi = index + 1 < static_cast<int>(bands_.size()) ? bands_[static_cast<size_t>(index + 1)].freqHz * 0.98
                                                               : kEqMaxFreqHz;
  return {lo, hi};
}

void BlockEqView::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void BlockEqView::resized() {
  for (auto* layer : {grid_.get(), spectrum_.get(), static_cast<juce::Component*>(graph_.get()),
                      static_cast<juce::Component*>(sliders_.get())})
    layer->setBounds(getLocalBounds());
}

}  // namespace t3k::ui
