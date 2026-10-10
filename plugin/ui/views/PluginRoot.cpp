#include "PluginRoot.h"

#include "block/BlockCard.h"
#include "library/LibraryDrawer.h"

#include "core/Design.h"
#include "core/Help.h"
#include "core/NoDefaultFocus.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// Faceplate.tsx PLATE_HEIGHT; the toast floats 24px above it.
constexpr int kToastGap = 24;
}  // namespace

PluginRoot::PluginRoot(Services& services)
    : services_(services),
      header_(services),
      hintBar_(services),
      main_(services),
      faceplate_(services),
      toast_(services.toast),
      hintTracker_(services.hints, *this),
      pointerTracker_(services.pointer, *this) {
  setOpaque(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);

  header_.onToggleTuner = [this](bool show) { setTunerShown(show); };
  header_.onStereoToggle = [this](bool stereo) {
    closeTunerThen([&] { services_.chain.setStereoMode(stereo); });
  };
  header_.onUndo = [this] { closeTunerThen([&] { services_.chain.undo(); }); };
  header_.onRedo = [this] { closeTunerThen([&] { services_.chain.redo(); }); };
  header_.presetBar().beforeSave = [this] { closeTunerThen({}); };
  header_.presetBar().beforeLoad = [this] { showChainThen({}); };
  header_.presetBar().onReset = [this] { showChainThen([&] { services_.chain.resetToDefault(); }); };

  banner_.onAction = [this](BannerAction action) { handleBannerAction(action); };
  banner_.onDismiss = [this](const juce::String& id) { services_.banners.dismiss(id); };

  // Network-dependent entry points pass the connection gate first.
  // A plain login lands back on the chain. One sign-in at a time: the
  // header stays live over the sign-in screen, so a second press is not a
  // second browser tab.
  header_.onLogin = [this] {
    if (signInShown()) return;
    services_.connection.requireConnection([this] { services_.session.login(ToneSession::LoginIntent::plain); });
  };
  header_.onLogout = [this] { logout(); };

  // The add / swap browse flows: + and ⇄ open the tone browser through the
  // load flow, which remembers the target slot or block for the pick.
  main_.chainScreen().onSelectTone = [this](ChainSide side, const std::string& id) {
    services_.loadFlow.select(side, id);
  };
  services_.loadFlow.onShowBrowser = [this](bool show) { setBrowserShown(show); };
  // A browse-intent login (a sign-in CTA inside the browser) comes back to
  // the browser.
  services_.session.onAuthenticated = [this] { setBrowserShown(true); };

  header_.onOpenSettings = [this] { openSettings(); };

  // The Library: the header toggles the drawer, a tile's "Add to Library"
  // opens it, and a preset loaded from it leaves any takeover first.
  header_.onToggleLibrary = [this](bool show) { setLibraryShown(show); };
  // Open where this instance had it.
  if (services_.library.savedShown())
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginRoot>(this)] {
      if (safe != nullptr) safe->setLibraryShown(true);
    });
  // Asked from a tile or a block card: leave the tuner / browser (they hide
  // the drawer) but not an open card, which is where Keep was pressed.
  services_.library.onRequestShow = [this] {
    setTunerShown(false);
    if (browserShown()) {
      services_.loadFlow.clearPendingTargets();
      setBrowserShown(false);
    }
    setLibraryShown(true);
  };
  services_.library.beforePresetLoad = [this] { showChainThen({}); };

  addAndMakeVisible(header_);
  addAndMakeVisible(main_);
  addAndMakeVisible(faceplate_);
  addChildComponent(hintBar_);
  addChildComponent(banner_);

  // Popovers, toast and modals sit above everything; the layer itself is
  // click-through.
  overlay_.setInterceptsMouseClicks(false, true);
  addAndMakeVisible(overlay_);
  overlay_.addChildComponent(toast_);

  addMouseListener(&focusPolicy_, true);
  services_.hints.addListener(this);
  services_.banners.addListener(this);
  services_.connection.addListener(this);
  services_.updates.addListener(this);
  services_.session.addListener(this);
  hintsVisible_ = services_.hints.enabled();
  updateChromeHeight();
  bannerChanged();
  connectionProblemChanged();
  updateNoticeChanged();
  authFlowChanged();
}

