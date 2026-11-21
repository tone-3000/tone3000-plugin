#include "PluginRoot.h"

#include "core/Design.h"
#include "core/Help.h"
#include "core/NoDefaultFocus.h"
#include "core/Theme.h"
#include "widgets/AccessibleGroup.h"
#include "widgets/SecondaryPress.h"
#include "views/block/BlockDetail.h"
#include "views/gallery/GalleryTile.h"

namespace t3k::ui {

namespace {
// Faceplate.tsx PLATE_HEIGHT; the toast floats 24px above it.
constexpr int kToastGap = 24;

// The first descendant of `root` of type T that `match` accepts.
template <typename T, typename Match>
T* findDescendant(juce::Component& root, Match&& match) {
  for (auto* child : root.getChildren()) {
    if (auto* found = dynamic_cast<T*>(child); found != nullptr && match(*found)) return found;
    if (auto* found = findDescendant<T>(*child, match)) return found;
  }
  return nullptr;
}

template <typename T>
T* findDescendant(juce::Component& root) {
  return findDescendant<T>(root, [](T&) { return true; });
}

bool canTakeFocus(const juce::Component* c) {
  return c != nullptr && c->isShowing() && c->isEnabled() && c->getWantsKeyboardFocus();
}
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
  // Read when a screen reader enters the plugin; Settings can turn the keys off.
  setDescription("Press greater than or less than to move to the next or previous group of controls.");

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
  a11y::makeGroup(banner_, "Alert");
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

  services_.hints.addListener(this);
  services_.banners.addListener(this);
  services_.connection.addListener(this);
  services_.updates.addListener(this);
  services_.session.addListener(this);
  services_.backend.addListener(this);
  hintsVisible_ = services_.hints.enabled();
  updateChromeHeight();
  bannerChanged();
  connectionProblemChanged();
  updateNoticeChanged();
  authFlowChanged();
  sessionChanged();
  // Queue after the initial store notifications so the sample is present
  // when the host first hands keyboard focus to this editor.
  if (juce::SystemStats::getEnvironmentVariable("T3K_DIALOG_PREVIEW", "") == "1"
      && juce::SystemStats::getEnvironmentVariable("T3K_DIALOG_PREVIEW_ON_OPEN", "") == "1") {
    juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
      if (self != nullptr) self->previewDialog(0);
    });
  }
}

PluginRoot::~PluginRoot() {
  if (keyWindow_ != nullptr) keyWindow_->removeKeyListener(&focusPolicy_);
  services_.session.onAuthenticated = nullptr;
  services_.loadFlow.onShowBrowser = nullptr;
  if (watchedParent_ != nullptr) watchedParent_->removeComponentListener(this);
  services_.backend.removeListener(this);
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
    g.setOrigin(child->getPosition());
    child->paintEntireComponent(g, true);
    g.restoreState();
  }
  return image;
}

template <typename Modal, typename... Args>
std::unique_ptr<Modal> PluginRoot::openModal(Args&&... args) {
  if (frontModal() == nullptr && modalReturnFocus_ == nullptr) {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused != nullptr && isParentOf(focused)) modalReturnFocus_ = focused;
  }
  auto modal = std::make_unique<Modal>([this](float scale) { return snapshotBeneathOverlay(scale); },
                                       std::forward<Args>(args)...);
  modal->setBounds(getLocalBounds());
  overlay_.addAndMakeVisible(*modal);
  return modal;
}

void PluginRoot::restackModals() {
  if (updateNotice_) updateNotice_->toFront(false);
  if (connectionModal_) connectionModal_->toFront(false);
  auto* top = frontModal();
  for (auto* dialog : {static_cast<ModalLayer*>(updateNotice_.get()), static_cast<ModalLayer*>(connectionModal_.get())})
    if (dialog != nullptr && dialog != top) dialog->cancelPendingFocus();
  if (top != nullptr) {
    if (focusedModal_ != top) {
      focusedModal_ = top;
      top->focusFirstControl();
    }
  } else {
    focusedModal_ = nullptr;
    if (modalReturnFocus_ != nullptr && modalReturnFocus_->isShowing() && modalReturnFocus_->isEnabled())
      modalReturnFocus_->grabKeyboardFocus();
    modalReturnFocus_ = nullptr;
  }
}

