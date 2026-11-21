#include "ImageDeckPanel.h"

#include <functional>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "services/MeterStore.h"

namespace t3k::ui {

namespace {

constexpr float kWobbleDefault = 0.25f;
// Centre of the log 32.5-520 Hz map = 130 Hz.
constexpr float kCrossoverDefault = 0.5f;
struct PowerDefaults {
  bool wobble, crossover, diffuse;
};
constexpr PowerDefaults powerDefaults(ImageFeature f) {
  return f == ImageFeature::spread ? PowerDefaults{true, true, true} : PowerDefaults{false, false, false};
}

constexpr int kPadTop = 14, kPadSide = 16, kPadBottom = 8;
// Equal section columns so the gap between Wobble / Crossover / Diffuse reads
// evenly. Sized for the longest label ("Crossover"); shorter labels centre.
constexpr int kSectionWidth = 82;
constexpr int kSectionGap = 18;
constexpr int kLedSize = 6, kLedInset = 8;

struct DeckHelp {
  help::Key wobble, wobblePower, crossover, crossoverPower, diffuse;
};
constexpr DeckHelp deckHelp(ImageFeature f) {
  using K = help::Key;
  return f == ImageFeature::spread
             ? DeckHelp{K::spreadWobble, K::spreadWobblePower, K::spreadCrossover,
                        K::spreadCrossoverPower, K::spreadDiffuse}
             : DeckHelp{K::alignWobble, K::alignWobblePower, K::alignCrossover,
                        K::alignCrossoverPower, K::alignDiffuse};
}

Knob::Options sectionKnob(const juce::String& label, const KnobScale& scale, float def, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  return o;
}

}  // namespace

juce::String imageFeaturePrefix(ImageFeature feature) {
  return feature == ImageFeature::spread ? "spread" : "align";
}

// LED
// Mono-safety LED. Below 0.5 correlation a mono fold-down audibly thins;
// below 0 it actively cancels.
class ImageDeckPanel::Led : public juce::Component, private MeterStore::Listener {
public:
  // The three colours as states, for screen readers (the colour is the only
  // cue on screen).
  enum class State { safe, caution, cancels };

  explicit Led(MeterStore& meters) : meters_(meters) {
    setHelpText(help::text(help::Key::imageCorrelation));
    setTitle("Mono safety");
    state_ = stateFor(meters_.correlation());
    meters_.addListener(this);
  }
  ~Led() override { meters_.removeListener(this); }
  void paint(juce::Graphics& g) override {
    const float c = meters_.correlation();
    g.setColour(c < 0 ? theme::kBrandRed : c < 0.5f ? theme::kBrandYellow : theme::kSubtle);
    g.fillEllipse(getLocalBounds().toFloat());
  }

