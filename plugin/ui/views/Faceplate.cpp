#include "Faceplate.h"

#include <array>

#include "core/CustomIcons.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/Popover.h"

namespace t3k::ui {

namespace {

// Faceplate.tsx: padding 16 30 (input/output align with the UV meters).
constexpr int kPadX = 30, kPadY = 16;
constexpr int kGroupGap = 10;  // knob ↔ companion within a group
constexpr int kToneGap = 24;   // between the three tone knobs
// Chrome boxes in a bottom-aligned row sit on the secondary-knob centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);

// Input mode trigger: [glyph 17][gap 3][chevron 10] + 2px padding + 1px border.
constexpr int kInputModeWidth = 1 + 2 + (theme::kIconSize + 3) + 3 + 10 + 2 + 1;
constexpr int kMenuWidth = 172;
constexpr int kMenuPadTop = 12, kMenuPadSide = 8, kMenuPadBottom = 8;
constexpr int kMenuTitleHeight = 13;  // 11px/600 line
constexpr int kMenuTitlePadBottom = 8;
constexpr int kMenuRowHeight = 33;    // 9px padding + 13px text line
constexpr int kMenuGap = 14;          // bottom: calc(100% + 14)

Knob::Options primaryKnob(const char* label, const KnobScale& scale, float def, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  return o;
}

Knob::Options secondaryKnob(const char* label, const KnobScale& scale, float def, help::Key help,
                            Knob::Variant variant = Knob::Variant::full) {
  auto o = primaryKnob(label, scale, def, help);
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.variant = variant;
  return o;
}

// The mode's glyph, shared by the trigger and the menu rows.
void drawModeGlyph(juce::Graphics& g, InputMode mode, juce::Rectangle<float> box, juce::Colour colour) {
  if (mode == InputMode::stereo) {
    Icons::draw(g, custom_icons::kInputStereo, juce::Rectangle<float>(17, 10).withCentre(box.getCentre()),
                colour);
    return;
  }
  g.setFont(Fonts::mono(11, true));
  g.setColour(colour);
  g.drawText(mode == InputMode::left ? "L" : "R", box, juce::Justification::centred, false);
}

struct ModeOption {
  InputMode mode;
  const char* label;
};
constexpr std::array<ModeOption, 3> kModeOptions{
    ModeOption{InputMode::stereo, "Stereo (L+R)"},
    ModeOption{InputMode::left, "Left"},
    ModeOption{InputMode::right, "Right"},
};

}  // namespace

// Input mode
// Which channels of a stereo source feed the plugin. The trigger (current
// glyph + down caret) opens a flat floating menu above the plate listing the
// three routings. Stereo (the default) shows the two-circle glyph; L/R take
// only that channel (mirrored onto both) and the trigger keeps the filled
// "engaged" look so a non-default routing is obvious at a glance. While a
// chain branch is active the chain has a single mono source, so the "Stereo"
// routing is unavailable (native enforces the same).
class Faceplate::InputModeButton : public Clickable {
public:
  explicit InputModeButton(Services& services) : Clickable("Input Mode"), services_(services) {
    setSize(kInputModeWidth, theme::kIconBoxSize);
    setHelpText(help::text(help::Key::inputMode));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
      if (menuOpen())
        closeMenu();
      else
        openMenu();
    };
  }

  void setState(InputMode mode, bool branched) {
    if (mode_ == mode && branched_ == branched) return;
    mode_ = mode;
    branched_ = branched;
    repaint();
    if (menuOpen()) {
      closeMenu();
      openMenu();
    }
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds().toFloat();
    if (mode_ != InputMode::stereo) paint::fill(g, box, theme::kIconBoxRadius, theme::kHighlight);
    auto content = getLocalBounds().reduced(1 + 2, 1);
    drawModeGlyph(g, mode_, content.removeFromLeft(theme::kIconSize + 3).toFloat(), theme::kWhite);
    content.removeFromLeft(3);
    Icons::draw(g, Icon::ChevronDown, juce::Rectangle<float>(10, 10).withCentre(content.toFloat().getCentre()),
                theme::kMuted);
  }

private:
  class Row : public Clickable {
  public:
    Row(const ModeOption& option, bool selected)
        : Clickable(option.label), option_(option), selected_(selected) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    InputMode mode() const { return option_.mode; }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
      if (highlighted) paint::fill(g, getLocalBounds().toFloat(), 8.0f, theme::kHighlight);
      const auto colour = selected_ ? theme::kWhite : theme::kMuted;
      auto content = getLocalBounds().reduced(12, 0);
      drawModeGlyph(g, option_.mode, content.removeFromLeft(theme::kIconBoxSize).toFloat(), colour);
      content.removeFromLeft(12);
      paint::text(g, option_.label, content, Fonts::sans(13), colour);
    }