ModalLayer* PluginRoot::frontModal() const {
  if (connectionModal_) return connectionModal_.get();
  return updateNotice_.get();
}

// Opt-in local QA path: no version endpoint, download, or saved snooze.
void PluginRoot::previewDialog(int kind) {
  if (frontModal() != nullptr) return;
  const auto close = [self = juce::Component::SafePointer(this)] {
    juce::MessageManager::callAsync([self] {
      if (self == nullptr) return;
      self->updateNotice_.reset();
      self->connectionModal_.reset();
      self->restackModals();
    });
  };
  if (kind == 0) {
    auto modal = openModal<UpdateNotice>(UpdateInfo{"0.0.0-test",
        "<p>This is an accessibility test. No update is required. Tab through the buttons, then press Escape to close.</p>", ""});
    modal->onRemindLater = [close](int) { close(); };
    updateNotice_ = std::move(modal);
  } else {
    auto modal = openModal<ConnectionModal>(kind == 1 ? ConnectionGate::Problem::offline : ConnectionGate::Problem::insecure, false);
    modal->onDismiss = close;
    modal->onRetry = close;
    connectionModal_ = std::move(modal);
  }
  restackModals();
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
      auto* focused = getCurrentlyFocusedComponent();
      self->signInOpener_ = focused != nullptr && self->isParentOf(focused) ? focused : nullptr;
      self->announceSignIn_ = !self->services_.session.authenticated();
      self->signIn_ = std::make_unique<SignInScreen>(self->services_);
      // Right above the browser when there is one (it is where a browse
      // sign-in returns to), else above the faceplate; under a tuner,
      // Settings and the overlay.
      juce::Component* below = self->browser_ ? static_cast<juce::Component*>(self->browser_.get())
                                              : static_cast<juce::Component*>(&self->faceplate_);
      self->addChildComponent(*self->signIn_, self->getIndexOfChildComponent(below) + 1);
      self->services_.hints.setHover({});
    } else {
      auto* focused = getCurrentlyFocusedComponent();
      const bool focusInside =
          focused != nullptr && (focused == self->signIn_.get() || self->signIn_->isParentOf(focused));
      if (self->services_.session.authenticated()) self->sessionChanged();  // announces the landing
      // Focus out of the page before it goes: back to what opened it, else
      // the account button, which now reads as signed in.
      auto* account = findDescendant<AccountMenu>(self->header_);
      if (focusInside) {
        if (canTakeFocus(self->signInOpener_)) self->signInOpener_->grabKeyboardFocus();
        else if (canTakeFocus(account)) account->grabKeyboardFocus();
      }
      self->signIn_.reset();
      self->syncTakeovers();
      self->resized();
      // An opener under the page (the browser's sign-in prompt) shows again
      // only now.
      if (focusInside && !self->hasKeyboardFocus(true))
        focusOpenerOr(self->signInOpener_, self->browser_ ? static_cast<juce::Component*>(self->browser_.get())
                                                          : static_cast<juce::Component*>(&self->main_));
      self->signInOpener_ = nullptr;
      return;
    }
    self->syncTakeovers();
    self->resized();
    // Into the page as it opens, when the keyboard was in the plugin.
    if (self->signInOpener_ != nullptr && self->signIn_ != nullptr) self->signIn_->focusFirstControl();
  });
}

void PluginRoot::sessionChanged() {
  const auto& session = services_.session;
  const auto user = session.user();
  const juce::String name = user ? user->name() : juce::String();
  if (auto* account = findDescendant<AccountMenu>(header_)) account->setDisplayName(name);
  if (session.authenticated() && announceSignIn_) {
    announceSignIn_ = false;
    help::announce(name.isNotEmpty() ? "Signed in as " + name : juce::String("Signed in"));
  }
}

