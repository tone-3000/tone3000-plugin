#include "Editor.h"
#include "Processor.h"

void TONE3000Editor::parentHierarchyChanged() {
  // iOS runs the standalone window in kiosk mode: it is already exactly the
  // screen, has no title bar to flip on and cannot be resized, so the whole
  // size-preserving dance below has nothing to correct.
#if ! JUCE_IOS
  if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    // Snapshot our own size before flipping the title bar style: JUCE
    // immediately relayouts the title-bar/content split within the window's
    // current bounds, which can stretch or shrink us before our own
    // aspect-ratio constrainer catches up. A hardcoded title-bar-height
    // guess doesn't hold on every OS/version and can leave us a few px off.
    // Re-asserting our exact pre-toggle size instead lets JUCE's own resize
    // listener (StandaloneFilterWindow::MainContentComponent, which already
    // measures the real native frame) grow the *window* to exactly contain
    // us again, with no guesswork.
    const int w = getWidth();
    const int h = getHeight();
    // Whatever resize this dance causes along the way is us correcting
    // ourselves, not the user choosing a size; don't let it clobber the
    // persisted scale. Cleared next tick so a deferred cascade from the
    // relayout is covered too, not just a same-tick one.
    restoringSize = true;
    window->setUsingNativeTitleBar(true);
    setSize(w, h);
    juce::Component::SafePointer<TONE3000Editor> self(this);
    juce::MessageManager::callAsync([self] {
      if (self != nullptr)
        self->restoringSize = false;
    });

#if JUCE_WINDOWS
    // Native window resizes can leave white dead space around the UI: JUCE 9's
    // default Direct2D backend validates WM_PAINT regions immediately but
    // defers the actual draw to the next vblank, so a frame drag can validate
    // regions that never get painted at the final size (JUCE forum: "Resizing
    // is strangely broken on Windows"; the Jan 2026 isSizing() fix was
    // superseded and 9.0.1 still defers). This window only paints a black
    // backdrop behind the WebView2 child, which does its own rendering, so
    // the synchronous software renderer costs nothing and cannot present
    // stale bounds. Engine 0 = "Software Renderer" (GDI), 1 = "Direct2D";
    // no-op when already selected, and JUCE re-applies the choice itself when
    // the peer is recreated. On construction-time passes the window isn't on
    // the desktop yet (no peer); the post-creation hierarchy-changed pass
    // lands here again with the peer in place.
    if (auto* peer = window->getPeer())
      peer->setCurrentRenderingEngine(0);
#endif
  }
#endif  // ! JUCE_IOS

#if JUCE_MAC
  // DAW hosts own the plugin's NSWindow and usually leave mouse-moved events
  // off, which kills hover/cursor feedback in the WKWebView (clicks still
  // work). Re-apply on every reparent since hosts can recreate the window
  // when the editor is closed and reopened.
  if (auto* peer = getPeer()) {
    EditorWebViewSetup::enableHostWindowMouseMovedEvents(peer->getNativeHandle());
    // And keep hover alive while the host window is not key (mouse-moved
    // events normally stop the moment the user clicks a DAW control).
    EditorWebViewSetup::installHoverMouseForwarding(peer->getNativeHandle());
    // Same timing: the WKWebView flashes its system-grey background between
    // window creation and the page's first paint; make it draw none so the
    // black window shows through instead.
    EditorWebViewSetup::applyBlackWebViewBackground(peer->getNativeHandle());
    // Web Inspector + native context menu (Reload) follow the Diagnostics
    // toggle. Always apply, including the off state, so release builds
    // strip the stock WKWebView menu.
    EditorWebViewSetup::setWebInspectorEnabled(
        peer->getNativeHandle(), TONE3000Processor::readPersistedWebInspectorEnabled());
  }
#endif

  // Trigger the WebView load only once the editor has a real top-level
  // component (i.e. the NSWindow on macOS exists and is on-screen). In a DAW
  // the editor is parented to the host's window before this is called, so the
  // load happens immediately. In Standalone we have to wait until JUCE
  // finishes wiring up the StandaloneFilterWindow, otherwise the WKWebView's
  // NSView attaches to no NSWindow and renders blank on some Macs.
  loadMainUrlIfNeeded();
}

