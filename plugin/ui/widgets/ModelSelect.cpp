#include "ModelSelect.h"

#include "LoadingDots.h"
#include "Popover.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/DragScroller.h"

namespace t3k::ui {

namespace {
constexpr int kPadX = 12;
constexpr int kGap = 10;
constexpr int kChevron = 20;
constexpr int kCountWidth = 70;
constexpr int kDividerMarginY = 8;
constexpr float kNameFontPx = 14;
constexpr float kCountFontPx = 13;
constexpr int kCountIcon = 14;
constexpr int kCountGap = 6;
constexpr float kTrackRadius = 8;
constexpr int kListGap = 4;
constexpr int kRowPadX = 16;
constexpr int kDotsRowHeight = 10 + LoadingDots::kDot + 2 * LoadingDots::kMargin + 10;
const juce::Colour kListBg{0xff39393d};
const juce::Colour kRowActive = juce::Colours::white.withAlpha(0.10f);
const juce::Colour kCountColour = juce::Colours::white.withAlpha(0.60f);
}  // namespace

// ‹ / › stepper: a 20px chevron, DISABLED_OPACITY + not-allowed at the ends.
class ModelSelect::StepButton : public Clickable {
public:
  StepButton(Icon icon, const juce::String& name) : Clickable(name), icon_(icon) {}

  void setEnabledLook(bool enabled) {
    setEnabled(enabled);
    setMouseCursor(enabled ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = juce::Rectangle<float>(kChevron, kChevron).withCentre(getLocalBounds().toFloat().getCentre());
    Icons::draw(g, icon_, box, theme::kWhite.withAlpha(isEnabled() ? 1.0f : theme::kDisabledOpacity));
  }

private:
  Icon icon_;
};

// The option list, anchored above the track and as wide as it.
class ModelSelect::Dropdown : public Popover {
public:
  explicit Dropdown(ModelSelect& owner) : owner_(owner) {
    viewport_.setViewedComponent(&content_, false);
    addAndMakeVisible(viewport_);
    dots_.setVisible(false);
    content_.addChildComponent(dots_);
  }

  // Rebuild rows for the current options and size to fit (max five rows).
  void rebuild() {
    rows_.clear();
    const int width = owner_.getWidth();
    int y = 0;
    for (size_t i = 0; i < owner_.options_.size(); ++i) {
      const bool last = i + 1 == owner_.options_.size();
      auto row = std::make_unique<Row>(owner_.options_[i], owner_.options_[i].id == owner_.value_, !last);
      row->onClick = [this, id = owner_.options_[i].id] { owner_.dismissAndSelect(id); };
      row->setBounds(0, y, width, kRowHeight + (last ? 0 : 1));
      content_.addAndMakeVisible(*row);
      rows_.push_back(std::move(row));
      y += kRowHeight + (last ? 0 : 1);
    }
    dots_.setVisible(owner_.loading_);
    if (owner_.loading_) {
      dots_.setBounds(0, y, width, kDotsRowHeight + 1);
      y += kDotsRowHeight + 1;
    }
    content_.setSize(width, y);
    const int maxHeight = kMaxVisibleRows * kRowHeight + (kMaxVisibleRows - 1);
    setSize(width, std::min(y, maxHeight));
    viewport_.setBounds(getLocalBounds());
    if (isOpen()) reposition();
    scrollToActive();
  }

  void paint(juce::Graphics& g) override {
    paint::fill(g, getLocalBounds().toFloat(), kTrackRadius, kListBg);
  }

  void resized() override { viewport_.setBounds(getLocalBounds()); }

private:
  class Row : public Clickable {
  public:
    Row(const Option& option, bool active, bool divider)
        : Clickable(option.name), name_(option.name), active_(active), divider_(divider) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    bool active() const { return active_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
      auto box = getLocalBounds();
      if (divider_) paint::hairlineH(g, 0, static_cast<float>(box.getWidth()), static_cast<float>(box.getBottom() - 1), theme::kBorder);
      auto row = divider_ ? box.withTrimmedBottom(1) : box;
      if (active_ || highlighted) {
        g.setColour(kRowActive);
        g.fillRect(row);
      }
      paint::text(g, name_, row.reduced(kRowPadX, 0), Fonts::sans(kNameFontPx), theme::kWhite);
    }

  private:
    juce::String name_;
    bool active_, divider_;
  };

  // Dots row: border-top hairline, dots centred under 10px pads.
  class DotsRow : public juce::Component {
  public:
    DotsRow() { addAndMakeVisible(dots_); }
    void paint(juce::Graphics& g) override {
      paint::hairlineH(g, 0, static_cast<float>(getWidth()), 0, theme::kBorder);
    }
    void resized() override {
      dots_.setBounds(juce::Rectangle<int>(LoadingDots::kWidth, LoadingDots::kDot + 2 * LoadingDots::kMargin)
                          .withCentre(getLocalBounds().withTrimmedTop(1).getCentre()));
    }

  private:
    LoadingDots dots_;
  };

  // scrollIntoView({block: 'nearest'}) on the active row.
  void scrollToActive() {
    for (auto& row : rows_)
      if (row->active()) viewport_.reveal(*row, 0);
  }