PluginRoot::~PluginRoot() {
  removeMouseListener(&focusPolicy_);
  if (keyWindow_ != nullptr) keyWindow_->removeKeyListener(&focusPolicy_);
  services_.session.onAuthenticated = nullptr;
  services_.loadFlow.onShowBrowser = nullptr;
  services_.library.onRequestShow = nullptr;
  services_.library.beforePresetLoad = nullptr;
  library_.reset();  // unsubscribes from the stores before they go
  if (watchedParent_ != nullptr) watchedParent_->removeComponentListener(this);
  services_.session.removeListener(this);
  services_.updates.removeListener(this);
  services_.connection.removeListener(this);
  services_.banners.removeListener(this);
  services_.hints.removeListener(this);
}

// Modals
juce::Image PluginRoot::snapshotBeneathOverlay(float scale) {
  juce::Image image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(getWidth() * scale)),
                    juce::jmax(1, juce::roundToInt(getHeight() * scale)), true);
  juce::Graphics g(image);
  g.addTransform(juce::AffineTransform::scale(scale));
  g.fillAll(theme::kBlack);
  for (auto* child : getChildren()) {
    if (child == &overlay_ || !child->isVisible()) continue;
    g.saveState();
    g.addTransform(child->getTransform());  // the chain + plate scaled beside the drawer
    g.setOrigin(child->getPosition());
    child->paintEntireComponent(g, true);
    g.restoreState();
  }
  return image;
}

template <typename Modal, typename... Args>
std::unique_ptr<Modal> PluginRoot::openModal(Args&&... args) {
  auto modal = std::make_unique<Modal>([this](float scale) { return snapshotBeneathOverlay(scale); },
                                       std::forward<Args>(args)...);
  modal->setBounds(getLocalBounds());
  overlay_.addAndMakeVisible(*modal);
  return modal;
}

void PluginRoot::restackModals() {
  if (updateNotice_) updateNotice_->toFront(false);
  if (connectionModal_) connectionModal_->toFront(false);
}

// The sign-in screen is up exactly while the flow is not idle; the screen
// itself follows the phase moves. Posted: the change arrives from inside
// the button handler that started it.
void PluginRoot::authFlowChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const bool inFlight = self->services_.session.authFlow().phase != ToneSession::AuthFlow::Phase::idle;
    if (inFlight == self->signInShown()) return;
    if (inFlight) {
      self->signIn_ = std::make_unique<SignInScreen>(self->services_);
      // Right above the browser when there is one (it is where a browse
      // sign-in returns to), else above the faceplate; under a tuner,
      // Settings and the overlay.
      juce::Component* below = self->browser_ ? static_cast<juce::Component*>(self->browser_.get())
                                              : static_cast<juce::Component*>(&self->faceplate_);
      self->addChildComponent(*self->signIn_, self->getIndexOfChildComponent(below) + 1);
      self->services_.hints.setHover({});
    } else {
      self->signIn_.reset();
    }
    self->syncTakeovers();
    self->resized();
  });
}

// Store changes arrive from inside a modal's own button handler, so the
// modal is rebuilt on the next message-loop turn rather than under its feet.
void PluginRoot::connectionProblemChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const auto& problem = self->services_.connection.problem();
    self->connectionModal_.reset();
    if (!problem) return;
    auto& s = self->services_;
    auto modal = self->openModal<ConnectionModal>(*problem, s.backend.canOpenDateTimeSettings());
    modal->onRetry = [&s] { s.connection.retry(); };
    modal->onDismiss = [&s] { s.connection.dismiss(); };
    modal->onOpenDateTimeSettings = [&s] { s.backend.openDateTimeSettings(); };
    self->connectionModal_ = std::move(modal);
    self->restackModals();
  });
}