TONE3000Editor::TONE3000Editor(TONE3000Processor& p) : AudioProcessorEditor(&p), processor(p) {
  // Dark-theme every JUCE-drawn surface (standalone audio settings dialog,
  // dialog backgrounds). The web UI itself is unaffected. SharedResourcePointer
  // keeps one instance across plugin instances in the same process.
  juce::LookAndFeel::setDefaultLookAndFeel(&darkLookAndFeel.get());

  mainWebView = std::make_unique<EditorWebViewSetup::GuardedWebView>(
      EditorWebViewSetup::buildMainWebViewOptions(this));

  // Attach the WebView synchronously so it inherits the editor's NSView/NSWindow
  // as soon as the editor is parented. Deferring this via callAsync was
  // racing with the Standalone window/mic-permission setup and leaving the
  // WKWebView with `viewWindow=0x0` at load time.
  mainWebView->setOpaque(true);
  addAndMakeVisible(*mainWebView);

  // Bespoke audio settings (standalone only): the controller listens to the
  // device manager and pushes changes to the UI, which re-pulls state.
  if (StandaloneAudioSettings::isAvailable())
    audioSettings = std::make_unique<StandaloneAudioSettings>(processor, [this] {
      if (mainWebView != nullptr)
        mainWebView->emitEventIfBrowserIsVisible("audioDeviceChanged", juce::var{});
    });

  // MIDI map push (all builds): learn commits, removals and state restores
  // land as one event so the settings UI re-pulls instead of polling.
  processor.midiMapper.onChanged = [this] {
    if (mainWebView != nullptr)
      mainWebView->emitEventIfBrowserIsVisible("midiMapChanged", juce::var{});
  };

  // Chain-change push (see Editor.h). 20 Hz keeps mutation→UI latency at
  // ~50 ms worst case for the cost of one atomic read per tick.
  lastPushedRevision = processor.getCurrentChainRevision();
  startTimerHz(20);

  // Grow-only resizing: corner/edge drags scale the whole window between the
  // 1024x578 design size and kMaxScale times it, aspect-locked. Restore the
  // session's scale (persisted via the processor; see ProcessorState.cpp).
#if JUCE_IOS
  // iOS gets one fixed, full-screen window: no corner drags, no host resize
  // request, no persisted scale. Installing the aspect-locked constrainer here
  // is not merely useless, it is harmful: the kiosk-mode window applies it to
  // the screen bounds, and on a 4:3 iPad the 1024:578 lock resolves by height,
  // making the editor ~1814pt wide inside a 1366pt screen and clipping a third
  // of the UI off the right edge. Take the size the window gives us instead and
  // let the web UI letterbox its 1024x578 design box into it (useUiScale),
  // which is the same path a host that refuses a resize already exercises.
  //
  // Still start at the design size and with the persisted chrome height, as
  // every other platform does: an editor that is 0x0 until the kiosk window
  // hands it bounds makes the UI's first scale calculation divide by a zero
  // viewport, and totalHeight() is read before the web UI reports its own
  // extra height back.
  extraContentHeight = juce::jlimit(0, 160, processor.editorExtraHeight.load());
  setSize(kWidth, totalHeight());
  setResizable(false, false);
#else
  setResizable(true, true);
  // Read the persisted scale before touching the constraints: installing the
  // resize limits already snaps the editor to the 1x minimum, and resized()
  // writes that back through processor.editorScale.
  const double savedScale = juce::jlimit(1.0, kMaxScale, processor.editorScale.load());
  // Pre-size for the persistent chrome the UI renders on first paint (the
  // hint bar preference survives sessions). Without this the window opens at
  // the bare design height, the first React commit overflows it, and the
  // post-paint height report grows the window a beat later: a visible
  // two-step launch jank. The banner is excluded (it's genuinely dynamic and
  // animates in when its state resolves).
  extraContentHeight = juce::jlimit(0, 160, processor.editorExtraHeight.load());
  updateResizeConstraints();
  applyScaledSize(savedScale);
#endif  // JUCE_IOS
}

void TONE3000Editor::applyScaledSize(double scale) {
  setSize(juce::roundToInt(kWidth * scale), juce::roundToInt(totalHeight() * scale));
}

void TONE3000Editor::updateResizeConstraints() {
  setResizeLimits(kWidth, totalHeight(), juce::roundToInt(kWidth * kMaxScale),
                  juce::roundToInt(totalHeight() * kMaxScale));
  // The ratio tracks the chrome strips, so corner drags at any extra-height
  // state preserve the current layout exactly.
  getConstrainer()->setFixedAspectRatio(static_cast<double>(kWidth) / totalHeight());
}

