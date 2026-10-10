// The small components inside the preset browser panel that are not list
// items: the bulk-action bar, the "move to" picker, the inline new-category
// row, the "Your Presets" header with its create button, and the in-panel
// confirm card. Included by PresetBrowsePanel.cpp only.
#pragma once

#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/preset/PresetBrowsePanel.h"

namespace t3k::ui {

namespace parts {

inline const juce::Colour kMutedText = theme::kGray;
inline const juce::Colour kDanger{0xffff6b6b};

// Outlined pill-ish button: [glyph] label (the bulk bar's actions).
class Chip : public Clickable {
public:
  Chip(Icon icon, juce::String text, juce::Colour colour, help::Key key)
      : Clickable(text), icon_(icon), text_(std::move(text)), colour_(colour) {
    setHelpText(help::text(key));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setSize(8 + 12 + 4 + juce::roundToInt(Fonts::width(Fonts::sans(11), text_)) + 8, 24);
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    paint::border(g, getLocalBounds().toFloat(), 6.0f, theme::kBorder);
    auto area = getLocalBounds().reduced(8, 0);
    Icons::draw(g, icon_, juce::Rectangle<float>(12, 12).withCentre(area.removeFromLeft(12).toFloat().getCentre()),
                colour_);
    area.removeFromLeft(4);
    paint::text(g, text_, area, Fonts::sans(11), colour_);
  }

private:
  Icon icon_;
  juce::String text_;
  juce::Colour colour_;
};

// One line of text as a button (the move picker's rows).
class MenuRow : public Clickable {
public:
  explicit MenuRow(juce::String text) : Clickable(text), text_(std::move(text)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void paintButton(juce::Graphics& g, bool highlighted, bool) override {
    if (highlighted) paint::fill(g, getLocalBounds().toFloat(), 4.0f, juce::Colours::white.withAlpha(0.08f));
    paint::text(g, text_, getLocalBounds().reduced(8, 0), Fonts::sans(12), theme::kWhite);
  }

private:
  juce::String text_;
};

// Pill button with a filled or outlined body and a centred label.
class PillLabel : public Clickable {
public:
  PillLabel(juce::String text, std::optional<juce::Colour> fill) : Clickable(text), text_(std::move(text)), fill_(fill) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds().toFloat();
    if (fill_) paint::fill(g, box, 6.0f, *fill_);
    else paint::border(g, box, 6.0f, theme::kBorder);
    paint::text(g, text_, getLocalBounds(), Fonts::sans(12, fill_.has_value()), theme::kWhite,
                juce::Justification::centred);
  }

private:
  juce::String text_;
  std::optional<juce::Colour> fill_;
};

}  // namespace parts

// "N selected" · Move to · Duplicate · Delete · ×
class PresetBrowsePanel::BulkBar : public juce::Component {
public:
  BulkBar()
      : move_(Icon::Folder, "Move to", theme::kWhite, help::Key::bulkMove),
        duplicate_(Icon::Copy, "Duplicate", theme::kWhite, help::Key::bulkDuplicate),
        remove_(Icon::Trash2, "Delete", parts::kDanger, help::Key::bulkDelete),
        clear_(Icon::X, 13, 4, help::Key::presetDeselectAll) {
    move_.onClick = [this] { if (onMove) onMove(); };
    duplicate_.onClick = [this] { if (onDuplicate) onDuplicate(); };
    remove_.onClick = [this] { if (onDelete) onDelete(); };
    clear_.onClick = [this] { if (onClear) onClear(); };
    for (auto* c : {static_cast<juce::Component*>(&move_), static_cast<juce::Component*>(&duplicate_),
                    static_cast<juce::Component*>(&remove_), static_cast<juce::Component*>(&clear_)})
      addAndMakeVisible(*c);
    setSize(0, kBulkBarHeight);
  }

  void setCount(int n) {
    count_ = n;
    repaint();
  }

