// The pieces the preset browser's list is built from (port of the row,
// category header and empty-drop-zone components in PresetBar.tsx):
//
//   GroupHeader   chevron · folder/star · name (count) · [delete], a drop
//                 target that also folds its group
//   PresetRow     select-or-active slot · star · name (or rename field) · PC ·
//                 pencil/trash · grip
//   EmptyZone     the dashed "drag presets here" placeholder of an empty group
//
// All three are Items: the panel lays them out in one column and hit-tests a
// drag against them, lighting up the one a release would land on.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

#include "core/Help.h"
#include "core/Icons.h"
#include "model/ChainState.h"
#include "widgets/Clickable.h"
#include "widgets/TextField.h"

namespace t3k::ui::presetlist {

constexpr int kRowHeight = 32;
constexpr int kHeaderHeight = 32;
constexpr int kEmptyZoneHeight = 34;

// Borderless icon button with `pad` around a `glyph`-px icon.
class GlyphButton : public Clickable {
public:
  GlyphButton(Icon icon, int glyph, int pad, help::Key key);
  void setIcon(Icon icon) { icon_ = icon; custom_ = nullptr; repaint(); }
  // A CustomIcons.h glyph instead of a Lucide one (the filled star).
  void setCustomIcon(const char* svg) { custom_ = svg; repaint(); }
  void setHelp(help::Key key);
  void setColour(juce::Colour c) { colour_ = c; repaint(); }
  void setFilled(bool filled) { filled_ = filled; repaint(); }
  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  Icon icon_;
  const char* custom_ = nullptr;
  int glyph_;
  juce::Colour colour_;
  bool filled_ = false;
};

// Something the list shows in a column and a drag can land on.
class Item : public juce::Component {
public:
  void setDropHighlight(bool on) {
    if (on == highlighted_) return;
    highlighted_ = on;
    repaint();
  }
  bool dropHighlight() const { return highlighted_; }

protected:
  // The dashed outline + wash every drop target shares.
  void paintDropHighlight(juce::Graphics& g) const;

private:
  bool highlighted_ = false;
};

// Plain muted/bold text line (section titles, the empty-list message).
class TextLine : public juce::Component {
public:
  TextLine(juce::String text, juce::Font font, juce::Colour colour, juce::BorderSize<int> pad);
  void paint(juce::Graphics& g) override;

private:
  juce::String text_;
  juce::Font font_;
  juce::Colour colour_;
  juce::BorderSize<int> pad_;
};

class GroupHeader : public Item {
public:
  enum class Glyph { folder, star, none };

  GroupHeader(juce::String title, int count, bool collapsed, Glyph glyph, bool deletable);

  // Where a release over this header files a preset: a category name ("" =
  // the root), or Favourites.
  bool isFavourites = false;
  juce::String category;
  bool isDropTarget = false;

  std::function<void()> onToggle;
  std::function<void()> onDelete;

  const juce::String& title() const { return title_; }
  int count() const { return count_; }

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  juce::String title_;
  int count_;
  bool collapsed_;
  Glyph glyph_;
  std::unique_ptr<GlyphButton> delete_;
};

class EmptyZone : public Item {
public:
  explicit EmptyZone(juce::String label) : label_(std::move(label)) { setInterceptsMouseClicks(false, false); }

  bool isFavourites = false;
  juce::String category;

  void paint(juce::Graphics& g) override;

private:
  juce::String label_;
};

// What a row calls back into; the panel implements it.
class RowHost {
public:
  virtual ~RowHost() = default;
  virtual void rowLoad(const juce::String& id) = 0;
  virtual void rowToggleSelected(const juce::String& id) = 0;
  virtual void rowToggleStar(const PresetInfo& preset) = 0;
  virtual void rowBeginRename(const juce::String& id) = 0;
  virtual void rowCommitRename(const PresetInfo& preset, const juce::String& name) = 0;
  virtual void rowCancelRename() = 0;
  virtual void rowDelete(const juce::String& id) = 0;
  // Drag, from the grip. Positions are in the panel's list-content space.
  virtual void rowDragBegin(class PresetRow& row, juce::Point<int> pointer) = 0;
  virtual void rowDragMove(class PresetRow& row, juce::Point<int> pointer) = 0;
  virtual void rowDragEnd(class PresetRow& row, juce::Point<int> pointer) = 0;
  virtual juce::Component& rowContentSpace() = 0;
};

class PresetRow : public Item {
public:
  struct State {
    PresetInfo preset;
    bool active = false;
    bool selectable = false;  // bulk-select mode: a checkbox replaces the active tick
    bool selected = false;
    bool draggable = false;   // shows the grip
    bool renaming = false;
    std::optional<int> pc;
  };

  PresetRow(RowHost& host, State state);
  ~PresetRow() override;

  const PresetInfo& preset() const { return state_.preset; }
  bool draggable() const { return state_.draggable; }
  void focusRename();
  void setDragging(bool dragging) { setAlpha(dragging ? 0.75f : 1.0f); }

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;

private:
  class NameButton;
  bool fromGrip(const juce::MouseEvent& e) const { return grip_ != nullptr && e.eventComponent == grip_.get(); }
  void commitRename();

  RowHost& host_;
  State state_;
  std::unique_ptr<NameButton> name_;
  std::unique_ptr<TextField> rename_;
  std::unique_ptr<GlyphButton> select_, star_, grip_, pencil_, trash_;
  juce::Rectangle<int> activeSlot_, pcArea_;
};

}  // namespace t3k::ui::presetlist
