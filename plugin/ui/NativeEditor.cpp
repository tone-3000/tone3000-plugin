#include "NativeEditor.h"

namespace t3k::ui {

NativeEditor::NativeEditor(TONE3000Processor& owner)
    : AudioProcessorEditor(&owner),
      processor_(owner),
      backend_(owner, *this),
      prefs_(&prefsFile_->file, &prefsFile_->lock),
      session_(backend_, prefs_, http_, Tone3000Session::Config::fromBuild()),
      services_(backend_, session_, *this, prefs_),
      root_(services_) {
  // Dark-theme every JUCE-drawn surface outside the root (standalone audio
  // settings dialog etc.). SharedResourcePointer keeps one instance across
  // plugin instances in the same process.
  juce::LookAndFeel::setDefaultLookAndFeel(&darkLookAndFeel_.get());
  setOpaque(true);
  addAndMakeVisible(root_);
  // Nothing polls or repaints while the window is minimised or the editor
  // hidden (isShowing covers both); visibilityChanged /
  // parentHierarchyChanged wake the clock when it is back.
  services_.clock.visible = [this] { return isShowing(); };
  // extraContentHeight_ is already set: the root reported its chrome from
  // its constructor (see setExtraContentHeight).

#if JUCE_IOS || JUCE_ANDROID
  // iOS and Android get one fixed, full-screen window: no corner drags, no
  // host resize request, no persisted scale. The kiosk window hands us its
  // bounds and the root letterboxes into them (see fitRoot); an aspect
  // constrainer would resolve the 4:3 screen by height and clip a third of
  // the UI. Android keeps this design-size guess until parentHierarchyChanged
  // corrects it.
  setSize(design::kWidth, designHeight());
  setResizable(false, false);
#else
  setResizable(true, true);
  // Read the persisted scale before touching the constraints: installing the
  // resize limits already snaps the editor to the 1x minimum, and resized()
  // writes that back through processor.editorScale.
  const double savedScale = juce::jlimit(1.0, maxStartScale(), processor_.editorScale.load());
  updateResizeConstraints();
  applyScaledSize(savedScale);
#endif
}

NativeEditor::~NativeEditor() { stopTimer(); }

double NativeEditor::maxStartScale() const {
  // In a DAW the host owns the plugin window, so the persisted scale is
  // restored as-is there.
  if (!StandaloneAudioSettings::isAvailable()) return design::kMaxScale;
  const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  if (display == nullptr) return design::kMaxScale;
  // Fit the standalone window into the primary display's usable area, with
  // headroom for the native title bar (GitHub issue #43; Mutter's 2x title
  // bar is 37 logical px). Floor at 1.0: the design box is the hard minimum.
  constexpr int titleBarAllowance = 40;
  const double fitW = display->userBounds.getWidth() / static_cast<double>(design::kWidth);
  const double fitH =
      (display->userBounds.getHeight() - titleBarAllowance) / static_cast<double>(designHeight());
  return juce::jlimit(1.0, design::kMaxScale, juce::jmin(fitW, fitH));
}

void NativeEditor::applyScaledSize(double scale) {
  setSize(juce::roundToInt(design::kWidth * scale), juce::roundToInt(designHeight() * scale));
}

void NativeEditor::updateResizeConstraints() {
  setResizeLimits(design::kWidth, designHeight(), juce::roundToInt(design::kWidth * design::kMaxScale),
                  juce::roundToInt(designHeight() * design::kMaxScale));
  // The ratio tracks the chrome strips, so corner drags at any extra-height
  // state preserve the current layout exactly.
  getConstrainer()->setFixedAspectRatio(static_cast<double>(design::kWidth) / designHeight());
}

void NativeEditor::setExtraContentHeight(int total, int persistent) {
  const int clamped = juce::jlimit(0, kMaxExtraHeight, total);
  // Remember the session-persistent portion (the hint bar; the banner is
  // dynamic) even when the window size itself doesn't change, so the next
  // editor opens pre-sized for the chrome the UI will render on first paint.
  processor_.editorExtraHeight.store(juce::jlimit(0, kMaxExtraHeight, persistent));
  if (clamped == extraContentHeight_) return;
  const bool grew = clamped > extraContentHeight_;
  extraContentHeight_ = clamped;
  // Before the constructor has sized us there is nothing to resize yet.
  if (getWidth() == 0) return;

  // A taller box keeps the current scale until the (possibly async or
  // refused) host resize lands, or the grace period ends.
  shrinkAllowedAtMs_ = grew ? juce::Time::currentTimeMillis() + kShrinkGraceMs : 0;
#if JUCE_IOS || JUCE_ANDROID
  // The window is the screen; it cannot grow. Shrink the box to fit.
  fitRoot();
#else
  // setSize() reaches the host as a resize request through the plugin
  // wrapper (resizeView in VST3); a host that refuses keeps the old size and
  // fitRoot letterboxes instead. Constrainer values are set directly rather
  // than via setResizeLimits(), which re-applies to the *current* bounds and
  // can snap the width for a frame mid-change.
  const double scale = currentScale();
  if (auto* c = getConstrainer()) {
    c->setSizeLimits(design::kWidth, designHeight(), juce::roundToInt(design::kWidth * design::kMaxScale),
                     juce::roundToInt(designHeight() * design::kMaxScale));
    c->setFixedAspectRatio(static_cast<double>(design::kWidth) / designHeight());
  }
  applyScaledSize(scale);
  fitRoot();
#endif
}

void NativeEditor::fitRoot() {
  const double byWidth = getWidth() / static_cast<double>(design::kWidth);
  const double byHeight = getHeight() / static_cast<double>(designHeight());
  double scale = byWidth;
  if (byHeight < byWidth - 1e-6) {
    // The box is taller than the window allows. Hold the width-driven scale
    // while a resize may still land (bottom strip clips, black on black),
    // then fit.
    const auto now = juce::Time::currentTimeMillis();
    if (now < shrinkAllowedAtMs_) {
      startTimer(static_cast<int>(shrinkAllowedAtMs_ - now) + 1);
    } else {
      stopTimer();
      scale = byHeight;
    }
  } else {
    stopTimer();
  }
  scale = juce::jmax(0.05, scale);
  root_.setTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
  services_.zoom.set(scale);
  // Top-anchored, horizontally centred. iOS and Android centre vertically
  // too: their window never resizes, so nothing can jump.
  const int x = juce::roundToInt((getWidth() - design::kWidth * scale) / 2);
#if JUCE_IOS || JUCE_ANDROID
  const int y = juce::jmax(0, juce::roundToInt((getHeight() - designHeight() * scale) / 2));
#else
  const int y = 0;
#endif
  root_.setTopLeftPosition(x, y);
}

void NativeEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void NativeEditor::resized() {
  fitRoot();
  // Persist the user's (or host's) chosen scale; skip while correcting our
  // own size. No chosen scale exists on iOS or Android (the window is the
  // screen).
#if !(JUCE_IOS || JUCE_ANDROID)
  if (!restoringSize_) processor_.editorScale.store(currentScale());
#endif
}

// Key presses reach the editor when no control took them (the peer falls
// back to its component; children pass unused keys up). Space and Enter
// are the host's transport keys, so they go back to it (keyPassthrough.ts).
// Since clicks never focus buttons (Clickable), that is the state after any
// mouse work; only a text field, or a control the user Tabbed to, takes
// them for itself (PluginRoot's focus policy).
bool NativeEditor::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::spaceKey) return backend_.forwardKeyToHost(Backend::HostKey::space);
  if (key == juce::KeyPress::returnKey) return backend_.forwardKeyToHost(Backend::HostKey::enter);
  return false;
}

void NativeEditor::visibilityChanged() { services_.clock.wake(); }

void NativeEditor::parentHierarchyChanged() {
  services_.clock.wake();
#if JUCE_ANDROID
  // Unlike UIKit, Android's peer keeps whatever size we ask for, so the
  // constructor's design-size guess would stick. The displays aren't known
  // at construction time but are once we're parented; the window's own
  // bounds aren't reliable yet (they can still be a placeholder).
  if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    setSize(juce::roundToInt(display->userBounds.getWidth()),
            juce::roundToInt(display->userBounds.getHeight()));
#endif
#if !(JUCE_IOS || JUCE_ANDROID)
  if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    // Flipping the native title bar on relayouts the window's content split
    // and can briefly mis-size us; re-assert our exact size so JUCE's own
    // resize listener grows the window to contain us again, and don't let
    // that correction clobber the persisted scale.
    const int w = getWidth();
    const int h = getHeight();
    restoringSize_ = true;
    window->setUsingNativeTitleBar(true);
    setSize(w, h);
    juce::Component::SafePointer<NativeEditor> self(this);
    juce::MessageManager::callAsync([self] {
      if (self != nullptr) self->restoringSize_ = false;
    });
  }
#endif
}

}  // namespace t3k::ui