void PluginRoot::updateNoticeChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const auto& notice = self->services_.updates.notice();
    self->updateNotice_.reset();
    if (!notice) return;
    auto& s = self->services_;
    auto modal = self->openModal<UpdateNotice>(*notice);
    modal->onRemindLater = [&s](int days) { s.updates.remindLater(days); };
    self->updateNotice_ = std::move(modal);
    self->restackModals();
  });
}

void PluginRoot::hintChanged() {
  if (hintsVisible_ == services_.hints.enabled()) return;
  hintsVisible_ = services_.hints.enabled();
  updateChromeHeight();
}

void PluginRoot::updateChromeHeight() {
  const int hintExtra = hintsVisible_ ? design::kHintHeight : 0;
  const int bannerExtra = bannerPhase_ != BannerPhase::hidden ? AppBanner::kHeight : 0;
  hintBar_.setVisible(hintsVisible_);
  setSize(design::kWidth, design::kHeight + bannerExtra + hintExtra);
  services_.shell.setExtraContentHeight(bannerExtra + hintExtra, hintExtra);
}

// Banner choreography
void PluginRoot::bannerChanged() {
  const auto& active = services_.banners.active();
  if (active) {
    switch (bannerPhase_) {
      case BannerPhase::hidden:
        // From cold the window has to grow first; show once it has.
        bannerPhase_ = BannerPhase::waiting;
        banner_.setSpec(*active);
        updateChromeHeight();
        if (viewportFits()) {
          bannerShow();
        } else {
          bannerWait_.start(kBannerWaitMs, [this] { bannerShow(); });
        }
        break;
      case BannerPhase::waiting:
      case BannerPhase::shown:
        // Rule swaps render directly.
        banner_.setSpec(*active);
        break;
    }
    return;
  }
  if (bannerPhase_ == BannerPhase::hidden) return;
  bannerWait_.cancel();
  bannerPhase_ = BannerPhase::hidden;
  banner_.setVisible(false);
  updateChromeHeight();
}

bool PluginRoot::viewportFits() const {
  // The viewport is the parent the shell scales us into; it fits once it is
  // at least the banner-inclusive box at the current scale (2px tolerance
  // for rounding). No parent yet: the check reruns when one arrives.
  const auto* parent = getParentComponent();
  if (parent == nullptr) return false;
  const float scale = getTransform().mat00;  // pure uniform scale from the shell
  return parent->getHeight() >= designHeight() * scale - 2;
}

void PluginRoot::componentMovedOrResized(juce::Component&, bool, bool) {
  if (bannerPhase_ == BannerPhase::waiting && viewportFits()) bannerShow();
}

void PluginRoot::parentHierarchyChanged() {
  if (auto* top = getTopLevelComponent(); top != this && top != keyWindow_.getComponent()) {
    if (keyWindow_ != nullptr) keyWindow_->removeKeyListener(&focusPolicy_);
    keyWindow_ = top;
    top->addKeyListener(&focusPolicy_);
  }
  auto* parent = getParentComponent();
  if (parent == watchedParent_) return;
  if (watchedParent_ != nullptr) watchedParent_->removeComponentListener(this);
  watchedParent_ = parent;
  if (watchedParent_ != nullptr) watchedParent_->addComponentListener(this);
  componentMovedOrResized(*this, false, true);
}

// Keyboard focus (see the header): JUCE's tab order, with no default.
std::unique_ptr<juce::ComponentTraverser> PluginRoot::createKeyboardFocusTraverser() {
  return std::make_unique<NoDefaultFocus>();
}