  ModelSelect& owner_;
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};  // the arrows walk the rows
  juce::Component content_;
  std::vector<std::unique_ptr<Row>> rows_;
  DotsRow dots_;
};

// ModelSelect
namespace {
// The name area between the steppers; a click toggles the list.
class Trigger : public juce::Component {
public:
  std::function<void()> onClick;
  void mouseUp(const juce::MouseEvent& e) override {
    // Local bounds, not Component::contains(): that walks up to the window
    // peer, which an offscreen (testbed) render doesn't have. A press that
    // panned the column around it is not a click.
    if (e.mods.isLeftButtonDown() && getLocalBounds().contains(e.getPosition()) &&
        !e.mouseWasDraggedSinceMouseDown() && onClick)
      onClick();
  }
};
}  // namespace

ModelSelect::ModelSelect()
    : prev_(std::make_unique<StepButton>(Icon::ChevronLeft, "Previous model")),
      next_(std::make_unique<StepButton>(Icon::ChevronRight, "Next model")),
      list_(std::make_unique<Dropdown>(*this)) {
  prev_->onClick = [this] { step(-1); };
  next_->onClick = [this] { step(+1); };
  addAndMakeVisible(*prev_);
  addAndMakeVisible(*next_);
  auto trigger = std::make_unique<Trigger>();
  trigger->setName("model select");
  trigger->onClick = [this] { toggleList(); };
  trigger->setMouseCursor(juce::MouseCursor::PointingHandCursor);
  trigger_ = std::move(trigger);
  addAndMakeVisible(*trigger_);
  setSize(100, kHeight);
  syncSteppers();
}

ModelSelect::~ModelSelect() = default;

int ModelSelect::currentIndex() const {
  for (size_t i = 0; i < options_.size(); ++i)
    if (options_[i].id == value_) return static_cast<int>(i);
  return -1;
}

void ModelSelect::setOptions(std::vector<Option> options) {
  options_ = std::move(options);
  syncSteppers();
  syncList();
  repaint();
}

void ModelSelect::setValue(const juce::String& id) {
  if (value_ == id) return;
  value_ = id;
  syncSteppers();
  syncList();
  repaint();
}

void ModelSelect::setTotalCount(int total) {
  total_ = total;
  repaint();
}

void ModelSelect::setLoading(bool loading) {
  if (loading_ == loading) return;
  loading_ = loading;
  syncList();
}

void ModelSelect::setDisabledLook(bool disabled) {
  setAlpha(disabled ? theme::kDisabledOpacity : 1.0f);
  setInterceptsMouseClicks(!disabled, !disabled);
  if (disabled) list_->close();
}

void ModelSelect::step(int delta) {
  const int i = currentIndex() + delta;
  if (i < 0 || i >= static_cast<int>(options_.size())) return;
  if (onChange) onChange(options_[static_cast<size_t>(i)].id);
}

void ModelSelect::toggleList() {
  if (list_->isOpen()) {
    list_->close();
    return;
  }
  if (onOpen) onOpen();
  list_->rebuild();
  list_->open(*this, Popover::Align::left, kListGap, 0, Popover::Placement::above);
}

void ModelSelect::dismissAndSelect(const juce::String& id) {
  list_->close();
  if (onChange) onChange(id);
}

void ModelSelect::syncSteppers() {
  const int i = currentIndex();
  prev_->setEnabledLook(i > 0);
  next_->setEnabledLook(i >= 0 && i < static_cast<int>(options_.size()) - 1);
}

void ModelSelect::syncList() {
  if (list_->isOpen()) list_->rebuild();
}

void ModelSelect::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, kTrackRadius, theme::kSegmentedTrack);

  // Right cluster: divider, then the 70px folder tally.
  auto right = getLocalBounds().reduced(kPadX, 0);
  const auto count = right.removeFromRight(kCountWidth);
  right.removeFromRight(kGap);
  const auto divider = right.removeFromRight(1).reduced(0, kDividerMarginY);
  g.setColour(theme::kBorder);
  g.fillRect(divider);

  const int i = currentIndex();
  const auto tally = juce::String(i + 1) + "/" + juce::String(total_);
  const auto font = Fonts::sans(kCountFontPx);
  const int textW = juce::roundToInt(Fonts::width(font, tally));
  const int total = kCountIcon + kCountGap + textW;
  int x = count.getCentreX() - total / 2;
  Icons::draw(g, Icon::FolderClosed,
              juce::Rectangle<float>(kCountIcon, kCountIcon).withCentre({x + kCountIcon / 2.0f, box.getCentreY()}),
              kCountColour);
  x += kCountIcon + kCountGap;
  paint::text(g, tally, juce::Rectangle<int>(x, 0, textW + 2, getHeight()), font, kCountColour);

  // Name, centred in the trigger area, ellipsised.
  const auto name = i >= 0 ? options_[static_cast<size_t>(i)].name : juce::String("Select models...");
  paint::text(g, name, trigger_->getBounds(), Fonts::sans(kNameFontPx), theme::kWhite, juce::Justification::centred);
}

void ModelSelect::resized() {
  auto area = getLocalBounds().reduced(kPadX, 0);
  area.removeFromRight(kCountWidth + kGap + 1 + kGap);
  prev_->setBounds(area.removeFromLeft(kChevron));
  area.removeFromLeft(kGap);
  next_->setBounds(area.removeFromRight(kChevron));
  area.removeFromRight(kGap);
  trigger_->setBounds(area);
  if (list_->isOpen()) list_->rebuild();
}

}  // namespace t3k::ui
