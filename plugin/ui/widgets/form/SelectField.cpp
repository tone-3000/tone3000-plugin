#include "SelectField.h"

#include <algorithm>

#include "FormStyle.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "widgets/Clickable.h"
#include "widgets/DragScroller.h"
#include "widgets/Popover.h"

namespace t3k::ui {

namespace {
int rowHeight(bool sublabel) {
  return 2 * form::kFieldPadY + Fonts::normalLineHeight(form::kFieldPx) +
         (sublabel ? SelectField::kSublabelGap + Fonts::normalLineHeight(SelectField::kSublabelPx) : 0);
}
}  // namespace

// The option list: as wide as the trigger, 4px below it, scrolling past
// 264px. No dividers between rows; only the active/hover fill and the
// container border delineate options.
class SelectField::Dropdown : public Popover {
public:
  explicit Dropdown(SelectField& owner) : owner_(owner) {
    viewport_.setViewedComponent(&content_, false);
    addAndMakeVisible(viewport_);
  }

  void rebuild() {
    rows_.clear();
    const int width = owner_.getWidth() - 2 * kBorder;
    int y = 0;
    for (const auto& option : owner_.options_) {
      auto row = std::make_unique<Row>(option, owner_.value_ == option.value);
      row->onClick = [this, value = option.value] { owner_.pick(value); };
      row->setBounds(0, y, width, rowHeight(option.sublabel.isNotEmpty()));
      content_.addAndMakeVisible(*row);
      y += row->getHeight();
      rows_.push_back(std::move(row));
    }
    content_.setSize(width, y);
    setSize(owner_.getWidth(), std::min(y, kListMaxHeight) + 2 * kBorder);
    viewport_.setBounds(contentBounds());
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, form::kFieldRadius, theme::kBlack);
    paint::border(g, box, form::kFieldRadius, form::kFieldBorder);
  }
  void resized() override { viewport_.setBounds(contentBounds()); }

private:
  class Row : public Clickable {
  public:
    Row(const Option& option, bool active) : Clickable(option.label), option_(option), active_(active) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
      if (active_ || highlighted) {
        g.setColour(form::kOptionFill);
        g.fillRect(getLocalBounds());
      }
      const auto font = Fonts::sans(form::kFieldPx);
      const float lineH = static_cast<float>(Fonts::normalLineHeight(form::kFieldPx));
      float top = form::kFieldPadY;
      g.setColour(theme::kWhite);
      g.setFont(font);
      g.drawSingleLineText(option_.label, form::kFieldPadX, juce::roundToInt(top + Fonts::cssBaseline(font, lineH)));
      if (option_.sublabel.isEmpty()) return;
      top += lineH + kSublabelGap;
      const auto small = Fonts::sans(kSublabelPx);
      const float smallH = static_cast<float>(Fonts::normalLineHeight(kSublabelPx));
      g.setColour(theme::kSubtle);
      g.setFont(small);
      g.drawSingleLineText(option_.sublabel, form::kFieldPadX,
                           juce::roundToInt(top + Fonts::cssBaseline(small, smallH)));
    }

  private:
    Option option_;
    bool active_;
  };

  SelectField& owner_;
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};  // the arrows walk the rows
  juce::Component content_;
  std::vector<std::unique_ptr<Row>> rows_;
};

class SelectField::AccessibleValue : public juce::AccessibilityTextValueInterface {
public:
  explicit AccessibleValue(SelectField& owner) : owner_(owner) {}

  bool isReadOnly() const override { return owner_.disabled_; }

  juce::String getCurrentValueAsString() const override {
    const auto* option = owner_.selected();
    return option != nullptr ? option->label : owner_.placeholder_;
  }

  void setValueAsString(const juce::String& value) override {
    if (owner_.disabled_) return;
    const auto option = std::find_if(owner_.options_.begin(), owner_.options_.end(), [&](const Option& candidate) {
      return candidate.label == value || candidate.value == value;
    });
    if (option != owner_.options_.end()) owner_.pick(option->value);
  }

private:
  SelectField& owner_;
};

namespace {

class SelectFieldAccessibilityHandler : public juce::AccessibilityHandler {
public:
  SelectFieldAccessibilityHandler(SelectField& owner, juce::AccessibilityActions actions, Interfaces interfaces)
      : juce::AccessibilityHandler(owner, juce::AccessibilityRole::comboBox, std::move(actions),
                                   std::move(interfaces)),
        owner_(owner) {}

