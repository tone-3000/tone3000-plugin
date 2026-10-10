#include "GalleryTile.h"

#include "GalleryGeometry.h"

namespace t3k::ui {

GalleryTile::GalleryTile(Services& services, std::string blockId, int size)
    : services_(services), blockId_(std::move(blockId)), size_(size) {
  setSize(size, size);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  // A drag on a tile sorts it; the gaps around the tiles pan the chain view.
  // A quick touch swipe clears this to hand the gesture over (mouseDrag).
  setViewportIgnoreDragFlag(true);
  // Sortable tiles are focusable (keyboard sorting) but draw no focus ring,
  // as the web didn't.
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
}

GalleryTile::~GalleryTile() {
  // The menu lives on the overlay layer; take it down with the tile.
  if (menu_ != nullptr) menu_->close();
}

TileDragHost* GalleryTile::host() { return findParentComponentOfClass<TileDragHost>(); }

void GalleryTile::setTravelling(bool travelling) {
  if (travelling_ == travelling) return;
  travelling_ = travelling;
  setAlpha(travelling ? gallery::kDragGhostOpacity : 1.0f);
  travellingChanged(travelling);
}

void GalleryTile::setDropArmed(bool armed) {
  if (dropArmed_ == armed) return;
  dropArmed_ = armed;
  dropArmedChanged(armed);
  repaint();
}

void GalleryTile::filesDropped(const juce::StringArray& files, int, int) {
  setDropArmed(false);
  services_.localFiles.drop(blockId_, files);
}

std::vector<ContextMenu::Item> GalleryTile::localLoadItems() {
  return {
      {"Load File", Icon::File, help::Key::loadFileTile,
       [this] { services_.localFiles.pick(blockId_, LocalFiles::Kind::file); }},
      {"Load Folder", Icon::FolderClosed, help::Key::loadFolderTile,
       [this] { services_.localFiles.pick(blockId_, LocalFiles::Kind::folder); }},
  };
}

// Menu
void GalleryTile::openMenu(juce::Point<int> at) {
  suppressClickUntilMs_ = juce::Time::currentTimeMillis() + kSuppressClickMs;
  if (auto* old = menu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  menu_ = std::make_unique<ContextMenu>(menuItems());
  menu_->onDismiss = [this] { menuDismissedMs_ = juce::Time::currentTimeMillis(); };
  menu_->openAtPoint(*this, at);
}

void GalleryTile::closeMenu() {
  if (menu_ != nullptr) menu_->close();  // kept alive: we may be inside its row's click
}

// Pointer
void GalleryTile::mouseDown(const juce::MouseEvent& e) {
  const auto now = juce::Time::currentTimeMillis();
  dragging_ = false;
  setViewportIgnoreDragFlag(true);  // re-arm after a pan whose release never came
  pressAt_ = e.getPosition();

  // Right-click / ctrl-click: the action sheet, and the click that follows
  // (macOS ctrl-click fires both) is swallowed.
  if (e.mods.isPopupMenu() && !e.source.isTouch()) {
    openMenu(e.getPosition());
    return;
  }
  // A press that just dismissed the sheet (outside-press) closes it only.
  if (now - menuDismissedMs_ < 100) suppressClickUntilMs_ = now + kSuppressClickMs;

  // A held touch opens the sheet itself, on the system's long-press delay,
  // while the finger is down. Any touch, not only coarse-pointer builds: a
  // Windows tablet is a desktop build with a finger on it, and once the
  // editor takes touch directly (NativeEditor::usesWindowsMultiTouch) the
  // OS no longer synthesises a contextmenu press for us. Where a platform
  // does still turn the hold into a right-click, that path opened the sheet
  // first and this one leaves it alone.
  if (e.source.isTouch()) {
    hold_.start(kLongPressMs, [this, at = e.getPosition()] {
      if (!menuOpen()) openMenu(at.translated(0, kLongPressMenuDrop));
    });
  }
}

void GalleryTile::mouseDrag(const juce::MouseEvent& e) {
  if (dragging_) {
    if (auto* h = host()) h->tileDragMove(e);
    return;
  }
  if (!getViewportIgnoreDragFlag()) return;  // a swipe: the chain view's viewport has it
  const auto travel = e.getPosition() - pressAt_;
  if (hold_.pending() &&
      (std::abs(travel.x) > kLongPressSlop || std::abs(travel.y) > kLongPressSlop))
    hold_.cancel();
  if (!e.mods.isLeftButtonDown() && !e.source.isTouch()) return;
  if (e.getDistanceFromDragStart() < gallery::kDragDistance) return;
  // A quick, mostly sideways touch swipe pans the chain view, as the web
  // view's scroll did; a slower drag still sorts, and the hold still opens
  // the sheet. Clearing the flag hands over: the viewport's drag listener
  // checks it on every move, this one included, and pans from the press with
  // its own inertia. Any touch source: iOS, and a finger on a Windows or
  // Linux tablet, which JUCE reports the same way. A mouse never gets here.
  if (e.source.isTouch() && (e.eventTime - e.mouseDownTime).inMilliseconds() < kFlickMs &&
      std::abs(travel.x) > std::abs(travel.y)) {
    hold_.cancel();
    setViewportIgnoreDragFlag(false);
    return;
  }
  // Past the activation distance the press is a drag; a sheet the hold
  // already opened yields to it.
  closeMenu();
  hold_.cancel();
  dragging_ = true;
  if (auto* h = host()) h->tileDragStart(*this, e);
}

void GalleryTile::mouseUp(const juce::MouseEvent& e) {
  hold_.cancel();
  if (!getViewportIgnoreDragFlag()) {  // the press became a pan: no click, no sort end
    setViewportIgnoreDragFlag(true);
    return;
  }
  if (dragging_) {
    dragging_ = false;
    if (auto* h = host()) h->tileDragEnd(e);
    return;
  }
  if (e.mods.isPopupMenu() && !e.source.isTouch()) return;
  if (!e.mouseWasClicked()) return;
  const auto now = juce::Time::currentTimeMillis();
  if (now < suppressClickUntilMs_) {
    suppressClickUntilMs_ = 0;
    return;
  }
  if (e.mods.isCtrlDown() || e.mods.isCommandDown()) return;
  if (menuOpen()) {
    closeMenu();
    return;
  }
  open();
}

bool GalleryTile::keyPressed(const juce::KeyPress& key) {
  if (auto* h = host()) return h->tileKey(*this, key);
  return false;
}

std::unique_ptr<juce::AccessibilityHandler> GalleryTile::createAccessibilityHandler() {
  return std::make_unique<juce::AccessibilityHandler>(
      *this, juce::AccessibilityRole::button,
      juce::AccessibilityActions()
          .addAction(juce::AccessibilityActionType::press, [this] { open(); })
          .addAction(juce::AccessibilityActionType::showMenu, [this] { openMenu(getLocalBounds().getCentre()); }));
}

}  // namespace t3k::ui
