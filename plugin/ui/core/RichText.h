// Runs of text with mixed style in one paragraph ("<b>No audio input.</b>
// No input device is selected."), and how to lay them out the way the
// browser did: one line clipped with an ellipsis (the app banner), or
// wrapped on a line-height grid (settings alerts, the update notice).
// Html::toRichText reads the small formatting subset remote copy may use.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace t3k::ui {

// An inline-block inside a paragraph (the LITE/FULL chip or a 12px icon
// sitting in a sentence): a fixed box the flow treats as one unbreakable
// word. `descent` is how far its bottom edge hangs below the baseline
// (CSS vertical-align: -1px → 1; `middle` → height/2 - xHeight/2); a box
// taller than the line's strut grows that line, as in CSS.
struct InlineBox {
  float width = 0;
  float height = 0;
  float descent = 0;
  std::function<void(juce::Graphics&, juce::Rectangle<float>)> paint;
};

struct TextRun {
  juce::String text;
  bool bold = false;
  bool italic = false;
  juce::String href;  // a link, opened by the host view
  bool underline = false;
  std::optional<juce::Colour> colour;  // overrides the paragraph colour
  std::shared_ptr<const InlineBox> box;  // an inline block instead of text

  static TextRun plain(juce::String text) {
    TextRun r;
    r.text = std::move(text);
    return r;
  }
  static TextRun strong(juce::String text) {
    TextRun r = plain(std::move(text));
    r.bold = true;
    return r;
  }
  static TextRun link(juce::String text, juce::String href, std::optional<juce::Colour> colour = {}) {
    TextRun r = plain(std::move(text));
    r.href = std::move(href);
    r.colour = colour;
    return r;
  }
  static TextRun inlineBox(std::shared_ptr<const InlineBox> box) {
    TextRun r;
    r.box = std::move(box);
    return r;
  }

  bool operator==(const TextRun& o) const {
    return text == o.text && bold == o.bold && italic == o.italic && href == o.href &&
           underline == o.underline && colour == o.colour && box == o.box;
  }
  // Adjacent words with equal style merge into one drawn piece; boxes never.
  bool styledLike(const TextRun& o) const {
    return bold == o.bold && italic == o.italic && href == o.href && underline == o.underline &&
           colour == o.colour && box == nullptr && o.box == nullptr;
  }
};
using RichText = std::vector<TextRun>;

// Paragraph breaks inside a RichText are runs of exactly "\n".
inline TextRun paragraphBreak() { return TextRun::plain("\n"); }
inline bool isParagraphBreak(const TextRun& r) { return r.text == "\n"; }

struct RichLine {
  // Draw `runs` on one line box at `origin` of the given width: CSS
  // `white-space: nowrap; overflow: hidden; text-overflow: ellipsis` with an
  // Arial `px` font at `lineHeightPx`.
  static void draw(juce::Graphics& g, const RichText& runs, float px, juce::Point<float> origin, float width,
                   float lineHeightPx, juce::Colour colour);
  static float width(const RichText& runs, float px);
};

// Word-wrapped rich text on a line-height grid (TextFlow for mixed styles).
class RichFlow {
public:
  RichFlow(const RichText& runs, float px, float lineHeightPx, float width);

  int lineCount() const { return static_cast<int>(lines_.size()); }
  // Sum of the line boxes (lineHeightPx per line unless an inline box
  // stretched one).
  float height() const;
  float maxLineWidth() const;

  void draw(juce::Graphics& g, juce::Point<float> origin, juce::Colour colour,
            juce::Justification align = juce::Justification::left) const;

  // The link under `point` (relative to the origin `draw` was given), if any.
  juce::String linkAt(juce::Point<float> point, juce::Point<float> origin,
                      juce::Justification align = juce::Justification::left) const;

private:
  struct Piece {
    TextRun run;
    float width;
  };
  struct Line {
    std::vector<Piece> pieces;
    float width = 0;
    float top = 0;       // offset from the flow's origin
    float height = 0;    // line box height
    float baseline = 0;  // from the line's top
  };
  juce::Font fontFor(const TextRun& run) const;
  float lineX(const Line& line, float originX, juce::Justification align) const;
  void placeLines();

  float px_, lineHeightPx_, width_;
  std::vector<Line> lines_;
};

struct Html {
  // Formatting tags only: p, br, b/strong, i/em, a[href], ul/ol/li. Anything
  // else is dropped (its text kept), so remote copy can never carry markup
  // we don't render. Entities &amp; &lt; &gt; &quot; &#39; &nbsp; decode.
  static RichText toRichText(const juce::String& html);
};

}  // namespace t3k::ui
