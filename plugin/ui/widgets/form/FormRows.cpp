#include "FormRows.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Paint.h"
#include "widgets/AccessibleGroup.h"

namespace t3k::ui {

// FieldRow
FieldRow::FieldRow(const juce::String& label, const juce::String& help) : label_(label), help_(help) {
  addAndMakeVisible(label_);
  addAndMakeVisible(help_);
  addAndMakeVisible(content_);
  help_.setVisible(help.isNotEmpty());
}

void FieldRow::setHelp(const juce::String& help) {
  help_.setText(help);
  help_.setVisible(help.isNotEmpty());
  heightChanged();
}

void FieldRow::setLabelExtra(juce::Component* extra) {
  if (extra_ != nullptr) removeChildComponent(extra_);
  extra_ = extra;
  if (extra_ != nullptr) addAndMakeVisible(*extra_);
  heightChanged();
}

float FieldRow::headerHeight() const {
  return juce::jmax(label_.heightFor(0), extra_ != nullptr ? static_cast<float>(extra_->getHeight()) : 0.0f);
}

float FieldRow::heightFor(float width) const {
  float h = headerHeight();
  h += help_.isVisible() ? kHelpGapTop + help_.heightFor(width) + form::kControlGap : form::kControlGap;
  return h + content_.heightFor(width);
}

void FieldRow::resized() {
  const auto width = static_cast<float>(getWidth());
  const float headerH = headerHeight();
  placeChild(label_, {0, (headerH - label_.heightFor(0)) / 2, label_.preferredWidth() + 2, label_.heightFor(0)});
  if (extra_ != nullptr)
    extra_->setTopLeftPosition(getWidth() - extra_->getWidth(),
                               static_cast<int>(std::round(subpixelTop() + (headerH - extra_->getHeight()) / 2)));
  float y = headerH;
  if (help_.isVisible()) {
    const float helpH = help_.heightFor(width);
    placeChild(help_, {0, y + kHelpGapTop, width, helpH});
    y += kHelpGapTop + helpH;
  }
  y += form::kControlGap;
  placeChild(content_, {0, y, width, content_.heightFor(width)});
}

// ToggleRow
ToggleRow::ToggleRow(const juce::String& label, RichText description)
    : label_(label), description_(description) {
  toggle_.setName(label);
  // The description under the switch is what it does; screen readers read
  // it after the switch's name and state.
  juce::String spoken;
  for (const auto& run : description) spoken << run.text;
  toggle_.setDescription(spoken.replace("  ", " ").trim());
  toggle_.onChange = [this](bool on) {
    if (onChange) onChange(on);
  };
  addAndMakeVisible(label_);
  addAndMakeVisible(toggle_);
  addAndMakeVisible(description_);
  addChildComponent(content_);
}

void ToggleRow::setNested() {
  indent_ = kNestedIndent;
  label_.setStyle(form::kBodyPx, false, theme::kWhite);
  heightChanged();
}

void ToggleRow::setExpanded(bool expanded) {
  if (expanded == content_.isVisible()) return;
  content_.setVisible(expanded);
  heightChanged();
}

float ToggleRow::heightFor(float width) const {
  const float inner = width - indent_;
  float h = PillToggle::kHeight + kDescriptionGap + description_.heightFor(inner);
  if (content_.isVisible()) h += form::kControlGap + content_.heightFor(inner);
  return h;
}

void ToggleRow::resized() {
  // The indent moves the text column in; the toggle keeps the section's
  // right edge.
  const auto x = static_cast<float>(indent_);
  const auto width = static_cast<float>(getWidth() - indent_);
  const float rowH = PillToggle::kHeight;
  placeChild(label_, {x, (rowH - label_.heightFor(0)) / 2, label_.preferredWidth() + 2, label_.heightFor(0)});
  toggle_.setTopLeftPosition(getWidth() - PillToggle::kWidth, 0);
  float y = rowH + kDescriptionGap;
  const float descH = description_.heightFor(width);
  placeChild(description_, {x, y, width, descH});
  y += descH;
  if (content_.isVisible()) {
    y += form::kControlGap;
    placeChild(content_, {x, y, width, content_.heightFor(width)});
  }
}

// RadioOption
namespace {

// A radio button: checked while selected, pressed to select.
class RadioOptionHandler : public juce::AccessibilityHandler {
public:
  explicit RadioOptionHandler(RadioOption& option)
      : juce::AccessibilityHandler(option, juce::AccessibilityRole::radioButton, actions(option)), option_(option) {}

  juce::AccessibleState getCurrentState() const override {
    auto state = juce::AccessibilityHandler::getCurrentState().withCheckable();
    return option_.selected() ? state.withChecked() : state;
  }

private:
  static juce::AccessibilityActions actions(RadioOption& option) {
    juce::AccessibilityActions result;
    result.addAction(juce::AccessibilityActionType::press, [&option] { option.choose(); });
    return result;
  }

