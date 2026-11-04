#include "PresetBar.h"

#include <algorithm>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/preset/PresetBrowsePanel.h"
#include "widgets/Clickable.h"
#include "widgets/Popover.h"
#include "widgets/TextField.h"

namespace t3k::ui {

namespace {

// The bar uses GRAY for its muted text (PresetBar.tsx `MUTED = GRAY`).
const juce::Colour kMutedText = theme::kGray;
constexpr int kPanelGap = 10;    // top: calc(100% + 10)
constexpr int kPanelInset = -8;  // left: -8

}  // namespace

// Pill buttons
class PresetBar::Chevron : public Clickable {
public:
  Chevron(Icon icon, help::Key key) : Clickable({}), icon_(icon) {
    setHelpText(help::text(key));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void setLit(bool lit) { lit_ = lit; repaint(); }
  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = juce::Rectangle<float>(14, 14).withCentre(getLocalBounds().toFloat().getCentre());
    Icons::draw(g, icon_, box, lit_ ? theme::kWhite : kMutedText);
  }

private:
  Icon icon_;
  bool lit_ = true;
};

class PresetBar::NameButton : public Clickable {
public:
  NameButton() : Clickable({}) {
    setHelpText(help::text(help::Key::presetBrowse));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void set(const juce::String& text, bool active) {
    text_ = text;
    active_ = active;
    repaint();
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    paint::text(g, text_, getLocalBounds().reduced(6, 0), Fonts::sans(14),
                active_ ? theme::kWhite : kMutedText, juce::Justification::centred);
  }

private:
  juce::String text_;
  bool active_ = false;
};

// Save popover
class PresetBar::SavePanel : public Popover {
public:
  static constexpr int kWidth = 280;
  static constexpr int kPad = 16;
  static constexpr int kTitleHeight = 16;
  static constexpr int kInputHeight = 35;  // 13px text + 9px padding + 1px border
  static constexpr int kButtonHeight = 35;

  explicit SavePanel(PresetBar& owner) : owner_(owner) {
    setSize(kWidth, kBorder * 2 + kPad + kTitleHeight + 12 + kInputHeight + 12 + kButtonHeight + kPad);
    name_.setPlaceholder("Name");
    name_.onChange = [this](const juce::String&) { repaint(); };
    name_.onEnter = [this] { save(); };
    addAndMakeVisible(name_);
    saveButton_.onClick = [this] { save(); };
    saveButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(saveButton_);
  }

  void show(const juce::String& prefill) {
    name_.setText(prefill);
    open(owner_, Align::left, kPanelGap, kPanelInset);
    name_.focus();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
    paint::text(g, "Save Preset", contentBounds().reduced(kPad).removeFromTop(kTitleHeight),
                Fonts::sans(14, true), theme::kWhite);
    // Outline pill: border rgba(235,235,245,0.6), label white / muted.
    const bool enabled = name_.text().trim().isNotEmpty();
    const auto b = saveButton_.getBounds().toFloat();
    paint::border(g, b, b.getHeight() / 2, juce::Colour(235, 235, 245).withAlpha(0.6f));
    paint::text(g, "Save", saveButton_.getBounds(), Fonts::sans(13),
                enabled ? theme::kWhite : kMutedText, juce::Justification::centred);
  }

  void resized() override {
    auto area = contentBounds().reduced(kPad);
    area.removeFromTop(kTitleHeight + 12);
    name_.setBounds(area.removeFromTop(kInputHeight));
    area.removeFromTop(12);
    saveButton_.setBounds(area.removeFromTop(kButtonHeight));
  }

private:
  // Invisible hit target; the panel paints the pill so the label colour can
  // follow the field without a second component.
  class Hit : public Clickable {
  public:
    Hit() : Clickable("Save") {}
    void paintButton(juce::Graphics&, bool, bool) override {}
  };

  void save() {
    const auto name = name_.text().trim();
    if (name.isEmpty()) return;
    close();
    if (owner_.beforeSave) owner_.beforeSave();
    if (owner_.services_.presets.save(name)) owner_.services_.toast.show("Preset Saved");
  }