  private:
    ModeOption option_;
    bool selected_;
  };

  class Menu : public Popover {
  public:
    Menu(InputMode mode, bool branched, std::function<void(InputMode)> onPick) {
      for (const auto& option : kModeOptions) {
        if (branched && option.mode == InputMode::stereo) continue;
        auto row = std::make_unique<Row>(option, option.mode == mode);
        row->onClick = [this, onPick, m = option.mode] {
          close();
          onPick(m);
        };
        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
      }
      setSize(kMenuWidth, kBorder * 2 + kMenuPadTop + kMenuTitleHeight + kMenuTitlePadBottom +
                              kMenuRowHeight * static_cast<int>(rows_.size()) + kMenuPadBottom);
    }
    void paint(juce::Graphics& g) override {
      const auto box = getLocalBounds().toFloat();
      paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
      paint::border(g, box, theme::kPanelCorner, theme::kBorder);
      auto title = contentBounds().reduced(kMenuPadSide, 0).removeFromTop(kMenuPadTop + kMenuTitleHeight);
      title.removeFromTop(kMenuPadTop);
      paint::text(g, "Input Mode", title.reduced(12, 0), Fonts::sans(11, true), theme::kSubtle);
    }
    void resized() override {
      auto area = contentBounds().reduced(kMenuPadSide, 0);
      area.removeFromTop(kMenuPadTop + kMenuTitleHeight + kMenuTitlePadBottom);
      for (auto& row : rows_) row->setBounds(area.removeFromTop(kMenuRowHeight));
    }

  private:
    std::vector<std::unique_ptr<Row>> rows_;
  };

  bool menuOpen() const { return menu_ != nullptr && menu_->isOpen(); }
  void closeMenu() {
    if (menu_ != nullptr) menu_->close();  // kept alive: we may be inside its row's click
  }
  // Rebuilt per open: the row set depends on `branched`.
  void openMenu() {
    if (auto* old = menu_.release()) juce::MessageManager::callAsync([old] { delete old; });
    menu_ = std::make_unique<Menu>(mode_, branched_,
                                   [this](InputMode mode) { services_.chain.setInputMode(mode); });
    menu_->open(*this, Popover::Align::left, kMenuGap, 0, Popover::Placement::above);
  }

  Services& services_;
  InputMode mode_ = InputMode::stereo;
  bool branched_ = false;
  std::unique_ptr<Menu> menu_;
};

// Plate
Faceplate::Faceplate(Services& services)
    : services_(services),
      input_(services.backend, "inputLevel",
             primaryKnob("Input", scales::gainDb(), 0.5f, help::Key::inputLevel)),
      inputMode_(std::make_unique<InputModeButton>(services)),
      gate_(services),
      transpose_(services),
      bass_(services.backend, "toneBass",
            primaryKnob("Bass", scales::tone(), 0.5f, help::Key::toneBass)),
      middle_(services.backend, "toneMid",
              primaryKnob("Middle", scales::tone(), 0.5f, help::Key::toneMiddle)),
      treble_(services.backend, "toneTreble",
              primaryKnob("Treble", scales::tone(), 0.5f, help::Key::toneTreble)),
      tonePower_(services.backend, "toneEqEnabled", help::Key::tonePower),
      spread_(services, ImageFeature::spread),
      align_(services, ImageFeature::align),
      autoBalance_(Icon::Equal, ChromeIconButton::Tone::armed, help::Key::autoBalance),
      balance_(services.backend, "outputBalance",
               secondaryKnob("Bal", scales::balanceDb(), 0.5f, help::Key::outputBalance,
                             Knob::Variant::bipolar)),
      output_(services.backend, "outputLevel",
              primaryKnob("Output", scales::gainDb(), 0.5f, help::Key::outputLevel)),
      spreadEnabled_(services.backend, "spreadEnabled") {
  setOpaque(true);

  addAndMakeVisible(input_);
  addChildComponent(*inputMode_);
  addAndMakeVisible(gate_);
  addAndMakeVisible(transpose_);

  // Powered-off section: knobs + labels dim and go inert; the power button
  // stays outside the dimmed wrapper, bright and clickable.
  for (auto* k : {&bass_, &middle_, &treble_}) toneDim_.addAndMakeVisible(*k);
  toneDim_.setOff(!tonePower_.value(), false);
  tonePower_.onValueChange = [this](bool on) { toneDim_.setOff(!on); };
  addAndMakeVisible(toneDim_);
  addAndMakeVisible(tonePower_);

  imageDim_.setHelpText(help::text(help::Key::spreadMonoOutput));
  imageDim_.addChildComponent(spread_);
  imageDim_.addChildComponent(align_);
  addAndMakeVisible(imageDim_);

  autoBalance_.onClick = [this] { services_.autoBalance.toggle(); };
  addChildComponent(autoBalance_);
  addChildComponent(balance_);
  addAndMakeVisible(output_);

  spreadEnabled_.onChange = [this] { syncFlags(); };
  services_.chain.addListener(this);
  services_.autoBalance.addListener(this);
  syncFlags();
}

