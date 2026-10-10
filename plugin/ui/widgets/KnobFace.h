// The knob artwork (port of KnobFace.tsx). Source of truth for the geometry
// is design/primary-knob.svg and design/secondary-knob.svg: the same
// hardware knob at two sizes, every radius here normalised to a 200x200
// box. Only the three face layers differ between the tones.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace t3k::ui {

enum class KnobTone { primary, secondary };

// The knob's value-independent layers, rasterised once per (device size,
// tone) and blitted by drawKnobFace (see the note there). On Windows these
// images are Direct2D-backed, and that dictates the cache's lifetime: it
// must never be a process-lifetime static. JUCE shuts itself down when the
// last plugin instance dies; the host then unloads the module, and the CRT
// destroys the plugin's statics *inside DllMain*, loader lock held. Releasing
// the last Direct2D image there tears down the D3D device, which waits on a
// driver worker thread that needs the loader lock: Cubase froze on every
// project close / insert removal on Windows. So the cache is a
// SharedResourcePointer resource instead: every editor (NativeEditor, the
// testbed host) holds one, so it lives exactly as long as some editor does
// and dies before the module is unloaded. drawKnobFace only takes a
// short-lived hold of its own.
struct KnobFaceCache {
  struct Entry {
    int devicePx = 0;
    KnobTone tone = KnobTone::primary;
    juce::Image base, face;
  };
  std::vector<Entry> entries;
};
using KnobFaceCacheHold = juce::SharedResourcePointer<KnobFaceCache>;

// Paints the knob into `box` (square). `angleDeg` is the pointer angle in
// degrees clockwise from noon (-135..135); `arcFromDeg` is where the value
// arc grows from (noon for centred knobs, -135 for the rest).
void drawKnobFace(juce::Graphics& g, juce::Rectangle<float> box, float angleDeg, float arcFromDeg,
                  KnobTone tone);

}  // namespace t3k::ui