void PluginRoot::focusOpenerOr(juce::Component* opener, juce::Component* fallback) {
  if (canTakeFocus(opener)) {
    opener->grabKeyboardFocus();
    return;
  }
  if (fallback == nullptr || !fallback->isShowing()) return;
  const auto order = juce::KeyboardFocusTraverser().getAllComponents(fallback);
  if (!order.empty()) order.front()->grabKeyboardFocus();
}

// Store changes arrive from inside a modal's own button handler, so the
// modal is rebuilt on the next message-loop turn rather than under its feet.
void PluginRoot::connectionProblemChanged() {
  juce::MessageManager::callAsync([self = juce::Component::SafePointer(this)] {
    if (self == nullptr) return;
    const auto& problem = self->services_.connection.problem();
    self->connectionModal_.reset();
    if (!problem) { self->restackModals(); return; }
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
    if (!notice) { self->restackModals(); return; }
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
  applyDesignSize();
  services_.shell.setExtraContentHeight(bannerExtra + hintExtra, hintExtra);
}

void PluginRoot::setStretch(int px) {
  px = juce::jlimit(0, design::kMaxStretch, px);
  if (px == stretch_) return;
  stretch_ = px;
  applyDesignSize();
}

void PluginRoot::applyDesignSize() {
  const int hintExtra = hintsVisible_ ? design::kHintHeight : 0;
  const int bannerExtra = bannerPhase_ != BannerPhase::hidden ? AppBanner::kHeight : 0;
  setSize(design::kWidth, design::kHeight + stretch_ + bannerExtra + hintExtra);
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
        describeBanner(*active);
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
        describeBanner(*active);
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

void PluginRoot::describeBanner(const BannerSpec& spec) {
  juce::String text;
  for (const auto& run : spec.content) text << run.text;
  banner_.setDescription(text);
  // Its buttons keep their visible labels as names ("Open Settings",
  // "Ignore") and carry the alert as context.
  for (auto* child : banner_.getChildren())
    if (auto* button = dynamic_cast<juce::Button*>(child)) button->setDescription(text);
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
  if ((key.isKeyCode(juce::KeyPress::F10Key) && key.getModifiers().isShiftDown())
#if JUCE_WINDOWS
      || key.isKeyCode(kWindowsApplicationsKey)
#endif
      ) {
    if (root_.frontModal() != nullptr) return false;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    for (auto* c = focused; c != nullptr && root_.isParentOf(c); c = c->getParentComponent()) {
      if (auto* target = dynamic_cast<SecondaryPressTarget*>(c)) {
        target->keyboardContextMenu();
        return true;
      }
      if (auto* tile = dynamic_cast<GalleryTile*>(c))
        return tile->keyPressed(juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0));
    }
  }
  if (key.getModifiers().isCtrlDown() && key.getModifiers().isAltDown() && key.getModifiers().isShiftDown()
      && juce::SystemStats::getEnvironmentVariable("T3K_DIALOG_PREVIEW", "") == "1") {
    if (key.isKeyCode('U')) { root_.previewDialog(0); return true; }
    if (key.isKeyCode('O')) { root_.previewDialog(1); return true; }
    if (key.isKeyCode('S')) { root_.previewDialog(2); return true; }
  }
  if (auto* modal = root_.frontModal()) {
    if (key == juce::KeyPress::escapeKey || key.isKeyCode(juce::KeyPress::tabKey))
      return modal->keyPressed(key);
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || !modal->isParentOf(focused)) {
      modal->focusFirstControl();
      return true; // never activate a control behind the dialog
    }
    return false;
  }
  // Focus resting on the window itself (a standalone DocumentWindow takes
  // it when the OS activates it) is nothing focused as far as the UI goes.
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused != nullptr && root_.isParentOf(focused)) return false;
  // Nothing focused: > enters the first group, < the last.
  if (root_.handleGroupKey(key)) return true;
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

bool PluginRoot::FocusPolicy::keyStateChanged(bool, juce::Component*) {
#if JUCE_WINDOWS
  // JUCE reports VK_APPS through key-state callbacks rather than keyPressed.
  const bool down = juce::KeyPress::isKeyCurrentlyDown(kWindowsApplicationsKey);
  const bool invoke = down && !applicationsKeyDown_;
  applicationsKeyDown_ = down;
  if (invoke)
    return keyPressed(juce::KeyPress(kWindowsApplicationsKey), nullptr);
#endif
  return false;
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

bool PluginRoot::handleGroupKey(const juce::KeyPress& key) {
  bool forward = true;
  if (!a11y::isGroupKey(key, forward)) return false;
  if (frontModal() != nullptr) return false;
  auto* focused = getCurrentlyFocusedComponent();
  if (focused != nullptr) {
    if (overlay_.isParentOf(focused)) return false;  // a menu or deck: its own keys
    for (auto* c = focused; c != nullptr; c = c->getParentComponent())
      if (dynamic_cast<juce::TextEditor*>(c) != nullptr) return false;  // typing
  }
  juce::Component* scope = settingsInFront();
  if (scope == nullptr) scope = this;
  auto* group = a11y::moveToGroup(*scope, forward);
  if (group == nullptr) return false;
#if JUCE_MAC
  // NVDA and Narrator name a group as focus enters it; VoiceOver reads only
  // the control, so say where it landed.
  help::announce(group->getTitle() + " group");
#endif
  return true;
}

bool PluginRoot::keyPressed(const juce::KeyPress& key) {
  if (handleGroupKey(key)) return true;
  if (key != juce::KeyPress::escapeKey) return false;
  auto* focused = getCurrentlyFocusedComponent();
  if (focused == nullptr || !isParentOf(focused)) return false;
  // A field, a menu or a dialog that let Escape through still only drops
  // focus, as before; elsewhere Escape is the screen's Back.
  bool typing = false;
  for (auto* c = focused; c != nullptr && c != this; c = c->getParentComponent())
    if (dynamic_cast<juce::TextEditor*>(c) != nullptr) typing = true;
  if (!typing && frontModal() == nullptr && !overlay_.isParentOf(focused) && closeFrontScreen()) return true;
  focused->giveAwayKeyboardFocus();
  return true;
}

bool PluginRoot::closeFrontScreen() {
  if (settings_ != nullptr) {
    closeSettings();  // returns focus to settingsOpener_
    return true;
  }
  if (tunerShown()) {
    setTunerShown(false);  // returns focus to the tuner button
    return true;
  }
  if (signInShown()) {
    signIn_->back();  // the page's back arrow; authFlowChanged returns focus to signInOpener_
    return true;
  }
  if (browserShown()) {
    // The browser's Close, posted: it tears down the browser the focused
    // control sits in. setBrowserShown returns focus to browserOpener_.
    juce::MessageManager::callAsync([self = juce::Component::SafePointer<PluginRoot>(this)] {
      if (self != nullptr && self->browser_ != nullptr && self->browser_->onClose) self->browser_->onClose();
    });
    return true;
  }
  if (!main_.isVisible()) return false;
  auto* detail = findDescendant<BlockDetail>(main_.chainScreen(), [](BlockDetail& d) { return d.isVisible(); });
  if (detail == nullptr || !detail->onBack) return false;
  // The card's back arrow, then focus back on the block's tile once the
  // card is gone (onBack closes it on the next message-loop turn; this
  // is posted after it).
  juce::String blockId;
  const auto saved = services_.prefs.session.find(UiPrefs::kDetailBlockId);
  if (saved != services_.prefs.session.end()) blockId = saved->second;
  detail->onBack();
  juce::MessageManager::callAsync([self = juce::Component::SafePointer<PluginRoot>(this), blockId] {
    if (self == nullptr || self->main_.hasKeyboardFocus(true)) return;
    const auto id = blockId.toStdString();
    auto* tile = findDescendant<GalleryTile>(self->main_, [&id](GalleryTile& t) { return t.blockId() == id; });
    focusOpenerOr(tile, &self->main_);
  });
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
  if (shown && tuner_->isShowing()) tuner_->grabKeyboardFocus();
  else if (!shown && header_.isShowing()) {
    header_.focusTunerButton();
  }
}

// The MIDI-mapped tuner control.
void PluginRoot::tunerToggleRequested() {
  // Settings covers the whole window: close it so the tuner shows.
  if (!tunerShown()) closeSettings();
  setTunerShown(!tunerShown());
}

void PluginRoot::setBrowserShown(bool shown) {
  if (shown == browserShown()) return;
  const bool transferFocus = hasKeyboardFocus(true);
  if (shown) {
    browserOpener_ = transferFocus ? getCurrentlyFocusedComponent() : nullptr;
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
  if (transferFocus) {
    juce::MessageManager::callAsync([self = juce::Component::SafePointer(this), shown] {
      if (self == nullptr || self->browserShown() != shown || self->hasKeyboardFocus(true)) return;
      // Into the browser as it opens; on close back to what opened it.
      if (shown) {
        focusOpenerOr(nullptr, self->browser_.get());
      } else {
        focusOpenerOr(self->browserOpener_, &self->main_);
        self->browserOpener_ = nullptr;
      }
    });
  }
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

  // Focus left behind the screen (the account button its menu hands focus
  // back to) would strand the keyboard: Tab walks only the Settings page,
  // and Escape would act on a control the screen covers. Move it to the
  // page's first control and remember the opener for the way back.
  auto* focused = getCurrentlyFocusedComponent();
  if (focused != nullptr && isParentOf(focused)) {
    settingsOpener_ = focused;
    const auto order = settings_->createKeyboardFocusTraverser()->getAllComponents(settings_.get());
    if (!order.empty()) order.front()->grabKeyboardFocus();
  }
}

void PluginRoot::closeSettings() {
  if (settings_ == nullptr) return;
  // Deferred: the close click comes from a button inside the screen.
  juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginRoot>(this)] {
    if (safe == nullptr || safe->settings_ == nullptr) return;
    auto* focused = getCurrentlyFocusedComponent();
    const bool focusInside = focused != nullptr && safe->settings_->isParentOf(focused);
    safe->settings_.reset();
    // Keyboard focus was on the page: return it to what opened Settings.
    if (focusInside && safe->settingsOpener_ != nullptr && safe->settingsOpener_->isShowing())
      safe->settingsOpener_->grabKeyboardFocus();
    safe->settingsOpener_ = nullptr;
  });
}

void PluginRoot::resized() {
  overlay_.setBounds(getLocalBounds());
  if (settings_ != nullptr) settings_->setBounds(getLocalBounds());
  for (auto* modal : {static_cast<ModalLayer*>(updateNotice_.get()), static_cast<ModalLayer*>(connectionModal_.get())})
    if (modal != nullptr) modal->setBounds(getLocalBounds());

  // The banner strip, then the content column at its full height (the core
  // plus the stretch); while the window has the strip's space but the
  // banner isn't shown yet, the gap at the bottom is black on black.
  const int slotH = bannerPhase_ == BannerPhase::shown ? AppBanner::kHeight : 0;
  banner_.setBounds(0, 0, design::kWidth, AppBanner::kHeight);
  const int hintH = hintsVisible_ ? design::kHintHeight : 0;
  const int coreH = design::kHeight + stretch_;
  auto column = juce::Rectangle<int>(0, slotH, design::kWidth, coreH + hintH);
  if (hintsVisible_) hintBar_.setBounds(column.removeFromBottom(hintH));
  header_.setBounds(column.removeFromTop(PluginHeader::kHeight));
  if (browser_) browser_->setBounds(column);  // the rest, faceplate included
  if (signIn_) signIn_->setBounds(column);
  faceplate_.setBounds(column.removeFromBottom(Faceplate::kHeight));
  main_.setBounds(column);
  if (tuner_) tuner_->setBounds(column);

  // The toast floats above the faceplate, measured from the overlay's bottom.
  const int belowColumn = getHeight() - (slotH + coreH + hintH);
  toast_.setBottomOffset(belowColumn + design::kPlateHeight + hintH + kToastGap);
}

}  // namespace t3k::ui