void TONE3000Editor::setExtraContentHeight(int pixels, int persistentPixels) {
  // setSize() reaches the host as a resize request through the plugin
  // wrapper (resizeView in VST3), so this works in DAWs too; a host that
  // refuses keeps the old size and the web UI shrinks to fit instead (see
  // useUiScale in the web UI). Standalone resizes its own window directly.
  // The UI reports design-space pixels; the window change is scaled.
  // Generous ceiling: banner (~44) + hint bar (~36) with headroom to spare.
  const int clamped = juce::jlimit(0, 160, pixels);
#if JUCE_IOS
  // The iOS window is the screen; it cannot grow to make room for a chrome
  // strip. Record the persistent portion for symmetry and let the web UI
  // shrink the design box to fit, exactly as it does for a host that refuses
  // the resize.
  processor.editorExtraHeight.store(juce::jlimit(0, 160, persistentPixels));
  extraContentHeight = clamped;
#else
  // Remember the session-persistent portion (the hint bar; the banner is
  // dynamic) even when the window size itself doesn't change, so the next
  // editor opens pre-sized for the chrome the UI will render on first paint.
  processor.editorExtraHeight.store(juce::jlimit(0, 160, persistentPixels));
  if (clamped == extraContentHeight)
    return;
  const double scale = currentScale();
  extraContentHeight = clamped;
  // Update the constrainer directly instead of via updateResizeConstraints():
  // setResizeLimits() immediately re-applies the constrainer to the *current*
  // bounds, and mid-change (new limits, stale size) the fixed-aspect pass can
  // snap the WIDTH for a frame. The web UI derives its CSS zoom from the
  // width, so that transient reads as a whole-UI scale flash. Setting the
  // constrainer values is pure bookkeeping; the single setSize below then
  // moves straight to the final, already-consistent box.
  if (auto* c = getConstrainer()) {
    c->setSizeLimits(kWidth, totalHeight(), juce::roundToInt(kWidth * kMaxScale),
                     juce::roundToInt(totalHeight() * kMaxScale));
    c->setFixedAspectRatio(static_cast<double>(kWidth) / totalHeight());
  }
  applyScaledSize(scale);
#endif  // JUCE_IOS
}

void TONE3000Editor::timerCallback() {
  const juce::uint32 revision = processor.getCurrentChainRevision();
  if (revision == lastPushedRevision || mainWebView == nullptr)
    return;
  lastPushedRevision = revision;
  mainWebView->emitEventIfBrowserIsVisible("chainChanged",
                                           juce::var(static_cast<int>(revision)));
}

TONE3000Editor::~TONE3000Editor() {
  stopTimer();
  // The mapper outlives the editor (it's the processor's); detach our webview
  // hook before the webview dies.
  processor.midiMapper.onChanged = nullptr;
  // Tear down the audio settings bridge first: it holds a device-manager
  // change listener (and possibly the input meter tap) that must not outlive
  // the webview it reports to.
  audioSettings.reset();
  // The tuner and block spectrum analyzers are only useful while the UI is
  // visible; stop feeding them when the editor goes away (the webview can't
  // send the disables itself on teardown).
  processor.setTunerEnabled(false);
  processor.disableAllBlockSpectrums();
  if (mainWebView) {
    removeChildComponent(mainWebView.get());
    mainWebView.reset();
  }
}

void TONE3000Editor::paint(juce::Graphics& g) {
  // Paint background black to eliminate white flash during loading
  g.fillAll(juce::Colours::black);
}

void TONE3000Editor::loadMainUrlIfNeeded() {
  if (mainUrlLoaded || mainWebView == nullptr)
    return;

  // Require a real top-level component before kicking off the load. For
  // Standalone this is the JUCEWindow / NSWindow; for plug-ins this is the
  // host's window. Without a parent, WKWebView has nothing to render into.
  if (getTopLevelComponent() == nullptr || getTopLevelComponent() == this)
    return;

  mainUrlLoaded = true;

  juce::String mainUrl;
#ifdef JUCE_DEBUG
  juce::Logger::writeToLog("Development mode: loading from localhost:5173");
  mainUrl = "http://localhost:5173/";
#else
  juce::Logger::writeToLog("Release mode: loading from embedded resources");
  mainUrl = juce::WebBrowserComponent::getResourceProviderRoot() + "index.html";
#endif
  // Failed navigations (OAuth redirect with tone3000.com unreachable)
  // recover by coming back here; see GuardedWebView::pageLoadHadNetworkError.
  mainWebView->setRecoveryUrl(mainUrl);
  mainWebView->goToURL(mainUrl);
}

