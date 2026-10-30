#include "NativeEditor.h"

#include <cmath>

#include "core/Fonts.h"

namespace t3k::ui {

namespace {

// Bring-up trace lines share one prefix so a user's log can be grepped for
// the editor's stages next to the "[Processor]" / "[Restore]" ones.
void trace(const juce::String& message) { juce::Logger::writeToLog("[Editor] " + message); }

juce::String formatScale(double scale) { return juce::String(scale, 2) + "x"; }

// "Direct2D" / "Software Renderer" on Windows; the index is opaque without
// the peer's own names, so fall back to it only when the list is empty.
juce::String rendererName(const juce::ComponentPeer& peer) {
  const auto engines = const_cast<juce::ComponentPeer&>(peer).getAvailableRenderingEngines();
  const int index = peer.getCurrentRenderingEngine();
  if (juce::isPositiveAndBelow(index, engines.size())) return engines[index];
  return "engine " + juce::String(index) + " of " + juce::String(engines.size());
}

}  // namespace

NativeEditor::Trace::Trace(const TONE3000Processor& processor) {
  // Everything a Windows-10-vs-11 / host-specific report needs in one line:
  // OS, JUCE, plugin format + host, and the display scale the peer will be
  // created at (DPI is the usual suspect when only one machine crashes).
  juce::String line = "Constructing: " + juce::SystemStats::getOperatingSystemName() +
                      (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : " (32-bit)") +
                      " | JUCE " + juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf("v", false, false) +
                      " | " + juce::AudioProcessor::getWrapperTypeDescription(processor.wrapperType) +
                      " in " + juce::String(juce::PluginHostType().getHostDescription());
  const auto& displays = juce::Desktop::getInstance().getDisplays();
  if (const auto* display = displays.getPrimaryDisplay()) {
    line << " | display " << juce::roundToInt(display->logicalBounds.getWidth()) << "x"
         << juce::roundToInt(display->logicalBounds.getHeight()) << " @ " << formatScale(display->scale)
         << " (dpi " << juce::roundToInt(display->dpi) << ")";
  } else {
    line << " | display: none";
  }
  line << " | displays " << displays.displays.size()
       << " | globalScale " << formatScale(juce::Desktop::getInstance().getGlobalScaleFactor());
  trace(line);
}

NativeEditor::FontsReady::FontsReady() {
  // Resolving these touches the OS font system (DirectWrite on Windows) and
  // decodes the embedded faces; if that is where a machine dies, the log
  // stops between "Constructing" and this line.
  const auto sans = Fonts::sans(14.0f);
  const auto mono = Fonts::mono(14.0f);
  trace("Fonts ready: sans=" + sans.getTypefaceName() +
        (sans.getTypefacePtr() != nullptr ? "" : " (NULL TYPEFACE)") + " | mono=" +
        mono.getTypefaceName() + (mono.getTypefacePtr() != nullptr ? "" : " (NULL TYPEFACE)"));
}

void NativeEditor::logPeerAttached() {
  // Once per native window: hosts can re-parent the editor (and the
  // standalone flips its title bar, see parentHierarchyChanged), each of
  // which may hand us a new peer with a different renderer or scale.
  auto* peer = getPeer();
  if (peer == nullptr || peer == loggedPeer_) return;
  const bool reattached = loggedPeer_ != nullptr;
  loggedPeer_ = peer;
  trace(juce::String(reattached ? "Peer re-attached" : "Peer attached") +
        ": renderer=" + rendererName(*peer) +
        " | platformScale " + formatScale(peer->getPlatformScaleFactor()) +
        " | editor " + juce::String(getWidth()) + "x" + juce::String(getHeight()) +
        " | peerBounds " + peer->getBounds().toString());
}

NativeEditor::NativeEditor(TONE3000Processor& owner)
    : AudioProcessorEditor(&owner),
      trace_(owner),
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
#if !JUCE_IOS
  root_.setCaptureAvailable(StandaloneAudioSettings::isAvailable());
#endif
  root_.onCaptureGear = [this] {
    if (!StandaloneAudioSettings::isAvailable()) return;
    if (!captureWindow_) {
      captureWindow_ = std::make_unique<t3k::capture::CaptureWindow>(this, [this] {
        captureWindow_->setVisible(false);
        root_.openSettings(SettingsScreen::Tab::system);
        toFront(true);
      });
    }
    captureWindow_->setVisible(true);
    captureWindow_->toFront(true);
  };
  // Nothing polls or repaints while the window is minimised or the editor
  // hidden (isShowing covers both); visibilityChanged /
  // parentHierarchyChanged wake the clock when it is back.
  services_.clock.visible = [this] { return isShowing(); };
  // extraContentHeight_ is already set: the root reported its chrome from
  // its constructor (see setExtraContentHeight).

#if JUCE_IOS
  // iOS gets one fixed, full-screen window: no corner drags, no host resize
  // request, no persisted size. The kiosk window hands us its bounds and the
  // root fits into them (see fitRoot): the 4:3 screen stretches the column
  // by what it has over the design box.
  setSize(design::kWidth, minDesignHeight());
  setResizable(false, false);
#else
  setResizable(true, true);
  // Read the persisted shape before installing the constrainer: it snaps the
  // editor to the 1x minimum, and resized() writes that back through the
  // processor. Limits first: setConstrainer reads them to decide whether the
  // host may resize us at all.
  const auto start = startFit();
  bounds_.setMinDesignHeight(minDesignHeight());
  bounds_.updateLimitsFor(design::kWidth);
  setConstrainer(&bounds_);
  applyScaledSize(start.scale, start.stretch);
#endif
  // Separates a crash inside PluginRoot's construction (log stops at "Fonts
  // ready") from one in the host's window attach (stops here).
  trace("Constructed: " + juce::String(getWidth()) + "x" + juce::String(getHeight()));
}

