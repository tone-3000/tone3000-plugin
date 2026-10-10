#include "views/preset/PresetRowViews.h"

#include "core/CustomIcons.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui::presetlist {

namespace {

const juce::Colour kMutedText = theme::kGray;
const juce::Colour kDropWash = juce::Colours::white.withAlpha(0.16f);
const juce::Colour kDropDash = juce::Colours::white.withAlpha(0.45f);
const juce::Colour kStarOff = juce::Colours::white.withAlpha(0.25f);
constexpr float kItemRadius = 6.0f;

}  // namespace

// GlyphButton

GlyphButton::GlyphButton(Icon icon, int glyph, int pad, help::Key key)
    : Clickable({}), icon_(icon), glyph_(glyph), colour_(theme::kWhite) {
  setSize(glyph + pad * 2, glyph + pad * 2);
  setHelp(key);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void GlyphButton::setHelp(help::Key key) { setHelpText(help::text(key)); }

void GlyphButton::paintButton(juce::Graphics& g, bool, bool) {
  if (filled_) paint::fill(g, getLocalBounds().toFloat(), 4.0f, juce::Colours::white.withAlpha(0.12f));
  const auto box = juce::Rectangle<float>(static_cast<float>(glyph_), static_cast<float>(glyph_))
                       .withCentre(getLocalBounds().toFloat().getCentre());
  if (custom_ != nullptr) Icons::draw(g, custom_, box, colour_);
  else Icons::draw(g, icon_, box, colour_);
}

// Item / TextLine

void Item::paintDropHighlight(juce::Graphics& g) const {
  if (!highlighted_) return;
  const auto box = getLocalBounds().toFloat().reduced(0.5f);
  paint::fill(g, box, kItemRadius, kDropWash);
  paint::dashedBorder(g, box, kItemRadius, kDropDash, 1.0f);
}

TextLine::TextLine(juce::String text, juce::Font font, juce::Colour colour, juce::BorderSize<int> pad)
    : text_(std::move(text)), font_(std::move(font)), colour_(colour), pad_(pad) {
  setInterceptsMouseClicks(false, false);
}

void TextLine::paint(juce::Graphics& g) { paint::text(g, text_, pad_.subtractedFrom(getLocalBounds()), font_, colour_); }

// GroupHeader

GroupHeader::GroupHeader(juce::String title, int count, bool collapsed, Glyph glyph, bool deletable)
    : title_(std::move(title)), count_(count), collapsed_(collapsed), glyph_(glyph) {
  setSize(0, kHeaderHeight);
  setHelpText(help::text(help::Key::categoryToggle));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  if (deletable) {
    delete_ = std::make_unique<GlyphButton>(Icon::Trash2, 12, 4, help::Key::categoryDelete);
    delete_->setColour(kMutedText);
    delete_->onClick = [this] {
      if (onDelete) onDelete();
    };
    addAndMakeVisible(*delete_);
  }
}

void GroupHeader::paint(juce::Graphics& g) {
  paintDropHighlight(g);
  auto area = getLocalBounds().reduced(4, 0);
  const auto chevron = area.removeFromLeft(13);
  Icons::draw(g, collapsed_ ? Icon::ChevronRight : Icon::ChevronDown,
              juce::Rectangle<float>(13, 13).withCentre(chevron.toFloat().getCentre()), kMutedText);
  area.removeFromLeft(6);
  if (glyph_ != Glyph::none) {
    const auto glyph = area.removeFromLeft(13);
    const auto box = juce::Rectangle<float>(13, 13).withCentre(glyph.toFloat().getCentre());
    if (glyph_ == Glyph::star) Icons::draw(g, custom_icons::kStarFilled, box, theme::kBrandYellow);
    else Icons::draw(g, Icon::Folder, box, kMutedText);
    area.removeFromLeft(6);
  }
  if (delete_ != nullptr) area.removeFromRight(delete_->getWidth() + 4);

  const auto titleFont = Fonts::sans(13, true);
  const int titleW = std::min(area.getWidth(), juce::roundToInt(Fonts::width(titleFont, title_)) + 2);
  paint::text(g, title_, area.removeFromLeft(titleW), titleFont, theme::kWhite);
  area.removeFromLeft(6);
  paint::text(g, "(" + juce::String(count_) + ")", area, Fonts::sans(11), kMutedText);
}

void GroupHeader::resized() {
  if (delete_ != nullptr)
    delete_->setBounds(getLocalBounds().removeFromRight(delete_->getWidth() + 4).withSizeKeepingCentre(
        delete_->getWidth(), delete_->getHeight()));
}

void GroupHeader::mouseUp(const juce::MouseEvent& e) {
  if (e.mouseWasClicked() && onToggle) onToggle();
}

// EmptyZone

void EmptyZone::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
  const bool hot = dropHighlight();
  paint::fill(g, box, kItemRadius, hot ? juce::Colours::white.withAlpha(0.14f) : juce::Colours::white.withAlpha(0.02f));
  paint::dashedBorder(g, box, kItemRadius, juce::Colours::white.withAlpha(hot ? 0.5f : 0.08f), 1.0f);
  paint::text(g, label_, getLocalBounds().reduced(24, 0), Fonts::sans(11, false, true),
              hot ? theme::kWhite : kMutedText);
}