  juce::String getTitle() const override { return owner_.getName(); }
  juce::String getHelp() const override { return owner_.getHelpText(); }

  juce::AccessibleState getCurrentState() const override {
    const auto state = juce::AccessibilityHandler::getCurrentState().withExpandable();
    return owner_.isOpen() ? state.withExpanded() : state.withCollapsed();
  }

private:
  SelectField& owner_;
};

}  // namespace

// SelectField
SelectField::SelectField(const juce::String& name) : dropdown_(std::make_unique<Dropdown>(*this)) {
  setName(name);
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  dropdown_->onDismiss = [this] { repaint(); };
}

SelectField::~SelectField() { dropdown_->close(); }

void SelectField::setOptions(std::vector<Option> options) {
  options_ = std::move(options);
  if (isOpen()) dropdown_->rebuild();
  repaint();
}

void SelectField::setValue(std::optional<juce::String> value) {
  value_ = std::move(value);
  if (isOpen()) dropdown_->rebuild();
  repaint();
}

void SelectField::setPlaceholder(const juce::String& text) {
  placeholder_ = text;
  repaint();
}

void SelectField::setDisabled(bool disabled) {
  disabled_ = disabled;
  if (disabled) close();
  setWantsKeyboardFocus(!disabled);
  setMouseCursor(disabled ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
  repaint();
}

bool SelectField::isOpen() const { return dropdown_->isOpen(); }

void SelectField::open() {
  if (disabled_ || isOpen()) return;
  dropdown_->rebuild();
  dropdown_->open(*this, Popover::Align::left, kListGap);
  repaint();
}

void SelectField::close() {
  dropdown_->close();
  repaint();
}

const SelectField::Option* SelectField::selected() const {
  if (!value_) return nullptr;
  for (const auto& o : options_)
    if (o.value == *value_) return &o;
  return nullptr;
}

void SelectField::pick(const juce::String& value) {
  dropdown_->close();
  repaint();
  if (onChange) onChange(value);
}

float SelectField::heightFor(float) const { return static_cast<float>(form::fieldHeight()); }

void SelectField::mouseUp(const juce::MouseEvent& e) {
  if (disabled_ || !getLocalBounds().contains(e.getPosition()) || e.mouseWasDraggedSinceMouseDown()) return;
  if (isOpen())
    close();
  else
    open();
}

bool SelectField::keyPressed(const juce::KeyPress& key) {
  if (disabled_) return false;
  if (key != juce::KeyPress::spaceKey && key != juce::KeyPress::returnKey && key != juce::KeyPress::downKey &&
      key != juce::KeyPress::upKey)
    return false;
  open();
  return isOpen();
}

std::unique_ptr<juce::AccessibilityHandler> SelectField::createAccessibilityHandler() {
  juce::AccessibilityActions actions;
  actions.addAction(juce::AccessibilityActionType::press, [this] {
    if (disabled_) return;
    grabKeyboardFocus();
    open();
  });
  juce::AccessibilityHandler::Interfaces interfaces;
  interfaces.value = std::make_unique<AccessibleValue>(*this);
  return std::make_unique<SelectFieldAccessibilityHandler>(*this, std::move(actions), std::move(interfaces));
}

void SelectField::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::border(g, box, form::kFieldRadius, form::kFieldBorder);
  const auto* option = selected();
  const bool muted = disabled_ || option == nullptr;
  const auto font = Fonts::sans(form::kFieldPx);
  const float lineH = static_cast<float>(Fonts::normalLineHeight(form::kFieldPx));
  const float textX = 1 + form::kFieldPadX;
  const float textRight = box.getWidth() - 1 - form::kFieldPadX - (disabled_ ? 0 : kChevron + 10);
  paint::cssLine(g, option != nullptr ? option->label : placeholder_, textX, (box.getHeight() - lineH) / 2, lineH,
                 textRight - textX, font, muted ? theme::kMuted : theme::kWhite);
  if (disabled_) return;
  const auto chevron = juce::Rectangle<float>(kChevron, kChevron)
                           .withCentre({box.getRight() - 1 - form::kFieldPadX - kChevron / 2.0f, box.getCentreY()});
  if (isOpen()) {
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi, chevron.getCentreX(), chevron.getCentreY()));
    Icons::draw(g, Icon::ChevronDown, chevron, theme::kMuted);
  } else {
    Icons::draw(g, Icon::ChevronDown, chevron, theme::kMuted);
  }
}

void SelectField::resized() {
  if (isOpen()) dropdown_->rebuild();
}

}  // namespace t3k::ui