bool PluginRoot::FocusPolicy::keyPressed(const juce::KeyPress& key, juce::Component*) {
  // Focus resting on the window itself (a standalone DocumentWindow takes
  // it when the OS activates it) is nothing focused as far as the UI goes.
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused != nullptr && root_.isParentOf(focused)) return false;
  if (root_.blockKey(key)) return true;  // nothing focused: the open card's numbers, A/B
  if (key.isKeyCode(juce::KeyPress::tabKey)) {
    // The Settings takeover is its own Tab cycle while it is up: its content
    // is out of the root's order, and the chrome under it must stay out of
    // reach.
    auto* settings = root_.settingsInFront();
    const auto order = juce::KeyboardFocusTraverser().getAllComponents(
        settings != nullptr ? static_cast<juce::Component*>(settings) : &root_);
    if (order.empty()) return false;
    (key.getModifiers().isShiftDown() ? order.back() : order.front())->grabKeyboardFocus();
    return true;
  }
  // The scroll keys move the screen's main scroller, as a browser scrolls
  // its document with nothing focused. (With a control focused they bubble
  // up from it to the scroller around it instead, DragScroller::keyPressed,
  // so this is the only route they need here.)
  auto* scroller = root_.frontScroller();
  return scroller != nullptr && scroller->scrollByKey(key);
}

SettingsScreen* PluginRoot::settingsInFront() const {
  // A modal over the page takes the keyboard with it.
  if (updateNotice_ != nullptr || connectionModal_ != nullptr) return nullptr;
  return settings_.get();
}

DragScroller* PluginRoot::frontScroller() {
  if (updateNotice_ != nullptr || connectionModal_ != nullptr) return nullptr;  // behind a scrim: nothing moves
  if (settings_ != nullptr) return &settings_->scroller();
  if (tunerShown() || signInShown()) return nullptr;  // neither screen scrolls
  if (browserShown()) return &browser_->scroller();
  return &main_.chainScreen().scroller();
}

void PluginRoot::FocusPolicy::mouseDown(const juce::MouseEvent& e) {
  // Runs after the pressed component's own mouseDown, so a click that gave
  // focus (a text field) has already done so: keep focus when it sits on the
  // pressed component's line of ancestry either way (an editor inside its
  // field, a row inside its popover).
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  auto* pressed = e.eventComponent;
  if (focused != nullptr && pressed != nullptr && root_.isParentOf(focused) && focused != pressed &&
      !focused->isParentOf(pressed) && !pressed->isParentOf(focused)) {
    focused->giveAwayKeyboardFocus();
    focused = nullptr;
  }
  // A click that took the keyboard nowhere (empty space, the faceplate) with
  // a block's card open: the card takes it, so its numbers and A/B work
  // after any click. A host gives the plugin's window the keyboard only when
  // something in it asks. Not a click in the Library drawer (its own keys).
  if (pressed == nullptr || (focused != nullptr && root_.isParentOf(focused))) return;
  if (pressed->findParentComponentOfClass<LibraryDrawer>() != nullptr || dynamic_cast<LibraryDrawer*>(pressed) != nullptr)
    return;
  if (!root_.isParentOf(pressed) && pressed != &root_) return;
  if (auto* card = root_.openCard()) card->grabKeyboardFocus();
}

BlockCard* PluginRoot::openCard() {
  if (settingsInFront() != nullptr || updateNotice_ != nullptr || connectionModal_ != nullptr) return nullptr;
  // The card on screen (one at a time).
  std::function<BlockCard*(juce::Component&)> find = [&find](juce::Component& c) -> BlockCard* {
    if (auto* card = dynamic_cast<BlockCard*>(&c); card != nullptr && card->isShowing()) return card;
    for (auto* child : c.getChildren())
      if (child->isVisible())
        if (auto* card = find(*child)) return card;
    return nullptr;
  };
  return find(*this);
}

bool PluginRoot::blockKey(const juce::KeyPress& key) {
  auto* card = openCard();
  return card != nullptr && card->blockKey(key);
}

bool PluginRoot::keyPressed(const juce::KeyPress& key) {
  // Bubbled up from whatever was focused, unused: the open card's, if it is
  // one of its keys.
  if (blockKey(key)) return true;
  if (key != juce::KeyPress::escapeKey) return false;
  auto* focused = getCurrentlyFocusedComponent();
  if (focused == nullptr || !isParentOf(focused)) return false;
  focused->giveAwayKeyboardFocus();
  return true;
}