// PresetRow

class PresetRow::NameButton : public Clickable {
public:
  NameButton(const juce::String& text, bool lit) : Clickable(text), text_(text), lit_(lit) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    paint::text(g, text_, getLocalBounds(), Fonts::sans(14), lit_ ? theme::kWhite : kMutedText);
  }

private:
  juce::String text_;
  bool lit_;
};

PresetRow::PresetRow(RowHost& host, State state) : host_(host), state_(std::move(state)) {
  const auto& preset = state_.preset;

  name_ = std::make_unique<NameButton>(preset.name, state_.active);
  name_->onClick = [this] {
    if (state_.selectable) host_.rowToggleSelected(state_.preset.id);
    else host_.rowLoad(state_.preset.id);
  };
  addAndMakeVisible(*name_);

  if (state_.selectable) {
    select_ = std::make_unique<GlyphButton>(Icon::Square, 14, 3,
                                            state_.selected ? help::Key::presetDeselect : help::Key::presetSelect);
    select_->setIcon(state_.selected ? Icon::CheckSquare : Icon::Square);
    select_->setColour(state_.selected ? theme::kWhite : kMutedText);
    select_->onClick = [this] { host_.rowToggleSelected(state_.preset.id); };
    addAndMakeVisible(*select_);
  }

  star_ = std::make_unique<GlyphButton>(Icon::Star, 13, 4,
                                        preset.favorite ? help::Key::presetUnfavorite : help::Key::presetFavorite);
  if (preset.favorite) star_->setCustomIcon(custom_icons::kStarFilled);
  star_->setColour(preset.favorite ? theme::kBrandYellow : kStarOff);
  star_->onClick = [this] { host_.rowToggleStar(state_.preset); };
  addAndMakeVisible(*star_);

  if (state_.renaming) {
    rename_ = std::make_unique<TextField>();
    rename_->setPadding(4, 8, 8);
    rename_->setCornerRadius(6);
    rename_->setText(preset.name);
    rename_->onEnter = [this] { commitRename(); };
    rename_->onBlur = [this] { commitRename(); };
    rename_->onEscape = [this] { host_.rowCancelRename(); };
    addAndMakeVisible(*rename_);
    name_->setVisible(false);
  }

  if (state_.draggable && !state_.renaming) {
    grip_ = std::make_unique<GlyphButton>(Icon::GripVertical, 14, 3, help::Key::presetDrag);
    grip_->setColour(kMutedText);
    grip_->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    grip_->setViewportIgnoreDragFlag(true);  // a drag on the grip moves the row; the list does not pan
    grip_->addMouseListener(this, false);
    addAndMakeVisible(*grip_);
  }
  if (!preset.factory && !state_.renaming && !state_.selectable) {
    pencil_ = std::make_unique<GlyphButton>(Icon::Pencil, 13, 3, help::Key::presetRename);
    pencil_->setColour(kMutedText);
    pencil_->onClick = [this] { host_.rowBeginRename(state_.preset.id); };
    addAndMakeVisible(*pencil_);
    trash_ = std::make_unique<GlyphButton>(Icon::Trash2, 13, 3, help::Key::presetDelete);
    trash_->setColour(kMutedText);
    trash_->onClick = [this] { host_.rowDelete(state_.preset.id); };
    addAndMakeVisible(*trash_);
  }
  setSize(0, kRowHeight);
}