void TONE3000Editor::pickLocalToneFile(
    bool pickFolder, const juce::String& targetBlockId,
    juce::WebBrowserComponent::NativeFunctionCompletion completion) {
  auto cancelled = [] {
    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("cancelled", true);
    return juce::var(result.get());
  };

  // One dialog at a time: a second request (double right-click) resolves as
  // cancelled instead of stacking choosers.
  if (localFileChooser != nullptr) {
    completion(cancelled());
    return;
  }

#if JUCE_IOS
  // iOS has no usable folder route. The document picker can return a folder
  // URL, but everything it returns from the Files app is security scoped and a
  // scoped directory cannot be enumerated through juce::URL, so a picked
  // folder would be an unreadable handle. Multi-select is the equivalent the
  // platform does support, so "Load Folder" asks for the files themselves and
  // the same many-models-at-once path runs on the result.
  localFileChooser = std::make_unique<juce::FileChooser>(
      pickFolder ? "Load Files" : "Load File", juce::File{}, juce::String("*.nam;*.wav"));

  const int flags = juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectFiles |
                    (pickFolder ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
#else
  localFileChooser = std::make_unique<juce::FileChooser>(
      pickFolder ? "Load Folder" : "Load File", juce::File{},
      pickFolder ? juce::String("*") : juce::String("*.nam;*.wav"));

  const int flags = juce::FileBrowserComponent::openMode |
                    (pickFolder ? juce::FileBrowserComponent::canSelectDirectories
                                : juce::FileBrowserComponent::canSelectFiles);
#endif

  // The dialog can outlive user patience but not the editor: destroying the
  // editor destroys the chooser (dialog dismissed, callback never fires, and
  // the dying webview's pending promise is moot). The SafePointer covers any
  // platform that still delivers the callback mid-teardown.
  juce::Component::SafePointer<TONE3000Editor> self(this);
  localFileChooser->launchAsync(
      flags, [self, cancelled, target = targetBlockId.toStdString(),
              completion = std::move(completion)](const juce::FileChooser& chooser) {
        if (self == nullptr)
          return;
        // Release the chooser once its callback unwinds (it is the caller).
        juce::MessageManager::callAsync([self] {
          if (self != nullptr)
            self->localFileChooser.reset();
        });

#if JUCE_IOS
        // getURLResults(), not getResults(): JUCE's own FileChooser docs say
        // to use the URL form on mobile, and here it is load bearing rather
        // than stylistic. The picker's files live outside the app sandbox and
        // are only readable through the security scope JUCE bookmarked;
        // getResults() flattens them to raw paths that the sandbox then
        // refuses to open ("Couldn't read the file"), which is exactly what a
        // file picked from Files on a device did before this.
        const auto results = chooser.getURLResults();
        if (results.isEmpty()) {
          completion(cancelled());
          return;
        }
        completion(self->processor.loadLocalToneUrls(results, target));
#else
        const auto results = chooser.getResults();
        if (results.isEmpty()) {
          completion(cancelled());
          return;
        }
        completion(self->processor.loadLocalTonePath(results.getReference(0), target));
#endif
      });
}

void TONE3000Editor::resized() {
  // Real pixels only, no transform. The webview handles devicePixelRatio
  // itself, and the page fits its design box to the actual viewport
  // (letterboxed; see useUiScale in the web UI).
  if (mainWebView != nullptr)
    mainWebView->setBounds(getLocalBounds());
  // Skip persisting while we're correcting our own size rather than
  // reflecting one the user (or host) actually chose; see restoringSize.
  // No user- or host-chosen scale exists on iOS (the window is the screen),
  // so there is nothing to persist and currentScale() would just record the
  // screen aspect.
#if ! JUCE_IOS
  if (!restoringSize)
    processor.editorScale.store(currentScale());
#endif
}

// Get the WebView UI resources from BinaryData
std::optional<juce::WebBrowserComponent::Resource> TONE3000Editor::getResource(
    const juce::String& url) {
  juce::Logger::writeToLog("Requested URL: " + url);

  // Extract filename and normalize to match BinaryData naming
  juce::String filename = juce::URL(url).getFileName().trim();
  juce::String resourceName = filename.removeCharacters("-").replaceCharacter('.', '_');

  int size = 0;
  const char* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), size);

  if (data == nullptr || size <= 0) {
    juce::Logger::writeToLog("Resource not found or empty: " + resourceName);
    return std::nullopt;
  }

  std::vector<std::byte> content(static_cast<size_t>(size));
  std::memcpy(content.data(), data, static_cast<size_t>(size));

  juce::String ext = filename.fromLastOccurrenceOf(".", false, false).toLowerCase();
  juce::String mime = getMimeForExtension(ext);
  if (mime.isEmpty())
    mime = "application/octet-stream";

  juce::Logger::writeToLog("Returning resource: " + resourceName + " (" + mime + ")");
  return juce::WebBrowserComponent::Resource{std::move(content), mime};
}

// Map file extensions to MIME types for serving embedded resources in the WebView UI
juce::String TONE3000Editor::getMimeForExtension(const juce::String& extension) {
  static const std::unordered_map<juce::String, juce::String> mimeMap = {
      {"htm", "text/html"},
      {"html", "text/html"},
      {"txt", "text/plain"},
      {"jpg", "image/jpeg"},
      {"jpeg", "image/jpeg"},
      {"svg", "image/svg+xml"},
      {"ico", "image/vnd.microsoft.icon"},
      {"json", "application/json"},
      {"png", "image/png"},
      {"css", "text/css"},
      {"map", "application/json"},
      {"js", "text/javascript"},
      {"woff2", "font/woff2"}};

  const auto lower = extension.toLowerCase();

  if (const auto it = mimeMap.find(lower); it != mimeMap.end())
    return it->second;

  jassertfalse;
  return "application/octet-stream";
}