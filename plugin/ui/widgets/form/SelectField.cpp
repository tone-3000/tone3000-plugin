#include "SelectField.h"

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

// SelectField
SelectField::SelectField(const juce::String& name) : dropdown_(std::make_unique<Dropdown>(*this)) {
  setName(name);
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
