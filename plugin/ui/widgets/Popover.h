// Anchored floating panel (the save popover, preset browser, account menu,
// tile menus…). Lives in the root's overlay layer so it paints above
// everything, is positioned relative to an anchor component, and dismisses
// on a press outside itself/its anchor or on Escape (useDismissable.ts).
// The panel takes its anchor's scale: one opened from a counter-scaled
// subtree (the tone browser's body, at 1x under the window zoom) is 1x too.
// Keyboard: the panel takes focus when it opens and is its own focus
// container, so Tab / Shift+Tab and the arrows walk its rows (buttons)
// without leaving it; Enter presses a row. A panel the keyboard opened (or
// walked) hands focus back to its anchor when it closes; one the mouse
// opened leaves nothing focused, so the host's keys work again.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

// Implemented by PluginRoot: the full-size, click-through layer popovers
// are parented to.
class OverlayHost {
public:
  virtual ~OverlayHost() = default;
  virtual juce::Component& overlayLayer() = 0;
};

class Popover : public juce::Component {
public:
  enum class Align { left, right };
  enum class Placement { below, above };

  Popover();
  ~Popover() override;

  // Show below (or above) `anchor`, `gap` px from its edge, with the panel's
  // left (or right) edge offset `inset` px from the anchor's matching edge.
  // The panel must already have its size set.
  void open(juce::Component& anchor, Align align, int gap, int inset = 0,
            Placement placement = Placement::below);
  // Show with the panel's top-left at a point given in `context`'s local
  // coordinates (a right-click's position): context menus. Any press outside
  // the panel dismisses; the panel is kept inside the overlay.
  void openAt(juce::Component& context, juce::Point<int> point);
  void close();
  // close() + onDismiss (a row pick, or the owner backing out for the user).
  void dismiss();
  bool isOpen() const { return getParentComponent() != nullptr; }

  // Called after the panel is removed (outside press, Escape, or close()).
  std::function<void()> onDismiss;
  // Ignore secondary presses (see isSecondaryPress) anywhere outside: a
  // right-click elsewhere (a tile's context menu) leaves the panel up.
  bool primaryOnly = false;
  // A press on the anchor dismisses too (the anchor is a control, not the
  // panel's toggle).
  bool dismissOnAnchorPress = false;

  bool keyPressed(const juce::KeyPress& key) override;
  std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

  // Every panel draws a 1px border; CSS padding starts inside it.
  static constexpr int kBorder = 1;
  juce::Rectangle<int> contentBounds() const { return getLocalBounds().reduced(kBorder); }

protected:
  // Reposition against the anchor (call after a size change while open).
  void reposition();

private:
  // Adopt `source`'s scale relative to the overlay; returns it.
  float adoptScaleOf(const juce::Component& source);

  // Global mouse listener: a press anywhere outside the panel and its
  // anchor dismisses. Separate object because Component is itself a
  // MouseListener for its own events.
  class OutsidePressWatcher : public juce::MouseListener {
  public:
    explicit OutsidePressWatcher(Popover& owner) : owner_(owner) {}
    void mouseDown(const juce::MouseEvent& e) override { owner_.outsidePress(e); }

  private:
    Popover& owner_;
  };

  void outsidePress(const juce::MouseEvent& e);
  void watchOutsidePresses();
  // Focus the next (or previous) row after the focused one, wrapping.
  void focusRow(bool next);

  OutsidePressWatcher watcher_{*this};
  juce::Time openingPress_;  // mouseDownTime of the press that opened the panel
  bool keyboardOpened_ = false;  // the anchor had focus at open(): focus returns to it
  juce::Component::SafePointer<juce::Component> anchor_;
  Align align_ = Align::left;
  Placement placement_ = Placement::below;
  int gap_ = 0, inset_ = 0;
};

}  // namespace t3k::ui
