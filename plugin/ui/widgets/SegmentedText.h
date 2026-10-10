// Segmented chips (theme.ts segmentedGroupStyle / segmentedCellStyle): a
// 20px track of cells with 4px side padding, each a cap-trimmed mono label
// or a 16px glyph (the EQ view switcher). Cells carry an on/off state
// painted by a Style (the pan rail's yellow-on-black [S|Ø], LITE/FULL, …).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include <optional>

#include "core/Help.h"

namespace t3k::ui {

class SegmentedText : public juce::Component {
public:
  struct Style {
    juce::Colour onBg, onFg, offBg, offFg;
    float fontPx = 12;
    // Track fill; defaults to theme::kSegmentedTrack.
    std::optional<juce::Colour> track;
    // Minimum cell width (the pan rail's [S|Ø] squares up to ICON_BOX).
    int minCellWidth = 0;
  };
  // Grey idle, house-armed yellow while engaged (solo / polarity).
  static Style armed();
  // Selection as text colour only: white vs MUTED on the shared track
  // (LITE/FULL, EQ view).
  static Style selection();

  struct Cell {
    juce::String label;
    // Resolved hint line (help::text(key), or a composed one such as the EQ
    // type selector's "Bell: band curve shape.").
    juce::String help;
    // Glyph cell: an SVG (see CustomIcons.h) tinted with the on/off colour
    // and drawn at iconPx; the label is then just the button name.
    const char* svg = nullptr;
    float iconPx = 16;

    Cell(juce::String l, help::Key key) : label(std::move(l)), help(help::text(key)) {}
    Cell(juce::String l, juce::String helpLine, const char* icon, float px)
        : label(std::move(l)), help(std::move(helpLine)), svg(icon), iconPx(px) {}
  };

  SegmentedText(std::vector<Cell> cells, Style style);
  ~SegmentedText() override;

  void setOn(int index, bool on);
  bool isOn(int index) const;
  // Radio behaviour: exactly `index` on.
  void select(int index);
  // Locked chip (the read-only size chip): no cursor, no clicks; cells keep
  // the given colour.
  void setInteractive(bool interactive);
  std::function<void(int index)> onCellClick;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Segment;
  Style style_;
  std::vector<std::unique_ptr<Segment>> cells_;
};

}  // namespace t3k::ui
