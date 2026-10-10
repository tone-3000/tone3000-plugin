#include "Paginator.h"

#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");
}

Paginator::Paginator() {
  // One Tab stop for the whole row (Left / Right turn the page), not one
  // per number; a click never focuses it.
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
  setTitle("Pages");
  rebuild();
}

void Paginator::turnTo(int page) {
  if (page >= 1 && page <= totalPages_ && page != page_ && onPageChange) onPageChange(page);
}

bool Paginator::keyPressed(const juce::KeyPress& key) {
  const int step = key.isKeyCode(juce::KeyPress::leftKey) ? -1 : key.isKeyCode(juce::KeyPress::rightKey) ? 1 : 0;
  if (step == 0) return false;
  turnTo(page_ + step);
  return true;
}

namespace {
class PagesValue : public juce::AccessibilityValueInterface {
public:
  explicit PagesValue(Paginator& owner) : owner_(owner) {}
  bool isReadOnly() const override { return false; }
  double getCurrentValue() const override { return owner_.page(); }
  juce::String getCurrentValueAsString() const override {
    return "Page " + juce::String(owner_.page()) + " of " + juce::String(owner_.totalPages());
  }
  void setValue(double page) override { owner_.turnTo(juce::roundToInt(page)); }
  void setValueAsString(const juce::String& text) override { setValue(text.getIntValue()); }
  AccessibleValueRange getRange() const override { return {{1.0, static_cast<double>(owner_.totalPages())}, 1.0}; }

private:
  Paginator& owner_;
};
}  // namespace

std::unique_ptr<juce::AccessibilityHandler> Paginator::createAccessibilityHandler() {
  juce::AccessibilityHandler::Interfaces interfaces;
  interfaces.value = std::make_unique<PagesValue>(*this);
  return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider,
                                                      juce::AccessibilityActions(), std::move(interfaces));
}

void Paginator::set(int page, int totalPages) {
  if (page == page_ && totalPages == totalPages_) return;
  page_ = page;
  totalPages_ = totalPages;
  rebuild();
}

std::vector<int> Paginator::pagesFor(int page, int totalPages) {
  std::vector<int> pages;
  if (totalPages > 7) {
    if (page <= 3) {
      for (int i = 1; i <= 4; ++i) pages.push_back(i);
      pages.push_back(0);
      pages.push_back(totalPages);
    } else if (page >= totalPages - 2) {
      pages.push_back(1);
      pages.push_back(0);
      for (int i = totalPages - 3; i <= totalPages; ++i) pages.push_back(i);
    } else {
      pages = {1, 0, page - 1, page, page + 1, 0, totalPages};
    }
  } else {
    for (int i = 1; i <= totalPages; ++i) pages.push_back(i);
  }
  return pages;
}

void Paginator::rebuild() {
  items_.clear();
  const auto font = Fonts::sans(kPx);
  const int line = Fonts::normalLineHeight(kPx);
  const int pageH = line + 2 * kPagePadY + 2;  // 1px borders
  const int arrowW = kArrowIcon + 2 * kArrowPad;
  int x = 0;
  auto add = [&](Kind kind, int page, int w, int h) {
    items_.push_back({kind, page, {x, (kHeight - h) / 2, w, h}});
    x += w + kGap;
  };
  if (page_ > 1) add(Kind::prev, 0, arrowW, kArrowIcon + 2 * kArrowPad);
  for (const int p : pagesFor(page_, totalPages_)) {
    if (p == 0)
      add(Kind::ellipsis, 0, juce::roundToInt(Fonts::width(font, kEllipsis)) + 2 * kEllipsisPadX, line + 2 * kPagePadY);
    else
      add(Kind::page, p, juce::roundToInt(Fonts::width(font, juce::String(p))) + 2 * kPagePadX + 2, pageH);
  }
  if (page_ < totalPages_) add(Kind::next, 0, arrowW, kArrowIcon + 2 * kArrowPad);
  setSize(std::max(0, x - kGap), kHeight);
  repaint();
}

void Paginator::paint(juce::Graphics& g) {
  const auto font = Fonts::sans(kPx);
  for (const auto& item : items_) {
    const auto box = item.bounds.toFloat();
    switch (item.kind) {
      case Kind::prev:
      case Kind::next:
        Icons::draw(g, item.kind == Kind::prev ? Icon::ArrowLeft : Icon::ArrowRight,
                    juce::Rectangle<float>(kArrowIcon, kArrowIcon).withCentre(box.getCentre()), theme::kMuted);
        break;
      case Kind::ellipsis:
        paint::text(g, kEllipsis, item.bounds, font, theme::kMuted, juce::Justification::centred);
        break;
      case Kind::page: {
        const bool current = item.page == page_;
        paint::border(g, box, kPageCorner, current ? theme::kWhite : theme::kBorder);
        paint::text(g, juce::String(item.page), item.bounds, font, current ? theme::kWhite : theme::kMuted,
                    juce::Justification::centred);
        break;
      }
    }
  }
}

const Paginator::Item* Paginator::hit(juce::Point<int> p) const {
  for (const auto& item : items_)
    if (item.kind != Kind::ellipsis && item.bounds.contains(p)) return &item;
  return nullptr;
}

void Paginator::mouseMove(const juce::MouseEvent& e) {
  setMouseCursor(hit(e.getPosition()) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Paginator::mouseExit(const juce::MouseEvent&) { setMouseCursor(juce::MouseCursor::NormalCursor); }

void Paginator::mouseUp(const juce::MouseEvent& e) {
  if (!e.mouseWasClicked() || !onPageChange) return;
  const auto* item = hit(e.getPosition());
  if (item == nullptr) return;
  switch (item->kind) {
    case Kind::prev: return onPageChange(page_ - 1);
    case Kind::next: return onPageChange(page_ + 1);
    case Kind::page: return onPageChange(item->page);
    case Kind::ellipsis: return;
  }
}

}  // namespace t3k::ui
