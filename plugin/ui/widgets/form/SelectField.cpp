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
    const bool restoreFocus = hasKeyboardFocus(true);
    std::optional<juce::String> focusedValue;
    for (const auto& row : rows_)
      if (row->hasKeyboardFocus(false)) focusedValue = row->optionValue();
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
    if (restoreFocus) focusOption(focusedValue);
  }

  void focusOption(std::optional<juce::String> preferred = std::nullopt) {
    if (!isShowing() || rows_.empty()) return;
    const auto wanted = preferred ? preferred : owner_.value_;
    for (const auto& row : rows_)
      if (wanted && row->optionValue() == *wanted) {
        row->grabKeyboardFocus();
        return;
      }
    rows_.front()->grabKeyboardFocus();
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
      setToggleable(true);
      setToggleState(active, juce::dontSendNotification);
      setDescription(option.sublabel);
    }
    const juce::String& optionValue() const { return option_.value; }

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

// SelectField
SelectField::SelectField(const juce::String& name) : dropdown_(std::make_unique<Dropdown>(*this)) {
  setName(name);
  setTitle(name);
  // A Tab stop for the keyboard; a click never focuses it (the host keeps
  // its keys), as before.
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  dropdown_->onDismiss = [this] { repaint(); };
}

SelectField::~SelectField() { dropdown_->close(); }

void SelectField::setOptions(std::vector<Option> options) {
  // Device polling must not destroy the focused option when nothing changed.
  if (options.size() == options_.size() && std::equal(options.begin(), options.end(), options_.begin(),
      [](const Option& a, const Option& b) {
        return a.value == b.value && a.label == b.label && a.sublabel == b.sublabel;
      })) return;
  const auto oldLabel = selectedLabel();
  options_ = std::move(options);
  if (isOpen()) dropdown_->rebuild();
  repaint();
  if (oldLabel != selectedLabel())
    if (auto* handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void SelectField::setValue(std::optional<juce::String> value) {
  if (value_ == value) return;
  value_ = std::move(value);
  if (isOpen()) dropdown_->rebuild();
  repaint();
  if (auto* handler = getAccessibilityHandler())
    handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void SelectField::setPlaceholder(const juce::String& text) {
  placeholder_ = text;
  repaint();
}

void SelectField::setDisabled(bool disabled) {
  if (disabled_ == disabled) return;
  disabled_ = disabled;
  if (disabled) close();
  setWantsKeyboardFocus(!disabled);
  setMouseCursor(disabled ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
  repaint();
}

bool SelectField::isOpen() const { return dropdown_->isOpen(); }

void SelectField::open() {
  if (disabled_ || !isEnabled() || options_.empty() || isOpen()) return;
  // Opened from the keyboard: focus lands on the chosen row and returns
  // here on close. A click leaves focus alone, so nothing stays focused.
  const bool keyboard = hasKeyboardFocus(false);
  dropdown_->rebuild();
  dropdown_->open(*this, Popover::Align::left, kListGap);
  if (keyboard) dropdown_->focusOption();
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

juce::String SelectField::selectedLabel() const {
  if (const auto* option = selected()) return option->label;
  return placeholder_;
}

bool SelectField::keyPressed(const juce::KeyPress& key) {
  if (disabled_ || !isEnabled()) return false;
  using KP = juce::KeyPress;
  if (key.isKeyCode(KP::upKey) || key.isKeyCode(KP::downKey))
    if (auto* menu = findParentComponentOfClass<Popover>()) return menu->keyPressed(key);
  if (key == KP::returnKey || key == KP::spaceKey || key == KP::downKey || key == KP::upKey
      || key == KP::F4Key || (key.isKeyCode(KP::downKey) && key.getModifiers().isAltDown())) {
    open();
    return true;
  }
  return false;
}

namespace {
class SelectValue : public juce::AccessibilityTextValueInterface {
public:
  explicit SelectValue(SelectField& field) : field_(field) {}
  bool isReadOnly() const override { return true; }
  juce::String getCurrentValueAsString() const override { return field_.selectedLabel(); }
  void setValueAsString(const juce::String&) override {}
private:
  SelectField& field_;
};

class SelectHandler : public juce::AccessibilityHandler {
public:
  explicit SelectHandler(SelectField& field)
      : juce::AccessibilityHandler(field, juce::AccessibilityRole::comboBox,
            juce::AccessibilityActions()
                .addAction(juce::AccessibilityActionType::press, [&field] { pressed(field); })
                .addAction(juce::AccessibilityActionType::showMenu, [&field] { pressed(field); }),
            {std::make_unique<SelectValue>(field)}), field_(field) {}
  juce::AccessibleState getCurrentState() const override {
    auto state = juce::AccessibilityHandler::getCurrentState().withExpandable();
    return field_.isOpen() ? state.withExpanded() : state.withCollapsed();
  }
private:
  // A screen reader's press is a keyboard open: focus the field first.
  static void pressed(SelectField& field) {
    if (field.isShowing() && field.getWantsKeyboardFocus()) field.grabKeyboardFocus();
    field.open();
  }
  SelectField& field_;
};
}  // namespace

std::unique_ptr<juce::AccessibilityHandler> SelectField::createAccessibilityHandler() {
  return std::make_unique<SelectHandler>(*this);
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
