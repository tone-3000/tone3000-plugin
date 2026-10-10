// Bits of chrome the settings copy embeds mid-sentence (Settings.tsx
// LiteFullTogglePreview and the inline <Gauge>/<Equal> glyphs): built as
// RichText inline boxes so a paragraph can wrap around them.
#pragma once

#include <memory>

#include "core/Icons.h"
#include "core/RichText.h"

namespace t3k::ui::inline_chrome {

// Decorative LITE/FULL chip matching the block-header toggle, both cells
// muted (an example of the control, not a selection); `vertical-align:
// middle` with 2px side margins.
std::shared_ptr<const InlineBox> liteFullChip();

// A Lucide glyph at `px` with 2px side margins, sitting 1px under the
// baseline (`vertical-align: -1px`), drawn white.
std::shared_ptr<const InlineBox> icon(Icon icon, float px = 12);

}  // namespace t3k::ui::inline_chrome
