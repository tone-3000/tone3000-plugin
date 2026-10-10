// CSS block layout for the settings forms: a column of items, each sized by
// its width (text wraps, so heights follow), stacked with margins. JUCE has
// no intrinsic sizing, so items report `heightFor(width)` and a FormStack
// places them top-down; when an item's content changes height (a toggle
// revealing its controls) it calls heightChanged() and the nearest host
// re-flows up to the scrolling page.
//
// Heights are floats: the web laid these forms out in fractional line
// heights (14px × 1.45) and snapped each box to the pixel grid on paint;
// keeping the fractions and rounding each item's edges reproduces where
// every box landed. Blink snaps each box and each text baseline from its
// *absolute* fractional position, so an item also learns the fraction its
// snapped top lost (subpixelTop) and adds it back when it snaps its own
// children and baselines.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace t3k::ui {

class FormHost;

class FormItem : public juce::Component {
public:
  virtual float heightFor(float width) const = 0;

  // The fraction of a pixel the item's true top sits below its snapped
  // bounds (-0.5 .. 0.5); set by the parent that placed it.
  void setSubpixelTop(float fraction);
  float subpixelTop() const { return subpixelTop_; }

protected:
  // The intrinsic height changed: the nearest FormHost re-flows.
  void heightChanged();
  // Place `child` at the fractional `box` (in this item's coordinates),
  // snapping its edges the way the browser does and handing down the
  // remaining fraction.
  void placeChild(FormItem& child, juce::Rectangle<float> box) const;
  void placeChild(juce::Component& child, juce::Rectangle<float> box) const;

private:
  juce::Rectangle<int> snapped(juce::Rectangle<float> box) const;

  float subpixelTop_ = 0;
};

class FormHost {
public:
  virtual ~FormHost() = default;
  virtual void itemHeightChanged() = 0;
};

// Vertical stack (a flex column / block flow). Items keep their own
// visibility; hidden items take no space. `gap` is the space between
// consecutive visible items on top of each item's own `marginTop`.
class FormStack : public FormItem, public FormHost {
public:
  explicit FormStack(float gap = 0) : gap_(gap) {}

  // Adds (and shows) `item`; `marginTop` is CSS margin-top on the item.
  void add(FormItem& item, float marginTop = 0);
  void setGap(float gap);
  // Space after the last item (a trailing margin-bottom).
  void setTrailing(float trailing);

  // Show / hide an item (hidden items take no space) and re-flow.
  void setShown(FormItem& item, bool shown);

  float heightFor(float width) const override;
  void resized() override;
  // Re-place the children now (a sibling may have grown while another
  // shrank, leaving this stack's own height unchanged) and tell the host.
  void itemHeightChanged() override {
    resized();
    heightChanged();
  }

  // Top of `item` within the stack at its current width (for scrolling to
  // a section), or -1 when not present.
  float topOf(const FormItem& item) const;

private:
  struct Entry {
    FormItem* item;
    float marginTop;
  };
  std::vector<Entry> entries_;
  float gap_;
  float trailing_ = 0;
};

// Empty vertical space (an explicit spacer div).
class FormSpacer : public FormItem {
public:
  explicit FormSpacer(float height) : height_(height) {}
  float heightFor(float) const override { return height_; }

private:
  float height_;
};

// Hosts a plain juce::Component of fixed height as a form item (a select, a
// button row).
class FormBox : public FormItem {
public:
  FormBox(juce::Component& content, float height) : content_(content), height_(height) {
    addAndMakeVisible(content_);
  }
  float heightFor(float) const override { return height_; }
  void resized() override { content_.setBounds(getLocalBounds()); }

private:
  juce::Component& content_;
  float height_;
};

}  // namespace t3k::ui
