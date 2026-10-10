// Page switcher for the gated streams (ToneBrowser.tsx Paginator, the web
// Paginator port): numbered outline buttons with ellipses past seven pages,
// and arrow prev / next at the ends when there is somewhere to go. Sizes
// itself to its content; the browser right-aligns it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace t3k::ui {

class Paginator : public juce::Component {
public:
  static constexpr int kHeight = 28;  // the 20px arrows + 4px pads

  Paginator();

  void set(int page, int totalPages);
  int page() const { return page_; }
  int totalPages() const { return totalPages_; }
  // Ask for `page` (the keys and screen readers come through here); out of
  // range or current: nothing.
  void turnTo(int page);
  std::function<void(int page)> onPageChange;

  // The page numbers to show, 0 standing in for an ellipsis: all of them up
  // to seven pages, otherwise the ends and a window around `page`.
  static std::vector<int> pagesFor(int page, int totalPages);

  void paint(juce::Graphics& g) override;
  void mouseUp(const juce::MouseEvent& e) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseExit(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  // "Page 3 of 12", adjustable, to a screen reader.
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  enum class Kind { prev, next, page, ellipsis };
  struct Item {
    Kind kind;
    int page = 0;  // Kind::page
    juce::Rectangle<int> bounds;
  };
  static constexpr int kGap = 8;
  static constexpr int kArrowIcon = 20;
  static constexpr int kArrowPad = 4;
  static constexpr int kPagePadX = 12;
  static constexpr int kPagePadY = 4;
  static constexpr int kEllipsisPadX = 8;
  static constexpr float kPx = 13;
  static constexpr float kPageCorner = 6;

  void rebuild();
  const Item* hit(juce::Point<int> p) const;

  int page_ = 1, totalPages_ = 1;
  std::vector<Item> items_;
};

}  // namespace t3k::ui
