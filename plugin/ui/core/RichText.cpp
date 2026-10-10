#include "RichText.h"

#include <cmath>

#include "Fonts.h"

namespace t3k::ui {

namespace {
const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");

juce::Font runFont(const TextRun& run, float px) { return Fonts::sans(px, run.bold, run.italic); }

void drawUnderline(juce::Graphics& g, const juce::Font& font, float x, float width, float baseline) {
  // CSS text-decoration: a 1px rule just under the baseline.
  g.fillRect(juce::Rectangle<float>(x, baseline + font.getDescent() * 0.35f, width, 1.0f));
}
}  // namespace

// RichLine
float RichLine::width(const RichText& runs, float px) {
  float w = 0;
  for (const auto& run : runs) w += Fonts::width(runFont(run, px), run.text);
  return w;
}

void RichLine::draw(juce::Graphics& g, const RichText& runs, float px, juce::Point<float> origin, float width,
                    float lineHeightPx, juce::Colour colour) {
  g.setColour(colour);
  const float baseline = origin.y + Fonts::cssBaseline(Fonts::sans(px), lineHeightPx);
  const bool clipped = RichLine::width(runs, px) > width;
  float x = origin.x;
  juce::GlyphArrangement glyphs;
  for (const auto& run : runs) {
    const auto font = runFont(run, px);
    auto text = run.text;
    const float ellipsisW = clipped ? Fonts::width(font, kEllipsis) : 0;
    if (clipped && x + Fonts::width(font, text) + ellipsisW > origin.x + width) {
      // The browser ellipsises at a character boundary; so do we.
      while (text.isNotEmpty() && x + Fonts::width(font, text + kEllipsis) > origin.x + width)
        text = text.dropLastCharacters(1);
      glyphs.addLineOfText(font, text + kEllipsis, x, baseline);
      break;
    }
    glyphs.addLineOfText(font, text, x, baseline);
    x += Fonts::width(font, text);
  }
  glyphs.draw(g);
}

// RichFlow
RichFlow::RichFlow(const RichText& runs, float px, float lineHeightPx, float width)
    : px_(px), lineHeightPx_(lineHeightPx), width_(width) {
  // Tokenise: each word keeps its run's style and remembers whether
  // whitespace preceded it (runs of whitespace collapse to one space). The
  // space keeps the style of the run it was written in, as in CSS: a
  // trailing space in "Visit " is plain even when the next run is
  // underlined, while a link's own leading space underlines with it.
  struct Token {
    TextRun run;  // text = the word
    bool spaceBefore;
    bool paragraphBreak;
    TextRun spaceStyle;  // style of the space before, when spaceBefore
  };
  std::vector<Token> tokens;
  bool pendingSpace = false;
  TextRun pendingStyle;
  auto styleOf = [](const TextRun& run) {
    TextRun style = run;
    style.text.clear();
    return style;
  };
  for (const auto& run : runs) {
    if (isParagraphBreak(run)) {
      tokens.push_back({run, false, true, {}});
      pendingSpace = false;
      continue;
    }
    if (run.box) {
      // An inline block is one unbreakable word; spacing around it comes
      // from the neighbouring runs' whitespace, as in the DOM.
      tokens.push_back({run, pendingSpace, false, pendingStyle});
      pendingSpace = false;
      continue;
    }
    if (run.text.isNotEmpty() && juce::CharacterFunctions::isWhitespace(run.text[0])) {
      pendingSpace = true;
      pendingStyle = styleOf(run);
    }
    juce::StringArray words;
    words.addTokens(run.text, " \t\r\n", {});
    words.removeEmptyStrings();
    for (const auto& word : words) {
      TextRun piece = run;
      piece.text = word;
      tokens.push_back({piece, pendingSpace, false, pendingStyle});
      pendingSpace = true;
      pendingStyle = styleOf(run);
    }
    if (run.text.isNotEmpty() && !juce::CharacterFunctions::isWhitespace(run.text.getLastCharacter()))
      pendingSpace = false;
  }

  // Greedy fill; a lone over-long word gets its own line, as in CSS.
  Line line;
  auto append = [&](const TextRun& run, const juce::String& text) {
    if (!line.pieces.empty() && line.pieces.back().run.styledLike(run)) {
      line.pieces.back().run.text += text;
    } else {
      TextRun piece = run;
      piece.text = text;
      line.pieces.push_back({piece, 0});
    }
  };
  auto pieceWidth = [&](const Piece& p) {
    return p.run.box ? p.run.box->width : Fonts::width(runFont(p.run, px_), p.run.text);
  };
  auto measure = [&] {
    line.width = 0;
    for (auto& p : line.pieces) line.width += (p.width = pieceWidth(p));
  };
  auto flush = [&] {
    measure();
    lines_.push_back(std::move(line));
    line = Line();
  };
  for (const auto& token : tokens) {
    if (token.paragraphBreak) {
      flush();
      continue;
    }
    const auto font = runFont(token.run, px_);
    const float wordW = token.run.box ? token.run.box->width : Fonts::width(font, token.run.text);
    // The space before a word keeps the style it was written in.
    const TextRun& spaceStyle = token.spaceStyle;
    const float spaceW = Fonts::width(runFont(spaceStyle, px_), " ");
    if (!line.pieces.empty() && line.width + (token.spaceBefore ? spaceW : 0) + wordW > width_) flush();
    if (token.spaceBefore && !line.pieces.empty()) {
      append(spaceStyle, " ");
      line.width += spaceW;
    }
    if (token.run.box) {
      line.pieces.push_back({token.run, wordW});
    } else {
      append(token.run, token.run.text);
    }
    line.width += wordW;
  }
  if (!line.pieces.empty() || lines_.empty()) flush();
  placeLines();
}

// Line boxes: the strut (the paragraph font on the line-height grid) sets
// each line's height and baseline; an inline box reaching past the strut
// above or below stretches that one line, as CSS does.
void RichFlow::placeLines() {
  const float strutBaseline = Fonts::cssBaseline(Fonts::sans(px_), lineHeightPx_);
  float y = 0;
  for (auto& line : lines_) {
    float above = strutBaseline, below = lineHeightPx_ - strutBaseline;
    for (const auto& piece : line.pieces) {
      if (const auto& box = piece.run.box) {
        above = juce::jmax(above, box->height - box->descent);
        below = juce::jmax(below, box->descent);
      }
    }
    line.top = y;
    line.baseline = above;
    line.height = above + below;
    y += line.height;
  }
}

juce::Font RichFlow::fontFor(const TextRun& run) const { return runFont(run, px_); }

float RichFlow::height() const {
  return lines_.empty() ? 0 : lines_.back().top + lines_.back().height;
}

float RichFlow::maxLineWidth() const {
  float w = 0;
  for (const auto& line : lines_) w = juce::jmax(w, line.width);
  return w;
}

float RichFlow::lineX(const Line& line, float originX, juce::Justification align) const {
  if (align.testFlags(juce::Justification::horizontallyCentred)) return originX + (width_ - line.width) / 2;
  if (align.testFlags(juce::Justification::right)) return originX + width_ - line.width;
  return originX;
}

void RichFlow::draw(juce::Graphics& g, juce::Point<float> origin, juce::Colour colour,
                    juce::Justification align) const {
  // One glyph arrangement per colour keeps the common case (one colour) to
  // a single draw call; a coloured run flushes what came before it.
  juce::GlyphArrangement glyphs;
  auto flushGlyphs = [&](juce::Colour c) {
    g.setColour(c);
    glyphs.draw(g);
    glyphs.clear();
  };
  for (const auto& line : lines_) {
    float x = std::round(lineX(line, origin.x, align));
    // Blink snaps each baseline to the pixel grid from its absolute
    // (fractional) position; the origin carries the caller's fraction.
    const float baseline = std::round(origin.y + line.top + line.baseline);
    for (const auto& piece : line.pieces) {
      if (const auto& box = piece.run.box) {
        if (box->paint)
          box->paint(g, juce::Rectangle<float>(x, baseline + box->descent - box->height, box->width, box->height));
        x += piece.width;
        continue;
      }
      const auto font = fontFor(piece.run);
      const auto pieceColour = piece.run.colour.value_or(colour);
      if (piece.run.colour) flushGlyphs(colour);
      glyphs.addLineOfText(font, piece.run.text, x, baseline);
      if (piece.run.underline) {
        g.setColour(pieceColour);
        drawUnderline(g, font, x, piece.width, baseline);
      }
      if (piece.run.colour) flushGlyphs(pieceColour);
      x += piece.width;
    }
  }
  flushGlyphs(colour);
}

juce::String RichFlow::linkAt(juce::Point<float> point, juce::Point<float> origin, juce::Justification align) const {
  for (const auto& line : lines_) {
    const float top = origin.y + line.top;
    if (point.y < top || point.y >= top + line.height) continue;
    float x = std::round(lineX(line, origin.x, align));
    for (const auto& piece : line.pieces) {
      if (piece.run.href.isNotEmpty() && point.x >= x && point.x < x + piece.width) return piece.run.href;
      x += piece.width;
    }
  }
  return {};
}

// Html
RichText Html::toRichText(const juce::String& html) {
  RichText out;
  TextRun style;
  int orderedIndex = 0;
  bool ordered = false;
  auto emit = [&](const juce::String& text) {
    if (text.isEmpty()) return;
    if (!out.empty() && !isParagraphBreak(out.back()) && out.back().styledLike(style)) {
      out.back().text += text;
    } else {
      TextRun run = style;
      run.text = text;
      out.push_back(run);
    }
  };
  auto breakParagraph = [&] {
    if (!out.empty() && !isParagraphBreak(out.back())) out.push_back(paragraphBreak());
  };
  auto decode = [](juce::String s) {
    return s.replace("&nbsp;", juce::String::fromUTF8("\xc2\xa0"))
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&amp;", "&");
  };

  int pos = 0;
  while (pos < html.length()) {
    const int lt = html.indexOfChar(pos, '<');
    if (lt < 0) {
      emit(decode(html.substring(pos)));
      break;
    }
    emit(decode(html.substring(pos, lt)));
    const int gt = html.indexOfChar(lt, '>');
    if (gt < 0) break;
    const auto tag = html.substring(lt + 1, gt).trim();
    pos = gt + 1;
    const bool closing = tag.startsWithChar('/');
    const auto name = (closing ? tag.substring(1) : tag).upToFirstOccurrenceOf(" ", false, false).toLowerCase();
    if (name == "b" || name == "strong") {
      style.bold = !closing;
    } else if (name == "i" || name == "em") {
      style.italic = !closing;
    } else if (name == "a") {
      if (closing) {
        style.href.clear();
      } else {
        const auto href = tag.fromFirstOccurrenceOf("href=", false, true).trim();
        const auto quoted = href.startsWithChar('"') || href.startsWithChar('\'')
                                ? href.substring(1).upToFirstOccurrenceOf(href.substring(0, 1), false, false)
                                : href.upToFirstOccurrenceOf(" ", false, false);
        // Only http(s) links; anything else (javascript:, file:) is text.
        style.href = quoted.startsWithIgnoreCase("http://") || quoted.startsWithIgnoreCase("https://") ? quoted : "";
      }
      style.underline = style.href.isNotEmpty();  // remote copy's links read as links
    } else if (name == "p" || name == "br") {
      breakParagraph();
    } else if (name == "ul" || name == "ol") {
      if (!closing) {
        ordered = name == "ol";
        orderedIndex = 0;
      }
      breakParagraph();
    } else if (name == "li") {
      if (!closing) {
        breakParagraph();
        emit(ordered ? juce::String(++orderedIndex) + ". " : juce::String::fromUTF8("\xe2\x80\xa2 "));
      }
    }
    // Any other tag is dropped; its text stays.
  }
  while (!out.empty() && isParagraphBreak(out.back())) out.pop_back();
  return out;
}

}  // namespace t3k::ui
