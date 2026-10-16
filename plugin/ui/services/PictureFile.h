// Picture files for the Library (folder pictures): JUCE's formats (PNG, JPEG,
// GIF) and WebP, through libwebp's decoder (bundled, BSD; see NativeUi.cmake).
// Used once, when a picture is chosen: the Library keeps a WebP as a PNG.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace t3k::ui::picture_file {

// The image in `file`, a format JUCE reads or WebP; invalid when neither.
juce::Image load(const juce::File& file);

// Formats a picture may be chosen in, for a file chooser ("*.png;*.jpg;...").
juce::String patterns();

}  // namespace t3k::ui::picture_file