  RadioOption& option_;
};

}  // namespace

RadioOption::RadioOption(const juce::String& label, const juce::String& description)
    : label_(label), description_(description) {
  setName(label);
  setTitle(label);
  setDescription(description);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  // Keyboard and screen readers only: a click does not move focus, so the
  // mouse behaves as before.
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
  description_.setInterceptsMouseClicks(false, false);
  // Spoken as the option's description; not a second item.
  description_.setAccessible(false);
  addAndMakeVisible(description_);
}

void RadioOption::setSelected(bool selected) {
  const bool changed = selected_ != selected;
  selected_ = selected;
  repaint();
  if (changed)
    if (auto* handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void RadioOption::choose() {
  if (isShowing()) grabKeyboardFocus();
  if (onSelect) onSelect();
}

void RadioOption::chooseSibling(int step) {
  auto* parent = getParentComponent();
  if (parent == nullptr) return;
  std::vector<RadioOption*> options;
  for (auto* child : parent->getChildren())
    if (auto* option = dynamic_cast<RadioOption*>(child); option != nullptr && option->isShowing())
      options.push_back(option);
  const auto it = std::find(options.begin(), options.end(), this);
  if (it == options.end() || options.size() < 2) return;
  const int count = static_cast<int>(options.size());
  const int index = static_cast<int>(it - options.begin());
  options[static_cast<size_t>(((index + step) % count + count) % count)]->choose();
}

bool RadioOption::keyPressed(const juce::KeyPress& key) {
  if (key.getModifiers().isAnyModifierKeyDown()) return false;
  const int code = key.getKeyCode();
  if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) {
    choose();
    return true;
  }
  if (code == juce::KeyPress::upKey || code == juce::KeyPress::leftKey) {
    chooseSibling(-1);
    return true;
  }
  if (code == juce::KeyPress::downKey || code == juce::KeyPress::rightKey) {
    chooseSibling(1);
    return true;
  }
  return false;
}

std::unique_ptr<juce::AccessibilityHandler> RadioOption::createAccessibilityHandler() {
  return std::make_unique<RadioOptionHandler>(*this);
}

float RadioOption::heightFor(float width) const {
  const float labelH = form::kBodyPx * kLabelLineHeight;
  const float textH = labelH + kDescriptionGap + description_.heightFor(width - textX());
  return juce::jmax(static_cast<float>(ChoiceIndicator::kSize + 1), textH);
}

void RadioOption::resized() {
  const float labelH = form::kBodyPx * kLabelLineHeight;
  const float x = textX();
  placeChild(description_, {x, labelH + kDescriptionGap, getWidth() - x, description_.heightFor(getWidth() - x)});
}

void RadioOption::paint(juce::Graphics& g) {
  ChoiceIndicator::paint(g, juce::Rectangle<float>(0, 1, ChoiceIndicator::kSize, ChoiceIndicator::kSize), selected_,
                         false);
  const auto font = Fonts::sans(form::kBodyPx);
  paint::cssLine(g, label_, textX(), subpixelTop(), form::kBodyPx * kLabelLineHeight, getWidth() - textX(), font,
                 theme::kWhite);
}

void RadioOption::mouseUp(const juce::MouseEvent& e) {
  if (!getLocalBounds().contains(e.getPosition()) || e.mouseWasDraggedSinceMouseDown()) return;
  if (onSelect) onSelect();
}

// SettingsGroup
SettingsGroup::SettingsGroup(const juce::String& title, const char* iconSvg)
    : title_(title.toUpperCase()), iconSvg_(iconSvg) {
  a11y::makeGroup(*this, title);
  addAndMakeVisible(content_);
}

float SettingsGroup::headerHeight() {
  return juce::jmax(static_cast<float>(kIcon), static_cast<float>(Fonts::normalLineHeight(kTitlePx)));
}

float SettingsGroup::heightFor(float width) const {
  const float headerH = headerHeight();
  return 2 * (1 + kPad) + headerH + kTitleGap + content_.heightFor(width - 2 * (1 + kPad));
}

void SettingsGroup::resized() {
  const float headerH = headerHeight();
  const float inner = static_cast<float>(getWidth() - 2 * (1 + kPad));
  placeChild(content_, {1 + kPad, 1 + kPad + headerH + kTitleGap, inner, content_.heightFor(inner)});
}

void SettingsGroup::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::border(g, box, form::kCardRadius, form::kFieldBorder);
  const float top = 1 + kPad;
  const float headerH = headerHeight();
  Icons::draw(g, iconSvg_, juce::Rectangle<float>(1 + kPad, top + (headerH - kIcon) / 2, kIcon, kIcon), theme::kWhite);
  const auto font = Fonts::sans(kTitlePx, true);
  const float lineH = static_cast<float>(Fonts::normalLineHeight(kTitlePx));
  paint::cssLine(g, title_, 1 + kPad + kIcon + kIconGap, subpixelTop() + top + (headerH - lineH) / 2, lineH,
                 box.getWidth() - 2 * (1 + kPad) - kIcon - kIconGap, font, theme::kWhite);
}

// TipRow
TipRow::TipRow(RichText text) : text_(std::move(text), form::kBodyPx, theme::kWhite) { addAndMakeVisible(text_); }

float TipRow::heightFor(float width) const {
  return juce::jmax(static_cast<float>(kIcon), text_.heightFor(width - kIcon - kGap));
}

void TipRow::resized() {
  const float w = static_cast<float>(getWidth() - kIcon - kGap);
  const float h = text_.heightFor(w);
  placeChild(text_, {kIcon + kGap, (heightFor(static_cast<float>(getWidth())) - h) / 2, w, h});
}

void TipRow::paint(juce::Graphics& g) {
  Icons::draw(g, Icon::Info, juce::Rectangle<float>(0, (getHeight() - kIcon) / 2.0f, kIcon, kIcon), theme::kWhite);
}

}  // namespace t3k::ui
