// Hardware-style knob with its label slot (port of KnobControl.tsx +
// KnobInner.tsx + KnobFace.tsx). Values are normalised 0..1; a KnobScale
// maps them to units for the readout and the type-in editor.
//
// Interaction conventions (matching typical plugin UX):
// - Drag vertically to adjust; hold Shift for 8x finer control (mid-drag too).
// - The label swaps to a live value readout while dragging (debounced so a
//   quick tap never flashes it) and lingers briefly after release.
// - Double-click opens inline text entry in real units (Enter commits,
//   Escape cancels, blur commits).
// - Alt/Option-click resets to the default value (when one is declared).
// - No scroll-wheel support on purpose: knobs sit inside the horizontally
//   scrolling chain view.
// On a touch screen: double tap resets, tapping the label opens the editor,
// and a touch-and-hold fires onLongPress (the right-click of the platform).
// From the keyboard (Tab to the knob; a click never focuses it): arrows step
// the value (Shift: fine), Home/End go to the ends, Enter opens the type-in
// editor. Screen readers see a slider named by the label, valued in units.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "core/DelayedCall.h"
#include "core/Help.h"
#include "core/KnobScale.h"
#include "core/Theme.h"
#include "widgets/TextField.h"

namespace t3k::ui {

class Knob : public juce::Component {
public:
  // Geometry variants: the artwork is the same, only the value -> angle map
  // and where the value arc grows from differ.
  //  full:     270° sweep, 0..1 = hard-left..hard-right; arc from bottom left.
  //  bipolar:  same sweep, 0.5 = noon means zero; arc grows out of noon.
  //  panLeft:  half track, 0..0.5 = hard left..noon.
  //  panRight: mirrored half track, 0.5..1 = noon..hard right.
  enum class Variant { full, bipolar, panLeft, panRight };
  // Visual tone: primary = a section's headline knob, secondary = its
  // darker, smaller companion trims.
  enum class Thumb { primary, secondary };

  struct Options {
    juce::String label;
    int size = theme::kKnobSizePrimary;
    Thumb thumb = Thumb::primary;
    Variant variant = Variant::full;
    float min = 0.0f, max = 1.0f;
    const KnobScale* scale = &scales::percent();
    // Normalised default; enables Alt/Option-click (touch: double tap) reset.
    std::optional<float> defaultValue;
    // Detented travel: every emitted value snaps to one of `steps` evenly
    // spaced positions across min..max (whole semitones, a 3-way choice),
    // and the arrow keys move one detent at a time.
    std::optional<int> steps;
    std::optional<help::Key> help;
    // Idle label in white instead of GRAY (pan rail section titles).
    bool labelBright = false;
    // Label above the knob instead of below (chain block trims).
    bool labelOnTop = false;
  };

  // Every knob label is 14px in a 17px slot; the editor overflows that slot
  // by 2px each way, which the component's bounds include at the bottom.
  static constexpr int kLabelSize = 14;
  static constexpr int kLabelSlot = 17;  // round(14 * 1.2)
  static constexpr int kEditorOverflow = 2;
  static int heightFor(int size) {
    return size + theme::kKnobLabelGap + kLabelSlot + kEditorOverflow;
  }

  explicit Knob(Options options);
  ~Knob() override;

  int knobSize() const { return options_.size; }
  // The knob face's bounds within this component.
  juce::Rectangle<int> faceBounds() const;

  float value() const { return value_; }
  // External update (parameter echo); ignored mid-drag.
  void setValue(float normalised);

  // Every emitted value: drag steps, typed entries, resets.
  std::function<void(float)> onChange;
  // true on grab / false on release (the owner brackets a host gesture).
  std::function<void(bool)> onDragStateChange;
  // Extra work on reset (after writing defaultValue).
  std::function<void()> onReset;
  // Touch-and-hold on the face (500 ms, 8px slop).
  std::function<void()> onLongPress;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void parentHierarchyChanged() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
  void mouseDoubleClick(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  // Set from outside the drag (keys, screen reader) as one host gesture,
  // with the readout shown for a moment.
  void nudgeTo(float normalised);
  bool resetToDefault();
  void applyLive(float raw, bool fine);
  void emit(float v);
  void openEditor();
  void commitEdit();
  void closeEditor();
  void endDrag();
  // The nearest detent when Options::steps is set; otherwise `v` itself.
  float snapToStep(float v) const;
  void setReadoutVisible(bool show);
  void syncReadout();
  juce::Rectangle<int> labelBounds() const;

  // A label (or readout) may run this far past each side of the column.
  static constexpr int kLabelOverflow = 60;

  Options options_;
  float value_ = 0.0f;    // shown value
  float live_ = 0.0f;     // un-snapped drag accumulator
  float emitted_ = 0.0f;  // last value emitted (detent-snapped)
  bool dragging_ = false;
  bool readoutVisible_ = false;
  float lastY_ = 0.0f;
  juce::Point<float> pressOrigin_;
  bool pressTravelled_ = false;
  // Touch double-tap recogniser: time and position of the previous tap.
  juce::int64 lastTapMs_ = 0;
  juce::Point<float> lastTapPos_;
  bool touchPress_ = false;

  DelayedCall readoutTimer_, holdTimer_;
  std::unique_ptr<TextField> editor_;
  // The readout floats in the overlay layer (see syncReadout); present while
  // it shows there, in which case the label slot here stays blank.
  class Readout;
  std::unique_ptr<Readout> readout_;
};

}  // namespace t3k::ui