  PresetBar& owner_;
  TextField name_;
  Hit saveButton_;
};

// Bar
PresetBar::PresetBar(Services& services)
    : services_(services),
      prev_(std::make_unique<Chevron>(Icon::ChevronLeft, help::Key::presetPrev)),
      next_(std::make_unique<Chevron>(Icon::ChevronRight, help::Key::presetNext)),
      name_(std::make_unique<NameButton>()),
      savePanel_(std::make_unique<SavePanel>(*this)),
      browsePanel_(std::make_unique<PresetBrowsePanel>(
          services, *this, [this] { return active() ? active()->id : juce::String(); },
          [this](const juce::String& id) { loadAndClose(id); })) {
  prev_->onClick = [this] { step(-1); };
  next_->onClick = [this] { step(1); };
  name_->onClick = [this] { openBrowsePanel(); };
  addAndMakeVisible(*prev_);
  addAndMakeVisible(*name_);
  addAndMakeVisible(*next_);

  save_.setCornerRadius(4);
  save_.setHelpText(help::text(help::Key::presetSave));
  save_.onClick = [this] { openSavePanel(); };
  addAndMakeVisible(save_);

  newButton_.setHelpText(help::text(help::Key::presetNew));
  newButton_.onClick = [this] {
    closePanels();
    if (onReset) onReset();
  };
  addAndMakeVisible(newButton_);

  services_.presets.addListener(this);
  services_.chain.addListener(this);
  services_.prefs.addListener(this);
  setSize(kWidth, kHeight);
  refreshChrome();
}

PresetBar::~PresetBar() {
  services_.prefs.removeListener(this);
  services_.chain.removeListener(this);
  services_.presets.removeListener(this);
  closePanels();
}

void PresetBar::presetsChanged(const std::vector<PresetInfo>&) {
  refreshChrome();
  browsePanel_->presetsChanged();
}

void PresetBar::chainChanged(const ChainState&) {
  refreshChrome();
  browsePanel_->presetsChanged();
}

void PresetBar::prefChanged(const juce::String& key) {
  if (key == UiPrefs::kShowPresetPcNumbers) browsePanel_->presetsChanged();
}

void PresetBar::refreshChrome() {
  const bool any = !presets().empty();
  prev_->setLit(any);
  next_->setLit(any);
  name_->set(active() ? active()->name : juce::String("Presets"), active().has_value());
  newButton_.setEnabled(!services_.chain.state().atDefault);
}

void PresetBar::step(int direction) {
  const auto& list = presets();
  if (list.empty()) return;
  int index = -1;
  if (active())
    for (size_t i = 0; i < list.size(); ++i)
      if (list[i].id == active()->id) index = static_cast<int>(i);
  const int n = static_cast<int>(list.size());
  // Wrap at the ends; with no active preset, › starts at the first, ‹ at the last.
  const int next = index < 0 ? (direction > 0 ? 0 : n - 1) : (index + direction + n) % n;
  loadAndClose(list[static_cast<size_t>(next)].id);
}

void PresetBar::loadAndClose(const juce::String& id) {
  closePanels();
  if (beforeLoad) beforeLoad();
  services_.presets.load(id);
}

void PresetBar::openSavePanel() {
  if (savePanel_->isOpen()) {
    savePanel_->close();
    return;
  }
  browsePanel_->close();
  // Prefill with the active user preset's name: saving it again is the
  // one-click "update" path (same name overwrites in place).
  juce::String prefill;
  if (active())
    for (const auto& p : presets())
      if (p.id == active()->id && !p.factory) prefill = p.name;
  savePanel_->show(prefill);
}

void PresetBar::openBrowsePanel() {
  if (browsePanel_->isOpen()) {
    browsePanel_->close();
    return;
  }
  savePanel_->close();
  browsePanel_->show();
}

void PresetBar::closePanels() {
  savePanel_->close();
  browsePanel_->close();
}

void PresetBar::paint(juce::Graphics& g) {
  // The ‹ name › pill: SEGMENTED_TRACK fill, radius 8.
  paint::fill(g, juce::Rectangle<float>(0, 0, kPillWidth, kHeight), 8.0f, theme::kSegmentedTrack);
}

void PresetBar::resized() {
  auto pill = getLocalBounds().removeFromLeft(kPillWidth).reduced(4, 0);
  prev_->setBounds(pill.removeFromLeft(22));
  next_->setBounds(pill.removeFromRight(22));
  name_->setBounds(pill);
  save_.setBounds(kPillWidth + 8, (kHeight - 28) / 2, 28, 28);
  newButton_.setBounds(kPillWidth + 8 + 28 + 8, (kHeight - 28) / 2, 28, 28);
}

}  // namespace t3k::ui
