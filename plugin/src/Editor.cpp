#include "Editor.h"
#include "Processor.h"

void TONE3000Editor::parentHierarchyChanged() {
  if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    // Snapshot our own size before flipping the title bar style: JUCE
    // immediately relayouts the title-bar/content split within the window's
    // current bounds, which can stretch or shrink us before our own
    // aspect-ratio constrainer catches up — previously "corrected" for with
    // a hardcoded +28 title-bar-height guess, which doesn't hold on every
    // OS/version and could leave us a few px off. Re-asserting our exact
    // pre-toggle size instead lets JUCE's own resize listener
    // (StandaloneFilterWindow::MainContentComponent, which already measures
    // the real native frame) grow the *window* to exactly contain us again —
    // no guesswork.
    const int w = getWidth();
    const int h = getHeight();
    // Whatever resize this dance causes along the way is us correcting
    // ourselves, not the user choosing a size — don't let it clobber the
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
  }

#if JUCE_MAC
  // DAW hosts own the plugin's NSWindow and usually leave mouse-moved events
  // off, which kills hover/cursor feedback in the WKWebView (clicks still
  // work). Re-apply on every reparent since hosts can recreate the window
  // when the editor is closed and reopened.
  if (auto* peer = getPeer())
    EditorWebViewSetup::enableHostWindowMouseMovedEvents(peer->getNativeHandle());
#endif

  // Trigger the WebView load only once the editor has a real top-level
  // component (i.e. the NSWindow on macOS exists and is on-screen). In a DAW
  // the editor is parented to the host's window before this is called, so the
  // load happens immediately. In Standalone we have to wait until JUCE
  // finishes wiring up the StandaloneFilterWindow — otherwise the WKWebView's
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
  // session's scale (persisted via the processor — see ProcessorState.cpp).
  setResizable(true, true);
  // Read the persisted scale before touching the constraints: installing the
  // resize limits already snaps the editor to the 1x minimum, and resized()
  // writes that back through processor.editorScale.
  const double savedScale = juce::jlimit(1.0, kMaxScale, processor.editorScale.load());
  updateResizeConstraints();
  applyScaledSize(savedScale);
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

void TONE3000Editor::setExtraContentHeight(int pixels) {
  // setSize() reaches the host as a resize request through the plugin
  // wrapper (resizeView in VST3), so this works in DAWs too — a host that
  // refuses keeps the old size and the webview scrolls, the same fallback
  // as before. Standalone resizes its own window directly.
  // The UI reports design-space pixels; the window change is scaled.
  // Generous ceiling: banner (~44) + hint bar (~36) with headroom to spare.
  const int clamped = juce::jlimit(0, 160, pixels);
  if (clamped == extraContentHeight)
    return;
  const double scale = currentScale();
  extraContentHeight = clamped;
  updateResizeConstraints();
  applyScaledSize(scale);
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
  // recover by coming back here — see GuardedWebView::pageLoadHadNetworkError.
  mainWebView->setRecoveryUrl(mainUrl);
  mainWebView->goToURL(mainUrl);
}

void TONE3000Editor::resized() {
  // Real pixels only — no transform. The webview handles devicePixelRatio
  // itself, and the page applies its own CSS zoom from the viewport width.
  if (mainWebView != nullptr)
    mainWebView->setBounds(getLocalBounds());
  // Skip persisting while we're correcting our own size rather than
  // reflecting one the user (or host) actually chose — see restoringSize.
  if (!restoringSize)
    processor.editorScale.store(currentScale());
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