PresetRow::~PresetRow() = default;

void PresetRow::focusRename() {
  if (rename_ != nullptr) rename_->focus();
}

void PresetRow::commitRename() {
  if (rename_ == nullptr) return;
  host_.rowCommitRename(state_.preset, rename_->text().trim());
}

void PresetRow::paint(juce::Graphics& g) {
  paintDropHighlight(g);
  if (state_.selected && !dropHighlight())
    paint::fill(g, getLocalBounds().toFloat(), kItemRadius, juce::Colours::white.withAlpha(0.08f));
  if (state_.active && !state_.selectable)
    Icons::draw(g, Icon::Check, juce::Rectangle<float>(14, 14).withCentre(activeSlot_.toFloat().getCentre()),
                theme::kWhite);
  if (state_.pc)
    paint::text(g, "PC " + juce::String(*state_.pc), pcArea_, Fonts::mono(11), kMutedText);
}

void PresetRow::resized() {
  auto area = getLocalBounds().reduced(4, 0);
  activeSlot_ = area.removeFromLeft(20);
  if (select_ != nullptr)
    select_->setBounds(activeSlot_.withSizeKeepingCentre(select_->getWidth(), select_->getHeight()));
  area.removeFromLeft(2);
  star_->setBounds(area.removeFromLeft(star_->getWidth()).withSizeKeepingCentre(star_->getWidth(), star_->getHeight()));
  area.removeFromLeft(6);

  auto takeRight = [&](GlyphButton* b) {
    if (b == nullptr) return;
    b->setBounds(area.removeFromRight(b->getWidth()).withSizeKeepingCentre(b->getWidth(), b->getHeight()));
    area.removeFromRight(2);
  };
  takeRight(grip_.get());
  takeRight(trash_.get());
  takeRight(pencil_.get());
  if (state_.pc) {
    const int w = juce::roundToInt(Fonts::width(Fonts::mono(11), "PC " + juce::String(*state_.pc)));
    pcArea_ = area.removeFromRight(w);
    area.removeFromRight(6);
  }
  if (rename_ != nullptr) rename_->setBounds(area.withSizeKeepingCentre(area.getWidth(), 25));
  name_->setBounds(area);
}

// Grip drag: the grip forwards its mouse events here (addMouseListener).
void PresetRow::mouseDown(const juce::MouseEvent& e) {
  if (fromGrip(e) && e.mods.isLeftButtonDown()) host_.rowDragBegin(*this, e.getEventRelativeTo(&host_.rowContentSpace()).getPosition());
}

void PresetRow::mouseDrag(const juce::MouseEvent& e) {
  if (fromGrip(e)) host_.rowDragMove(*this, e.getEventRelativeTo(&host_.rowContentSpace()).getPosition());
}

void PresetRow::mouseUp(const juce::MouseEvent& e) {
  if (fromGrip(e)) host_.rowDragEnd(*this, e.getEventRelativeTo(&host_.rowContentSpace()).getPosition());
}

}  // namespace t3k::ui::presetlist