  static State stateFor(float c) { return c < 0 ? State::cancels : c < 0.5f ? State::caution : State::safe; }
  static juce::String spoken(State s) {
    switch (s) {
      case State::cancels: return "Cancels in mono";
      case State::caution: return "Thins in mono";
      case State::safe: break;
    }
    return "Mono safe";
  }
  State state() const { return state_; }

  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  // Correlation arrives in 0.05 steps several times a second; only a change
  // of colour notifies, and only a worsening one is announced, at most
  // every few seconds, so a signal hovering at a threshold stays quiet.
  void correlationChanged() override {
    repaint();
    const auto next = stateFor(meters_.correlation());
    if (next == state_) return;
    const bool worse = static_cast<int>(next) > static_cast<int>(state_);
    state_ = next;
    if (auto* handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    const auto now = juce::Time::getMillisecondCounter();
    if (worse && isShowing() && (lastAnnounceMs_ == 0 || now - lastAnnounceMs_ >= kAnnounceGapMs)) {
      lastAnnounceMs_ = now;
      help::announce(spoken(next));
    }
  }

  static constexpr juce::uint32 kAnnounceGapMs = 5000;
  MeterStore& meters_;
  State state_ = State::safe;
  juce::uint32 lastAnnounceMs_ = 0;
};

namespace {
// The LED as read-only text: "Mono safety, Mono safe".
class LedValue : public juce::AccessibilityTextValueInterface {
public:
  explicit LedValue(std::function<juce::String()> read) : read_(std::move(read)) {}
  bool isReadOnly() const override { return true; }
  juce::String getCurrentValueAsString() const override { return read_(); }
  void setValueAsString(const juce::String&) override {}

private:
  std::function<juce::String()> read_;
};
}  // namespace

std::unique_ptr<juce::AccessibilityHandler> ImageDeckPanel::Led::createAccessibilityHandler() {
  juce::AccessibilityHandler::Interfaces interfaces;
  interfaces.value = std::make_unique<LedValue>([this] { return spoken(state_); });
  // Spoken help: what the states mean, in words, without the hint's symbols.
  struct Handler : juce::AccessibilityHandler {
    Handler(juce::Component& led, Interfaces interfaces)
        : juce::AccessibilityHandler(led, juce::AccessibilityRole::staticText, juce::AccessibilityActions(),
                                     std::move(interfaces)) {}
    juce::String getHelp() const override { return "Live check of how the output survives a mono sum."; }
  };
  return std::make_unique<Handler>(*this, std::move(interfaces));
}

// Section
ImageDeckPanel::Section::Section(Services& services, const juce::String& param,
                                 Knob::Options options, help::Key powerHelp)
    : knob(services.backend, param, std::move(options)),
      power(services.backend, param + "Enabled", powerHelp) {
  dim.addAndMakeVisible(knob);
  dim.setOff(!power.value(), false);
  power.onValueChange = [this](bool on) { dim.setOff(!on); };
}

void ImageDeckPanel::Section::layout(juce::Rectangle<int> column) {
  // The knob is centred in the column (not the knob + power cluster), and
  // takes the column's full width so its nowrap label can overflow the face
  // symmetrically ("Crossover" is wider than a 60px knob).
  const int size = knob.knobSize();
  const int height = Knob::heightFor(size);
  dim.setBounds(column.getX(), column.getBottom() - height + Knob::kEditorOverflow,
                column.getWidth(), height);
  knob.setBounds(0, 0, column.getWidth(), height);
  power.setTopLeftPosition(column.getCentreX() + size / 2 + 6,
                           column.getY() + (size - theme::kIconBoxSize) / 2);
}

// Panel
ImageDeckPanel::ImageDeckPanel(Services& services, ImageFeature feature)
    : wobble_(services, imageFeaturePrefix(feature) + "Wobble",
              sectionKnob("Wobble", scales::percent(), kWobbleDefault, deckHelp(feature).wobble),
              deckHelp(feature).wobblePower),
      crossover_(services, imageFeaturePrefix(feature) + "Crossover",
                 sectionKnob("Crossover", scales::crossoverHz(), kCrossoverDefault,
                             deckHelp(feature).crossover),
                 deckHelp(feature).crossoverPower),
      diffusePower_(services.backend, imageFeaturePrefix(feature) + "DiffuseEnabled",
                    deckHelp(feature).diffuse),
      led_(std::make_unique<Led>(services.meters)) {
  for (auto* s : {&wobble_, &crossover_}) {
    addAndMakeVisible(s->dim);
    addAndMakeVisible(s->power);
  }
  addAndMakeVisible(diffusePower_);
  addAndMakeVisible(*led_);
  primaryOnly = true;          // right-click toggles the panel; don't dismiss on it
  dismissOnAnchorPress = true; // the anchor is the Offset knob, a control
  setSize(kWidth, kHeight);
}

ImageDeckPanel::~ImageDeckPanel() = default;

void ImageDeckPanel::resetDeck(Backend& backend, ImageFeature feature) {
  const auto prefix = imageFeaturePrefix(feature);
  const auto powers = powerDefaults(feature);
  ParamBinding(backend, prefix + "Wobble").set(kWobbleDefault);
  ParamBinding(backend, prefix + "WobbleEnabled").set(powers.wobble);
  ParamBinding(backend, prefix + "Crossover").set(kCrossoverDefault);
  ParamBinding(backend, prefix + "CrossoverEnabled").set(powers.crossover);
  ParamBinding(backend, prefix + "DiffuseEnabled").set(powers.diffuse);
}

void ImageDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);

  // Diffuse has no continuous control, just its power; same column width and
  // knob-box/label geometry as the knob sections so the label baseline reads
  // evenly.
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);
  const int labelTop = content.getY() + theme::kKnobSizeSecondary + theme::kKnobLabelGap;
  const auto diffuseColumn =
      juce::Rectangle<int>(content.getX() + 2 * (kSectionWidth + kSectionGap), labelTop,
                           kSectionWidth, 17);
  paint::text(g, "Diffuse", diffuseColumn, Fonts::sans(14), theme::kGray, juce::Justification::centred);
}

void ImageDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);

  auto column = [&](int i) {
    return juce::Rectangle<int>(content.getX() + i * (kSectionWidth + kSectionGap), content.getY(),
                                kSectionWidth, content.getHeight());
  };
  wobble_.layout(column(0));
  crossover_.layout(column(1));
  const auto diffuse = column(2);
  diffusePower_.setTopLeftPosition(diffuse.getCentreX() - theme::kIconBoxSize / 2,
                                   diffuse.getY() + (theme::kKnobSizeSecondary - theme::kIconBoxSize) / 2);
  led_->setBounds(contentBounds().getRight() - kLedInset - kLedSize, contentBounds().getY() + kLedInset,
                  kLedSize, kLedSize);
}

}  // namespace t3k::ui