Faceplate::~Faceplate() {
  services_.autoBalance.removeListener(this);
  services_.chain.removeListener(this);
}

void Faceplate::autoMeasureChanged() { autoBalance_.setOn(services_.autoBalance.listening()); }

void Faceplate::syncFlags() {
  const auto& chain = services_.chain.state();
  // The rig can drive two distinct output channels at all (stereo host bus /
  // 2+ channel output device). False dims the Spread group and makes it
  // inert, power button included: the feature is unavailable, not merely off.
  const bool stereoOutput = chain.stereoOutput;
  // Two independent chains are running: auto balance shows and the slot
  // swaps Spread for Align. Mono-mode spread doesn't need auto balance: both
  // channels carry the same chain, so their energy already matches.
  const bool stereoChains = chain.stereoEnabled;
  // The balance trim is audible (stereo chains on any rig, or spread on a
  // stereo rig; on a mono rig it trims the chains inside the mono sum).
  const bool balanceActive = stereoChains || (spreadEnabled_.boolValue() && stereoOutput);

  if (inputMode_->isVisible() != chain.stereoInput) {
    // The input group grows by the button, so the plate spreads again.
    inputMode_->setVisible(chain.stereoInput);
    resized();
  }
  inputMode_->setState(chain.inputMode, chain.branch.has_value());

  spread_.setVisible(!stereoChains);
  align_.setVisible(stereoChains);
  imageDim_.setOff(!stereoOutput && !stereoChains);

  autoBalance_.setVisible(stereoChains);
  autoBalance_.setOn(services_.autoBalance.listening());
  balance_.setVisible(balanceActive);
}

void Faceplate::paint(juce::Graphics& g) {
  g.fillAll(theme::kSurfaceRaised);
  paint::hairlineH(g, 0, static_cast<float>(getWidth()), 0, theme::kBorder);
}

void Faceplate::resized() {
  const auto content = getLocalBounds().reduced(kPadX, kPadY);
  const int baseline = content.getBottom();  // every label slot ends here
  const int knobBottom = baseline + Knob::kEditorOverflow;
  const auto knobY = [&](int size) { return knobBottom - Knob::heightFor(size); };
  const int chromeY = baseline - theme::kIconBoxSize + kChromeLift;
  const int primary = theme::kKnobSizePrimary, secondary = theme::kKnobSizeSecondary, box = theme::kIconBoxSize;

  // Six peers share the plate width (space-between). Their footprints are
  // fixed; only the input group grows when the input-mode button shows.
  const int inputW = primary + (inputMode_->isVisible() ? kGroupGap + inputMode_->getWidth() : 0);
  const int gateW = GateGroup::kWidth;
  const int transposeW = TransposeGroup::kWidth;
  const int toneW = 3 * primary + 2 * kToneGap + kGroupGap + box;
  const int imageW = StereoImageGroup::kWidth;
  const int outputW = box + kGroupGap + secondary + kGroupGap + primary;
  const float gap =
      (content.getWidth() - (inputW + gateW + transposeW + toneW + imageW + outputW)) / 5.0f;

  float x = static_cast<float>(content.getX());
  const auto at = design::snap;

  // Input [+ mode]
  input_.setTopLeftPosition(at(x), knobY(primary));
  inputMode_->setTopLeftPosition(at(x) + primary + kGroupGap, chromeY);
  x += inputW + gap;

  // Gate + power (+ deck)
  gate_.setTopLeftPosition(at(x), knobY(secondary));
  x += gateW + gap;

  // Transpose + power (+ deck)
  transpose_.setTopLeftPosition(at(x), knobY(secondary));
  x += transposeW + gap;

  // Bass / Middle / Treble + power
  toneDim_.setBounds(at(x), knobY(primary), 3 * primary + 2 * kToneGap, Knob::heightFor(primary));
  bass_.setTopLeftPosition(0, 0);
  middle_.setTopLeftPosition(primary + kToneGap, 0);
  treble_.setTopLeftPosition(2 * (primary + kToneGap), 0);
  tonePower_.setTopLeftPosition(at(x) + 3 * primary + 2 * kToneGap + kGroupGap, chromeY);
  x += toneW + gap;

  // Spread / Align slot
  imageDim_.setBounds(at(x), knobBottom - StereoImageGroup::height(), imageW, StereoImageGroup::height());
  spread_.setTopLeftPosition(0, 0);
  align_.setTopLeftPosition(0, 0);
  x += imageW + gap;

  // [=][Bal][Output]: the (=) sits on the outer edge, keeping Bal next to the
  // main knob.
  autoBalance_.setTopLeftPosition(at(x), chromeY);
  balance_.setTopLeftPosition(at(x) + box + kGroupGap, knobY(secondary));
  output_.setTopLeftPosition(at(x) + box + kGroupGap + secondary + kGroupGap, knobY(primary));
}

}  // namespace t3k::ui