void PluginRoot::bannerShow() {
  bannerWait_.cancel();
  bannerPhase_ = BannerPhase::shown;
  banner_.setVisible(true);
  resized();
  if (const auto& spec = services_.banners.active()) {
    juce::String text;
    for (const auto& run : spec->content) text << run.text;
    help::announce(text);
  }
}

void PluginRoot::handleBannerAction(BannerAction action) {
  switch (action) {
    case BannerAction::openSettings:
      openSettings(SettingsScreen::Tab::system);
      break;
    case BannerAction::switchToAsio:
      services_.audioDevice.setDeviceType("ASIO");
      break;
    case BannerAction::openMicSettings:
      services_.audioDevice.openMicSettings();
      break;
  }
}

// Takeovers
void PluginRoot::setTunerShown(bool shown) {
  if (shown == tunerShown()) return;
  if (shown) {
    tuner_ = std::make_unique<TunerView>(services_);
    tuner_->onClose = [this] { setTunerShown(false); };
    addAndMakeVisible(*tuner_);
    overlay_.toFront(false);  // popovers and the toast stay above the takeover
  } else {
    tuner_.reset();
  }
  syncTakeovers();
  header_.setTunerShown(shown);
  resized();
}

void PluginRoot::setBrowserShown(bool shown) {
  if (shown == browserShown()) return;
  if (shown) {
    browser_ = std::make_unique<ToneBrowser>(services_);
    // Closing without picking abandons any pending swap / insert target.
    browser_->onClose = [this] {
      services_.loadFlow.clearPendingTargets();
      setBrowserShown(false);
    };
    // The browser's sign-in CTAs run the login flow and return to this
    // same browser.
    browser_->onSignIn = [this] {
      if (signInShown()) return;
      services_.connection.requireConnection([this] { services_.session.login(ToneSession::LoginIntent::browse); });
    };
    // Right above the faceplate: under a tuner, Settings and the overlay.
    addChildComponent(*browser_, getIndexOfChildComponent(&faceplate_) + 1);
  } else {
    browser_.reset();
  }
  syncTakeovers();
  resized();
}

void PluginRoot::setLibraryShown(bool shown) {
  if (shown == libraryShown()) return;
  services_.library.setShown(shown);
  if (shown) {
    library_ = std::make_unique<LibraryDrawer>(services_);
    library_->onClose = [this] {
      // Deferred: the close click comes from a button inside the drawer.
      juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginRoot>(this)] {
        if (safe != nullptr) safe->setLibraryShown(false);
      });
    };
    // Above the chain and faceplate, below the takeovers and the overlay.
    addChildComponent(*library_, getIndexOfChildComponent(&faceplate_) + 1);
  } else {
    services_.library.cancelAdd();
    library_.reset();
  }
  header_.setLibraryShown(shown);
  syncTakeovers();
  resized();
}

// What a takeover covers is hidden, not left painting underneath: the meters
// tick at 30 Hz and would otherwise repaint for nothing.
void PluginRoot::syncTakeovers() {
  const bool tuner = tunerShown(), browser = browserShown(), signIn = signInShown();
  const bool column = browser || signIn;  // something covers the whole column
  main_.setVisible(!tuner && !column);
  faceplate_.setVisible(tuner || !column);
  if (browser_) browser_->setVisible(!tuner && !signIn);
  if (signIn_) signIn_->setVisible(!tuner);
  if (library_) library_->setVisible(!tuner && !column);
}

void PluginRoot::closeTunerThen(const std::function<void()>& fn) {
  setTunerShown(false);
  if (fn) fn();
}

void PluginRoot::showChainThen(const std::function<void()>& fn) {
  setTunerShown(false);
  if (browserShown()) {
    services_.loadFlow.clearPendingTargets();
    setBrowserShown(false);
  }
  main_.chainScreen().returnToGallery();
  if (fn) fn();
}