NativeEditor::~NativeEditor() { captureWindow_.reset(); stopTimer(); }

design::Fit NativeEditor::startFit() const {
  // The persisted shape, cut down to the room the window may open into. In a
  // DAW the host owns the plugin window, so only the design's own range
  // applies. The standalone fits the primary display's usable area, with
  // headroom for the native title bar (GitHub issue #43; Mutter's 2x title
  // bar is 37 logical px). The scale floors at 1x: the design box is the
  // hard minimum.
  const int minH = minDesignHeight();
  int roomW = juce::roundToInt(design::kWidth * design::kMaxScale);
  int roomH = juce::roundToInt((minH + design::kMaxStretch) * design::kMaxScale);
  if (StandaloneAudioSettings::isAvailable()) {
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
      constexpr int titleBarAllowance = 40;
      const auto usable = display->userBounds.toNearestInt();
      roomW = juce::jmin(roomW, usable.getWidth());
      roomH = juce::jmin(roomH, usable.getHeight() - titleBarAllowance);
    }
  }
  const double maxScale = juce::jmax(1.0, juce::jmin(roomW / static_cast<double>(design::kWidth),
                                                     roomH / static_cast<double>(minH)));
  const double scale = juce::jlimit(1.0, maxScale, processor_.editorScale.load());
  const int maxStretch =
      juce::jlimit(0, design::kMaxStretch, static_cast<int>(std::floor(roomH / scale)) - minH);
  return {scale, juce::jlimit(0, maxStretch, processor_.editorStretch.load())};
}