  std::function<void()> onMove, onDuplicate, onDelete, onClear;

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, 8.0f, juce::Colours::white.withAlpha(0.08f));
    paint::border(g, box, 8.0f, theme::kBorder);
    paint::text(g, juce::String(count_) + " selected", getLocalBounds().withTrimmedLeft(10).withWidth(72),
                Fonts::sans(12, true), theme::kWhite);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(8, 0);
    clear_.setBounds(area.removeFromRight(clear_.getWidth()).withSizeKeepingCentre(clear_.getWidth(), clear_.getHeight()));
    area.removeFromRight(4);
    for (auto* c : {static_cast<parts::Chip*>(&remove_), &duplicate_, &move_}) {
      c->setBounds(area.removeFromRight(c->getWidth()).withSizeKeepingCentre(c->getWidth(), c->getHeight()));
      area.removeFromRight(6);
    }
  }

private:
  parts::Chip move_, duplicate_, remove_;
  presetlist::GlyphButton clear_;
  int count_ = 0;
};

// The root plus every category, as rows.
class PresetBrowsePanel::MovePicker : public juce::Component {
public:
  static constexpr int kRowHeight = 28, kPad = 4, kMaxHeight = 140;

  void set(const std::vector<juce::String>& categories) {
    rows_.clear();
    names_.clear();
    names_.push_back({});
    auto add = [this](const juce::String& label, const juce::String& name) {
      auto row = std::make_unique<parts::MenuRow>(label);
      row->onClick = [this, name] { if (onPick) onPick(name); };
      addAndMakeVisible(*row);
      rows_.push_back(std::move(row));
    };
    add("Your Presets (root)", {});
    for (const auto& c : categories) add(c, c);
    setSize(getWidth(), kPad * 2 + kRowHeight * static_cast<int>(rows_.size()));
    resized();
  }

  int preferredHeight() const { return std::min(kMaxHeight, kPad * 2 + kRowHeight * static_cast<int>(rows_.size())); }

  std::function<void(const juce::String&)> onPick;

  void paint(juce::Graphics& g) override {
    paint::fill(g, getLocalBounds().toFloat(), 8.0f, juce::Colour(0xff1c1c1e));
    paint::border(g, getLocalBounds().toFloat(), 8.0f, theme::kBorder);
  }
  void resized() override {
    int y = kPad;
    for (auto& row : rows_) {
      row->setBounds(kPad, y, getWidth() - kPad * 2, kRowHeight);
      y += kRowHeight;
    }
  }

private:
  std::vector<std::unique_ptr<parts::MenuRow>> rows_;
  std::vector<juce::String> names_;
};

// [folder] [new category…] [✓] [×]
class PresetBrowsePanel::CreateRow : public juce::Component {
public:
  CreateRow()
      : ok_(Icon::Check, 14, 3, help::Key::categoryCreate), cancel_(Icon::X, 14, 3, help::Key::categoryCreate) {
    field_.setPlaceholder("New category...");
    field_.setPadding(4, 8, 8);
    field_.setCornerRadius(6);
    field_.setFontSize(12.0f);
    field_.setMaxLength(kMaxCategoryNameLength);
    field_.onEnter = [this] { if (onCommit) onCommit(field_.text()); };
    field_.onEscape = [this] { if (onCancel) onCancel(); };
    field_.onChange = [this](const juce::String& t) { if (onChange) onChange(t); };
    ok_.onClick = [this] { if (onCommit) onCommit(field_.text()); };
    cancel_.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(field_);
    addAndMakeVisible(ok_);
    addAndMakeVisible(cancel_);
    setSize(0, 38);
  }
  void setText(const juce::String& t) { field_.setText(t); }
  void focus() { field_.focus(); }

  std::function<void(const juce::String&)> onCommit, onChange;
  std::function<void()> onCancel;