void PluginRoot::logout() {
  services_.loadFlow.clearPendingTargets();
  setBrowserShown(false);
  services_.session.logout();
}

// Layout
void PluginRoot::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void PluginRoot::openSettings(SettingsScreen::Tab tab) {
  if (settings_ != nullptr) {
    settings_->setTab(tab);
    return;
  }
  settings_ = std::make_unique<SettingsScreen>(services_, tab);
  settings_->onClose = [this] { closeSettings(); };
  settings_->setBounds(getLocalBounds());
  // Above the content column, below the overlay layer.
  addChildComponent(*settings_);
  settings_->toBehind(&overlay_);
  settings_->setVisible(true);
  services_.hints.setHover({});
}

void PluginRoot::closeSettings() {
  if (settings_ == nullptr) return;
  // Deferred: the close click comes from a button inside the screen.
  juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginRoot>(this)] {
    if (safe != nullptr) safe->settings_.reset();
  });
}

void PluginRoot::resized() {
  overlay_.setBounds(getLocalBounds());
  if (settings_ != nullptr) settings_->setBounds(getLocalBounds());
  for (auto* modal : {static_cast<ModalLayer*>(updateNotice_.get()), static_cast<ModalLayer*>(connectionModal_.get())})
    if (modal != nullptr) modal->setBounds(getLocalBounds());

  // The banner strip, then the content column at its full height; while
  // the window has the strip's space but the banner isn't shown yet, the
  // gap at the bottom is black on black.
  const int slotH = bannerPhase_ == BannerPhase::shown ? AppBanner::kHeight : 0;
  banner_.setBounds(0, 0, design::kWidth, AppBanner::kHeight);
  const int hintH = hintsVisible_ ? design::kHintHeight : 0;
  auto column = juce::Rectangle<int>(0, slotH, design::kWidth, design::kHeight + hintH);
  if (hintsVisible_) hintBar_.setBounds(column.removeFromBottom(hintH));
  header_.setBounds(column.removeFromTop(PluginHeader::kHeight));
  if (browser_) browser_->setBounds(column);  // the rest, faceplate included
  if (signIn_) signIn_->setBounds(column);
  // The drawer spans the column's height at its left. Beside it, the chain
  // band and the faceplate keep their full-width layout and are scaled down
  // together to fit, laid out taller so they fill the height: their parts
  // have fixed footprints (the 800px block card, the plate's knob groups),
  // so squeezing them would overlap them, and covering them would hide them.
  // Every tile stays a drop target.
  const bool drawer = library_ != nullptr && library_->isVisible();
  if (library_) library_->setBounds(column.withWidth(LibraryDrawer::kWidth));
  auto band = column;
  const auto plate = band.removeFromBottom(Faceplate::kHeight);
  if (drawer) {
    const auto beside = column.withTrimmedLeft(LibraryDrawer::kWidth);
    const float scale = beside.getWidth() / static_cast<float>(design::kWidth);
    const int innerH = juce::roundToInt(beside.getHeight() / scale);
    const auto fit = juce::AffineTransform::scale(scale).translated(static_cast<float>(beside.getX()),
                                                                   static_cast<float>(beside.getY()));
    main_.setTransform(fit);
    faceplate_.setTransform(fit);
    main_.setBounds(0, 0, design::kWidth, innerH - Faceplate::kHeight);
    faceplate_.setBounds(0, innerH - Faceplate::kHeight, design::kWidth, Faceplate::kHeight);
  } else {
    main_.setTransform({});
    faceplate_.setTransform({});
    faceplate_.setBounds(plate);
    main_.setBounds(band);
  }
  if (tuner_) tuner_->setBounds(band);  // the drawer steps aside for the tuner

  // The toast floats above the faceplate, measured from the overlay's bottom.
  const int belowColumn = getHeight() - (slotH + design::kHeight + hintH);
  toast_.setBottomOffset(belowColumn + design::kPlateHeight + hintH + kToastGap);
}

}  // namespace t3k::ui