void NativeEditor::applyScaledSize(double scale, int stretch) {
  setSize(juce::roundToInt(design::kWidth * scale), juce::roundToInt((minDesignHeight() + stretch) * scale));
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
#if JUCE_IOS
  // The window is the screen; it cannot grow. The column gives up the
  // strip's height from its stretch, or the box shrinks to fit.
  fitRoot();
#else
  // Keep the scale and the stretch, grow by the strip. setSize() reaches the
  // host as a resize request through the plugin wrapper (resizeView in
  // VST3); a host that refuses keeps the old size, and fitRoot takes the
  // strip out of the stretch, or letterboxes once that is spent. The
  // constrainer's own limits update in resized(); its minimum follows the
  // design box, so a drag can't shrink us under the new chrome either way.
  bounds_.setMinDesignHeight(minDesignHeight());
  applyScaledSize(currentScale(), root_.stretch());
  fitRoot();
#endif
}

void NativeEditor::fitRoot() {
  auto fit = design::fit(getWidth(), getHeight(), minDesignHeight());
  if (fit.scale < currentScale() - 1e-6) {
    // The box is taller than the window allows. Hold the width-driven scale
    // while a resize may still land (bottom strip clips, black on black),
    // then fit.
    const auto now = juce::Time::currentTimeMillis();
    if (now < shrinkAllowedAtMs_) {
      startTimer(static_cast<int>(shrinkAllowedAtMs_ - now) + 1);
      fit = {currentScale(), 0};
    } else {
      stopTimer();
    }
  } else {
    stopTimer();
  }
  root_.setStretch(fit.stretch);
  root_.setTransform(juce::AffineTransform::scale(static_cast<float>(fit.scale)));
  services_.zoom.set(fit.scale);
  // Top-anchored, horizontally centred: the stretch fills the height, and a
  // letterboxed box leaves its black at the sides and the bottom.
  root_.setTopLeftPosition(juce::roundToInt((getWidth() - design::kWidth * fit.scale) / 2), 0);
}

void NativeEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void NativeEditor::paintOverChildren(juce::Graphics& g) {
  // Not paint(): the opaque root covers us, so JUCE clips our own paint to
  // nothing and never calls it. This runs after the whole tree has painted
  // once, which is the marker we want.
  if (loggedFirstPaint_) return;
  loggedFirstPaint_ = true;
  // The peer exists by now even if parentHierarchyChanged never saw it.
  logPeerAttached();
  trace("First paint: " + juce::String(getWidth()) + "x" + juce::String(getHeight()) + " | clip " +
        g.getClipBounds().toString());
}

void NativeEditor::resized() {
  if (!loggedFirstResize_) {
    loggedFirstResize_ = true;
    trace("First resized: " + juce::String(getWidth()) + "x" + juce::String(getHeight()) +
          " (scale " + formatScale(currentScale()) + ", extraHeight " +
          juce::String(extraContentHeight_) + ")");
  }
  fitRoot();
#if !JUCE_IOS
  // The height range hosts read follows the width we now have.
  bounds_.updateLimitsFor(getWidth());
  // Persist the user's (or host's) chosen shape; skip while correcting our
  // own size. No chosen shape exists on iOS (the window is the screen).
  if (!restoringSize_) {
    processor_.editorScale.store(currentScale());
    processor_.editorStretch.store(root_.stretch());
  }
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
  logPeerAttached();
#if !JUCE_IOS
  if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    // Flipping the native title bar on relayouts the window's content split
    // and can briefly mis-size us; re-assert our exact size so JUCE's own
    // resize listener grows the window to contain us again, and don't let
    // that correction clobber the persisted scale.
    const int w = getWidth();
    const int h = getHeight();
    restoringSize_ = true;
    window->setUsingNativeTitleBar(true);
    // Plugin wrappers read usesWindowsMultiTouch(); the standalone's window
    // owns the peer instead, so opt it in here (see NativeEditor.h).
    window->setUsingWindowsMultiTouch(true);
    setSize(w, h);
    juce::Component::SafePointer<NativeEditor> self(this);
    juce::MessageManager::callAsync([self] {
      if (self != nullptr) self->restoringSize_ = false;
    });
  }
#endif
}

}  // namespace t3k::ui