  void paint(juce::Graphics& g) override {
    Icons::draw(g, Icon::Folder, juce::Rectangle<float>(14, 14).withCentre({12.0f, getHeight() * 0.5f}), parts::kMutedText);
  }
  void resized() override {
    auto area = getLocalBounds().reduced(4, 6);
    area.removeFromLeft(22);
    cancel_.setBounds(area.removeFromRight(cancel_.getWidth()).withSizeKeepingCentre(cancel_.getWidth(), cancel_.getHeight()));
    area.removeFromRight(2);
    ok_.setBounds(area.removeFromRight(ok_.getWidth()).withSizeKeepingCentre(ok_.getWidth(), ok_.getHeight()));
    area.removeFromRight(6);
    field_.setBounds(area);
  }

private:
  TextField field_;
  presetlist::GlyphButton ok_, cancel_;
};

// "Your Presets" ............ [folder+]
class PresetBrowsePanel::SectionHeader : public juce::Component {
public:
  explicit SectionHeader(bool creating) : create_(Icon::FolderPlus, 15, 3, help::Key::categoryCreate) {
    create_.setColour(creating ? theme::kWhite : parts::kMutedText);
    create_.onClick = [this] { if (onCreate) onCreate(); };
    addAndMakeVisible(create_);
    setSize(0, 32);
  }
  std::function<void()> onCreate;
  void paint(juce::Graphics& g) override {
    paint::text(g, "Your Presets", getLocalBounds().reduced(4, 0), Fonts::sans(14, true), theme::kGray);
  }
  void resized() override {
    create_.setBounds(getLocalBounds().removeFromRight(create_.getWidth() + 4).withSizeKeepingCentre(
        create_.getWidth(), create_.getHeight()));
  }

private:
  presetlist::GlyphButton create_;
};

// Scrim + card over the whole panel (ConfirmModal in PresetBar.tsx).
class PresetBrowsePanel::ConfirmCard : public juce::Component {
public:
  static constexpr int kCardWidth = 300, kPadding = 16;

  ConfirmCard(juce::String title, juce::String message)
      : title_(std::move(title)), message_(std::move(message)), cancel_("Cancel", std::nullopt),
        confirm_("Delete", juce::Colour(0xffd32f2f)) {
    cancel_.onClick = [this] { if (onCancel) onCancel(); };
    confirm_.onClick = [this] { if (onConfirm) onConfirm(); };
    addAndMakeVisible(cancel_);
    addAndMakeVisible(confirm_);
  }

  std::function<void()> onConfirm, onCancel;

  void paint(juce::Graphics& g) override {
    paint::fill(g, getLocalBounds().toFloat(), theme::kPanelCorner, juce::Colours::black.withAlpha(0.75f));
    const auto card = cardBounds();
    paint::fill(g, card.toFloat(), 12.0f, juce::Colour(0xff1c1c1e));
    paint::border(g, card.toFloat(), 12.0f, theme::kBorder);
    auto area = card.reduced(kPadding);
    paint::text(g, title_, area.removeFromTop(18), Fonts::sans(14, true), theme::kWhite);
    area.removeFromTop(8);
    layout(area.getWidth()).draw(g, area.removeFromTop(messageHeight(area.getWidth())).toFloat());
  }

  void resized() override {
    const auto card = cardBounds();
    auto row = card.reduced(kPadding).removeFromBottom(28);
    confirm_.setBounds(row.removeFromRight(72));
    row.removeFromRight(8);
    cancel_.setBounds(row.removeFromRight(72));
  }

  void mouseDown(const juce::MouseEvent&) override {}  // swallow: the list below is inert

private:
  juce::TextLayout layout(int width) const {
    juce::AttributedString text(message_);
    text.setFont(Fonts::sans(12));
    text.setColour(parts::kMutedText);
    juce::TextLayout l;
    l.createLayout(text, static_cast<float>(width));
    return l;
  }
  int messageHeight(int width) const { return juce::roundToInt(std::ceil(layout(width).getHeight())); }
  juce::Rectangle<int> cardBounds() const {
    const int inner = kCardWidth - kPadding * 2;
    const int h = kPadding + 18 + 8 + messageHeight(inner) + 12 + 28 + kPadding;
    return juce::Rectangle<int>(std::min(kCardWidth, getWidth() - 32), h).withCentre(getLocalBounds().getCentre());
  }

  juce::String title_, message_;
  parts::PillLabel cancel_, confirm_;
};

}  // namespace t3k::ui
