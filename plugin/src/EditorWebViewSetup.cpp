#include "EditorWebViewSetup.h"
#include "Editor.h"

namespace EditorWebViewSetup {

namespace {
// The JS bridge delivers primitives with backend-dependent types (bool, int,
// int64, double, or stringified). Every native function normalizes through
// these two instead of hand-rolling the coercion.
bool coerceBool(const juce::var& v) {
  if (v.isBool())
    return static_cast<bool>(v);
  if (v.isDouble() || v.isInt() || v.isInt64())
    return static_cast<double>(v) > 0.5;
  return v.toString() == "true";
}

double coerceDouble(const juce::var& v) {
  if (v.isDouble() || v.isInt() || v.isInt64())
    return static_cast<double>(v);
  if (v.isBool())
    return static_cast<bool>(v) ? 1.0 : 0.0;
  return v.toString().getDoubleValue();
}

/**
 * Uniform native-function shape: validates arity once, and a malformed call
 * resolves to `fallback` instead of each handler hand-rolling the check. The
 * handler is a plain synchronous `args -> var`; every bridge function here
 * completes inline on the message thread.
 */
template <typename Fn>
auto guarded(int minArgs, juce::var fallback, Fn&& fn) {
  return [minArgs, fallback, fn = std::forward<Fn>(fn)](
             const juce::Array<juce::var>& args,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
    if (args.size() < minArgs) {
      completion(fallback);
      return;
    }
    completion(fn(args));
  };
}
}  // namespace

bool GuardedWebView::isAllowedUrl(const juce::String& url) {
  // JUCE swaps in about:blank while the component is hidden; must stay allowed.
  if (url == "about:blank")
    return true;
  // Embedded UI served through the resource provider (juce://juce.backend/ on
  // macOS/Linux, https://juce.backend/ on Windows).
  if (url.startsWith(juce::WebBrowserComponent::getResourceProviderRoot()))
    return true;
  // Vite dev server.
  if (url.startsWith("http://localhost:") || url.startsWith("http://127.0.0.1:"))
    return true;
  // The OAuth Select flow navigates the view to tone3000.com and back.
  const juce::String domain = juce::URL(url).getDomain();
  if (url.startsWith("https://") &&
      (domain == "tone3000.com" || domain.endsWith(".tone3000.com")))
    return true;
  return false;
}

bool GuardedWebView::pageAboutToLoad(const juce::String& newUrl) {
  if (isAllowedUrl(newUrl))
    return true;
  juce::Logger::writeToLog("Blocked webview navigation to: " + newUrl);
  if (newUrl.startsWith("http://") || newUrl.startsWith("https://"))
    juce::URL(newUrl).launchInDefaultBrowser();
  return false;
}

void GuardedWebView::newWindowAttemptingToLoad(const juce::String& newUrl) {
  // target=_blank / window.open: never spawn a second view, use the system browser.
  if (newUrl.startsWith("http://") || newUrl.startsWith("https://"))
    juce::URL(newUrl).launchInDefaultBrowser();
}

bool GuardedWebView::pageLoadHadNetworkError(const juce::String& errorInfo) {
  juce::Logger::writeToLog("WebView navigation failed: " + errorInfo);
  // The only remote navigations this view makes are the OAuth redirects to
  // tone3000.com; a failure means the site is unreachable and the user is
  // stuck on a dead page. Recover by reloading the plugin UI; chain state
  // lives natively and tokens in localStorage, so nothing is lost. In
  // release the recovery URL is served from embedded resources and can't
  // itself hit the network; `recoveryInFlight` stops a retry loop in dev
  // builds where it's the (possibly down) Vite server.
  //
  // The query param tells the UI why it was reloaded, so it can surface the
  // OAuth error overlay (retry / dismiss) instead of landing silently on the
  // main screen. The resource provider ignores it (juce::URL::getFileName
  // strips query parameters), as does the OAuth callback detection.
  if (recoveryUrl.isNotEmpty() && !recoveryInFlight) {
    recoveryInFlight = true;
    goToURL(recoveryUrl + "?t3k-nav-error=1");
  }
  return false;  // never show the platform's built-in error page
}

void GuardedWebView::pageFinishedLoading(const juce::String&) {
  recoveryInFlight = false;
}

// WebView2's cache/storage folder (Windows only). A stable per-user location
// instead of the temp dir: temp cleaners can purge it mid-session, and a
// persistent cache makes editor cold-opens faster. Matches the app-data root
// used by PresetManager.
static juce::File webView2DataFolder() {
  const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("TONE3000")
                          .getChildFile("WebView2");
  folder.createDirectory();
  return folder;
}

juce::WebBrowserComponent::Options buildMainWebViewOptions(TONE3000Editor* editor) {
  return juce::WebBrowserComponent::Options{}
      .withNativeIntegrationEnabled()
      // If the UI ever goes blank after being hidden/re-shown (e.g. tabbing
      // between panes in some DAWs), enable this. By default JUCE navigates the
      // WebView to about:blank when hidden and some hosts/macOS versions fail to
      // restore it. Left off for now; flip on only if we hit that issue.
      // .withKeepPageLoadedWhenBrowserIsHidden()
      .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
      .withWinWebView2Options(
          juce::WebBrowserComponent::Options::WinWebView2{}
              .withUserDataFolder(webView2DataFolder())
              // Match the UI theme before the first page paints (no white flash).
              .withBackgroundColour(juce::Colours::black)
              // No link-hover status bar / Edge error page inside the plugin.
              .withStatusBarDisabled()
              .withBuiltInErrorPageDisabled())
      .withResourceProvider(
          [editor](const auto& url) { return editor->getResource(url); },
          juce::URL{"http://localhost:5173/"}.getOrigin())
      .withOptionsFrom(editor->controlParameterIndexReceiver)
      .withOptionsFrom(editor->inputLevelRelay)
      .withOptionsFrom(editor->outputLevelRelay)
      .withOptionsFrom(editor->outputBalanceRelay)
      .withOptionsFrom(editor->spreadEnabledRelay)
      .withOptionsFrom(editor->spreadOffsetRelay)
      .withOptionsFrom(editor->spreadWobbleRelay)
      .withOptionsFrom(editor->spreadWobbleEnabledRelay)
      .withOptionsFrom(editor->spreadCrossoverRelay)
      .withOptionsFrom(editor->spreadCrossoverEnabledRelay)
      .withOptionsFrom(editor->spreadDiffuseEnabledRelay)
      .withOptionsFrom(editor->alignEnabledRelay)
      .withOptionsFrom(editor->alignOffsetRelay)
      .withOptionsFrom(editor->alignWobbleRelay)
      .withOptionsFrom(editor->alignWobbleEnabledRelay)
      .withOptionsFrom(editor->alignCrossoverRelay)
      .withOptionsFrom(editor->alignCrossoverEnabledRelay)
      .withOptionsFrom(editor->alignDiffuseEnabledRelay)
      .withOptionsFrom(editor->chainPanLeftRelay)
      .withOptionsFrom(editor->chainPanRightRelay)
      .withOptionsFrom(editor->chainPanLinkedRelay)
      .withOptionsFrom(editor->chainSoloLeftRelay)
      .withOptionsFrom(editor->chainSoloRightRelay)
      .withOptionsFrom(editor->chainInvertLeftRelay)
      .withOptionsFrom(editor->chainInvertRightRelay)
      .withOptionsFrom(editor->bassRelay)
      .withOptionsFrom(editor->midRelay)
      .withOptionsFrom(editor->trebleRelay)
      .withOptionsFrom(editor->gateThresholdRelay)
      .withOptionsFrom(editor->gateEnabledRelay)
      .withOptionsFrom(editor->toneEqEnabledRelay)
      .withOptionsFrom(editor->calibrateInputRelay)
      .withOptionsFrom(editor->inputCalibrationLevelRelay)
      .withOptionsFrom(editor->osEnabledRelay)
      .withOptionsFrom(editor->osFactorRelay)
      // --- Chain mutations -------------------------------------------------
      .withNativeFunction(
          // (toneJson, targetInsertId?): the tone lands in the insert slot
          // the user clicked; absent/stale ids fall back to the active
          // lane's first insert.
          "loadTone", guarded(1, juce::var(""), [editor](const juce::Array<juce::var>& args) {
            const std::string targetInsertId =
                args.size() >= 2 ? args[1].toString().toStdString() : std::string();
            return juce::var(editor->processor.loadTone(args[0].toString(), targetInsertId));
          }))
      .withNativeFunction(
          // (title, files, targetInsertId?): local .nam/.wav file(s) dropped
          // on an insert slot (one for a file, many for a folder). The
          // webview can't hand over file paths, so the bytes ride the bridge
          // as [{ name, data }] with base64 data; native validates, stashes
          // and loads them as one block (see loadLocalTone). Returns
          // { blockId } or a user-facing { error }.
          "loadLocalTone", guarded(2, juce::var(), [editor](const juce::Array<juce::var>& args) {
            const std::string targetInsertId =
                args.size() >= 3 ? args[2].toString().toStdString() : std::string();
            return editor->processor.loadLocalTone(args[0].toString(), args[1], targetInsertId);
          }))
      .withNativeFunction(
          // (pickFolder, targetBlockId?): the tile menus' Load File / Load
          // Folder actions. Opens the native OS picker, then loads the pick
          // through the same pipeline as a drop, from its path (no base64
          // round-trip). Not `guarded`: the completion resolves later, from
          // the chooser callback. Resolves with { blockId } / { error } like
          // loadLocalTone, or { cancelled: true } when dismissed. Exists
          // because drops can't be the only way in: Linux never delivers OS
          // file drags to the webview (see pickLocalToneFile in Editor.h).
          "pickLocalToneFile",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            const bool pickFolder = args.size() >= 1 && coerceBool(args[0]);
            const juce::String targetBlockId =
                args.size() >= 2 ? args[1].toString() : juce::String();
            editor->pickLocalToneFile(pickFolder, targetBlockId, std::move(completion));
          })
      .withNativeFunction(
          // Replace the tone of an existing block (Swap action). Keeps the
          // block's chain position and user params.
          "swapTone", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.swapTone(args[0].toString().toStdString(),
                                                        args[1].toString()));
          }))
      .withNativeFunction(
          // (toneJson): best-effort metadata re-sync from a fresh /tones/{id}
          // payload. Merges into every block holding that tone (stored models
          // array preserved); metadata only, not undoable, revision bumps
          // only on real change.
          "refreshToneMetadata", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.refreshToneMetadata(args[0].toString()));
          }))
      .withNativeFunction(
          // (blockId, modelId, modelJson): native only stores the active
          // model, so the full model object always rides along.
          "switchModel", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            const juce::var modelData =
                args[2].isObject() ? args[2] : juce::JSON::parse(args[2].toString());
            return juce::var(editor->processor.switchModel(args[0].toString().toStdString(),
                                                           static_cast<int>(args[1]), modelData));
          }))
      .withNativeFunction(
          // Retry a failed model download (block.loadFailed); re-queues the
          // block's active model through the background loader.
          "retryModelLoad", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.retryModelLoad(args[0].toString().toStdString()));
          }))
      .withNativeFunction(
          "removeChainBlock", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.removeChainBlock(args[0].toString().toStdString()));
          }))
      .withNativeFunction(
          "reorderChainBlocks", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            if (!args[0].isArray())
              return juce::var(false);
            std::vector<std::string> newOrder;
            for (const auto& item : *args[0].getArray())
              newOrder.push_back(item.toString().toStdString());
            return juce::var(editor->processor.reorderChainBlocks(newOrder));
          }))
      .withNativeFunction(
          // (blockId, "left" | "right", targetIndex): drag across lanes.
          "moveBlockToChain", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.moveBlockToChain(
                args[0].toString().toStdString(), args[1].toString(), static_cast<int>(args[2])));
          }))
      .withNativeFunction(
          // (sourceBlockId, "left" | "right", targetIndex): clone a live tone
          // block with all its settings (alt-drag duplicate). Returns the new
          // block id, "" on failure.
          "duplicateChainBlock",
          guarded(3, juce::var(""), [editor](const juce::Array<juce::var>& args) {
            return juce::var(juce::String(editor->processor.duplicateChainBlock(
                args[0].toString().toStdString(), args[1].toString(), static_cast<int>(args[2]))));
          }))
      .withNativeFunction(
          // (blockId): snapshot the block (tone + settings + model bytes)
          // into the in-app block clipboard. The snapshot is self-contained,
          // so paste survives preset switches and deleting the source.
          "copyChainBlock", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.copyChainBlock(args[0].toString().toStdString()));
          }))
      .withNativeFunction(
          // ("left" | "right", targetIndex): rebuild the copied block there
          // (an insert slot at the index is filled). Returns the new block
          // id, "" on failure (empty clipboard, right lane while mono).
          "pasteChainBlock",
          guarded(2, juce::var(""), [editor](const juce::Array<juce::var>& args) {
            return juce::var(juce::String(editor->processor.pasteChainBlock(
                args[0].toString(), static_cast<int>(args[1]))));
          }))
      .withNativeFunction(
          "swapChains", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.swapChains());
          }))
      .withNativeFunction(
          // ("left" | "right", afterBlockId): branch the other lane off the
          // named lane after one of its tone blocks (stereo mode only).
          "setChainBranch", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setChainBranch(
                args[0].toString(), args[1].toString().toStdString()));
          }))
      .withNativeFunction(
          // Revert to two fully independent chains.
          "clearChainBranch", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.clearChainBranch());
          }))
      .withNativeFunction(
          "setStereoMode", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setStereoMode(coerceBool(args[0]));
            return juce::var(true);
          }))
      .withNativeFunction(
          // ("stereo" | "left" | "right"): which channels of a stereo
          // source feed the plugin (the faceplate input-mode button).
          "setInputMode", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setInputMode(
                TONE3000Processor::inputModeFromString(args[0].toString()));
            return juce::var(true);
          }))
      .withNativeFunction(
          "setActiveEditChain", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setActiveEditChain(args[0].toString());
            return juce::var(true);
          }))
      .withNativeFunction(
          // Machine-wide default NAM A2 size for newly added blocks, in
          // NAM's slimmable-size domain (0 = lite, 1 = full). Loaded blocks
          // keep their own size (see setBlockSlimSize); persists in the
          // shared settings file, and the current value rides getChainState
          // as `namSlimSizeDefault`.
          "setNamSlimSizeDefault", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setNamSlimSizeDefault(coerceDouble(args[0]));
            return juce::var(true);
          }))
      .withNativeFunction(
          // Machine-wide multi-core processing (true = stereo chains fork
          // across cores, and oversampled NAM models fork their phase
          // instances too). Applies instantly (pure scheduling, output is
          // bit-identical) and persists in the shared settings file; the
          // current value rides getChainState as `multiCore`.
          "setMultiCore", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setMultiCoreEnabled(coerceBool(args[0]));
            return juce::var(true);
          }))
      // --- Per-block params / EQ / spectrum ---------------------------------
      .withNativeFunction(
          // Single entry point for per-block user params:
          // (blockId, "enabled" | "normalize" | "inputGain" | "outputGain" |
          //  "mix", numeric value; booleans as 0/1).
          "setBlockParam", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockParam(
                args[0].toString().toStdString(), args[1].toString(), coerceDouble(args[2])));
          }))
      .withNativeFunction(
          // (blockId, slimSize 0..1): the block's NAM A2 size (0 = lite,
          // 1 = full). Not a setBlockParam param because it retiers the
          // block's loaded engine under the chain-edit fade; rides
          // getChainState as params.slimSize.
          "setBlockSlimSize", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockSlimSize(
                args[0].toString().toStdString(), coerceDouble(args[1])));
          }))
      .withNativeFunction(
          // (blockId, bandIndex, { type, freqHz, gainDb, q }). Whole-band
          // updates keep drags atomic and give undo/redo a clean unit later.
          "setBlockEqBand", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            if (!args[2].isObject())
              return juce::var(false);
            return juce::var(editor->processor.setBlockEqBand(
                args[0].toString().toStdString(), static_cast<int>(args[1]), args[2]));
          }))
      .withNativeFunction(
          // EQ power/bypass: band settings stay, processing is skipped.
          "setBlockEqEnabled", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockEqEnabled(args[0].toString().toStdString(),
                                                                 coerceBool(args[1])));
          }))
      .withNativeFunction(
          // EQ position: true = before the block's model (after its input
          // gain), false = after the block (default).
          "setBlockEqPre", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockEqPre(args[0].toString().toStdString(),
                                                             coerceBool(args[1])));
          }))
      .withNativeFunction(
          "resetBlockEq", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.resetBlockEq(args[0].toString().toStdString()));
          }))
      .withNativeFunction(
          // The UI enables a block's analyzer only while its EQ view is
          // open; otherwise the audio thread does no analyzer work for it.
          "setBlockSpectrumEnabled",
          guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockSpectrumEnabled(
                args[0].toString().toStdString(), coerceBool(args[1])));
          }))
      .withNativeFunction(
          // Polled ~30 Hz by an open EQ view. Returns 64 log-spaced dB bins.
          "getBlockSpectrum", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->processor.getBlockSpectrum(args[0].toString().toStdString());
          }))
      // --- Chain state / history --------------------------------------------
      .withNativeFunction(
          "getChainState", guarded(0, juce::var(), [editor](const juce::Array<juce::var>& args) {
            // Optional arg 0: last revision the UI saw; -1 forces a full state.
            // JS numbers may arrive as int, int64 or double depending on backend.
            const bool hasRevision =
                args.size() >= 1 && (args[0].isInt() || args[0].isInt64() || args[0].isDouble());
            return editor->processor.getChainState(hasRevision ? static_cast<int>(args[0]) : -1);
          }))
      .withNativeFunction(
          "undoChain", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.undoChain());
          }))
      .withNativeFunction(
          "redoChain", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.redoChain());
          }))
      // --- Presets -----------------------------------------------------------
      .withNativeFunction(
          // Fetched on demand (browser open, after mutations); the active
          // preset itself rides the revision-gated getChainState poll.
          "getPresetList", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.getPresetList();
          }))
      .withNativeFunction(
          // Saves the current chain + faceplate params under a name; a
          // same-name user preset is overwritten (that's the update path).
          "savePreset", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->processor.savePreset(args[0].toString());
          }))
      .withNativeFunction(
          "loadPreset", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.loadPreset(args[0].toString()));
          }))
      .withNativeFunction(
          "renamePreset", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.renamePreset(args[0].toString(),
                                                            args[1].toString()));
          }))
      .withNativeFunction(
          "deletePreset", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.deletePreset(args[0].toString()));
          }))
      .withNativeFunction(
          // (id, delta): N steps within the preset's browser section
          // (negative = earlier). Prev/next and MIDI follow it.
          "movePreset", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.movePreset(
                args[0].toString(), static_cast<int>(coerceDouble(args[1]))));
          }))
      .withNativeFunction(
          // The top bar's New button: back to the factory-default state
          // (empty mono chain, default faceplate params, no active preset).
          // Undoable; false when already at default.
          "resetToDefault", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.resetToDefault());
          }))
      // --- Audio device settings (standalone only) ---------------------------
      // All of these route through the StandaloneAudioSettings controller,
      // which exists only under the standalone holder; in hosts they resolve
      // to void/{ok:false} and the UI never renders the System Settings tab.
      .withNativeFunction(
          "getAudioDeviceState", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->getState()
                                                    : juce::var();
          }))
      .withNativeFunction(
          "setAudioDeviceType", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setDeviceType(args[0].toString())
                       : juce::var();
          }))
      .withNativeFunction(
          // ("input" | "output" | "linked", deviceName; "" = no device)
          "setAudioDevice", guarded(2, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setDevice(args[0].toString(), args[1].toString())
                       : juce::var();
          }))
      .withNativeFunction(
          // ([deviceChannelIndices]): 1 = mono, 2 = stereo.
          "setAudioInputChannels",
          guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            if (editor->audioSettings == nullptr || !args[0].isArray())
              return juce::var();
            return editor->audioSettings->setInputChannels(*args[0].getArray());
          }))
      .withNativeFunction(
          "setAudioOutputPair", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setOutputPair(static_cast<int>(args[0]))
                       : juce::var();
          }))
      .withNativeFunction(
          "setAudioSampleRate", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setSampleRate(coerceDouble(args[0]))
                       : juce::var();
          }))
      .withNativeFunction(
          "setAudioBufferSize", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setBufferSize(
                             static_cast<int>(coerceDouble(args[0])))
                       : juce::var();
          }))
      .withNativeFunction(
          "setHearYourself", guarded(1, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setHearYourself(coerceBool(args[0]))
                       : juce::var();
          }))
      .withNativeFunction(
          "playTestTone", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->playTestTone()
                                                    : juce::var();
          }))
      .withNativeFunction(
          "openAudioControlPanel", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->openControlPanel()
                                                    : juce::var();
          }))
      .withNativeFunction(
          "restartAudioDevice", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->restartDevice()
                                                    : juce::var();
          }))
      .withNativeFunction(
          // Jump to the OS microphone privacy page (the fix for a denied mic).
          "openMicSettings", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->openMicSettings()
                                                    : juce::var();
          }))
      // --- MIDI: device layer (standalone only) -------------------------------
      .withNativeFunction(
          // (identifier, enabled): which hardware feeds the plugin.
          "setMidiInputEnabled", guarded(2, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setMidiInputEnabled(args[0].toString(),
                                                                    coerceBool(args[1]))
                       : juce::var();
          }))
      .withNativeFunction(
          "openBluetoothMidiPairing", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->openBluetoothMidiPairing()
                       : juce::var();
          }))
      // --- MIDI: mapping engine (lives in the processor; works in hosts too) --
      .withNativeFunction(
          "getMidiMapState", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.midiMapper.getState();
          }))
      .withNativeFunction(
          // (channel): 0 = omni, 1-16 = that channel only.
          "setMidiChannelFilter", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.midiMapper.setChannelFilter(static_cast<int>(coerceDouble(args[0])));
            return juce::var(true);
          }))
      .withNativeFunction(
          // (targetId): arm learn; the next CC / note-on wins.
          "startMidiLearn", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.midiMapper.startLearn(args[0].toString());
            return juce::var(true);
          }))
      .withNativeFunction(
          "cancelMidiLearn", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            editor->processor.midiMapper.cancelLearn();
            return juce::var(true);
          }))
      .withNativeFunction(
          "removeMidiMapping", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.midiMapper.removeMapping(args[0].toString()));
          }))
      .withNativeFunction(
          // (targetId, ccNumber): assign a CC directly, the typed alternative
          // to learn.
          "setMidiCcMapping", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.midiMapper.setCcMapping(
                args[0].toString(), static_cast<int>(coerceDouble(args[1]))));
          }))
      .withNativeFunction(
          // Channel-picker meters: enabled only while the picker is on screen.
          "setAudioInputMetering",
          guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            if (editor->audioSettings == nullptr)
              return juce::var(false);
            editor->audioSettings->setInputMetering(coerceBool(args[0]));
            return juce::var(true);
          }))
      .withNativeFunction(
          // Polled ~30 Hz while metering. dB per device input channel index.
          "getAudioInputLevels", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->audioSettings != nullptr ? editor->audioSettings->getInputLevels()
                                                    : juce::var();
          }))
      .withNativeFunction(
          // The UI reports the combined height of its chrome strips (banner +
          // hint bar) so the window grows instead of squishing the plugin UI.
          // In hosts this becomes a resize request to the DAW. The optional
          // second arg is the session-persistent portion (the hint bar, not
          // the banner), used to pre-size the next editor at launch.
          "setExtraContentHeight", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            const int total = static_cast<int>(coerceDouble(args[0]));
            const int persistent =
                args.size() > 1 ? static_cast<int>(coerceDouble(args[1])) : total;
            editor->setExtraContentHeight(total, persistent);
            return juce::var(true);
          }))
      // --- Meters / tuner / auto-balance -------------------------------------
      .withNativeFunction(
          // One call per UI frame covers every meter in the plugin:
          // { input, output, blocks: { blockId: { in, out } } } (dB).
          "getMeterLevels", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.getMeterLevels();
          }))
      .withNativeFunction(
          "setTunerEnabled", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setTunerEnabled(coerceBool(args[0]));
            return juce::var(true);
          }))
      .withNativeFunction(
          "getTunerReading", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.getTunerReading();
          }))
      .withNativeFunction(
          // Arm a one-shot chain energy measurement; the UI polls
          // pollAutoBalance for progress/result (see Processor.h).
          "startAutoBalance", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            editor->processor.startAutoBalance();
            return juce::var(true);
          }))
      .withNativeFunction(
          "cancelAutoBalance", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            editor->processor.cancelAutoBalance();
            return juce::var(true);
          }))
      .withNativeFunction(
          "pollAutoBalance", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.pollAutoBalance();
          }))
      .withNativeFunction(
          // Arm a one-shot Align probe (stereo chain mode; output mutes for
          // ~half a second); the UI polls pollAutoOffset for progress and
          // the result (see Processor.h / AutoOffset.h).
          "startAutoOffset", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            editor->processor.startAutoOffset();
            return juce::var(true);
          }))
      .withNativeFunction(
          "cancelAutoOffset", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            editor->processor.cancelAutoOffset();
            return juce::var(true);
          }))
      .withNativeFunction(
          "pollAutoOffset", guarded(0, juce::var(), [editor](const juce::Array<juce::var>&) {
            return editor->processor.pollAutoOffset();
          }))
      // --- Misc ---------------------------------------------------------------
      .withNativeFunction(
          // Single source of truth is the CMake project version; the UI uses
          // this for the startup update check against the tone3000.com API.
          "getPluginVersion", guarded(0, juce::var(""), [](const juce::Array<juce::var>&) {
            return juce::var(JucePlugin_VersionString);
          }))
      .withNativeFunction(
          // Stable machine hash (survives storage/peripheral changes; a CPU
          // or motherboard swap invalidates it). The UI sends it on every
          // version check so the API can target betas per install. Empty
          // when JUCE can't compute one.
          "getUniqueDeviceID", guarded(0, juce::var(""), [](const juce::Array<juce::var>&) {
            return juce::var(juce::SystemStats::getUniqueDeviceID());
          }))
      .withNativeFunction(
          // Called by the main webview after the OAuth Select flow completes
          // (and again on every refresh). Stored on the processor so that
          // background model downloads can attach the Bearer header.
          "setAccessToken", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setAccessToken(args[0].toString());
            return juce::var(true);
          }))
      .withNativeFunction(
          // Logout: drop the webview's tone3000.com session (cookies + site
          // storage) so the next OAuth redirect shows a real login screen
          // instead of silently re-approving on the old session.
          "clearAuthCookies", guarded(0, false, [](const juce::Array<juce::var>&) {
            clearAuthCookies();
            return juce::var(true);
          }))
      .withNativeFunction(
          // Clipboard writes from the webview itself are unreliable across
          // the JUCE webview backends, so the UI routes them through native.
          "copyToClipboard", guarded(1, false, [](const juce::Array<juce::var>& args) {
            juce::SystemClipboard::copyTextToClipboard(args[0].toString());
            return juce::var(true);
          }))
#if JUCE_MAC || JUCE_WINDOWS
      .withNativeFunction(
          // Jump to the OS clock settings from the secure-connection modal
          // (a wrong system clock is the usual reason HTTPS to tone3000.com
          // fails). Process::openDocument routes settings URIs to
          // NSWorkspace/ShellExecute. Not registered on Linux (no reliable
          // settings URI across desktops); the UI hides the button then.
          "openDateTimeSettings", guarded(0, false, [](const juce::Array<juce::var>&) {
  #if JUCE_MAC
            // Ventura+ pane id; older systems fall back to the settings root.
            return juce::var(juce::Process::openDocument(
                "x-apple.systempreferences:com.apple.Date-Time-Settings.extension", {}));
  #else
            return juce::var(juce::Process::openDocument("ms-settings:dateandtime", {}));
  #endif
          }))
#endif
      .withNativeFunction(
          // ("Space" | "Enter"): hand a transport keypress the UI has no use
          // for to the host DAW, whose play/stop or return-to-start shortcut
          // it almost certainly is (see WindowKeyEvents.mm / .cpp). No-op in
          // Standalone, where there is no transport to reach; the UI still
          // suppresses the key so it can't beep or scroll.
          "forwardKeyToHost", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            if (juce::JUCEApplicationBase::isStandaloneApp())
              return juce::var(false);
            const HostKey key = args[0].toString() == "Enter" ? HostKey::enter : HostKey::space;
            if (auto* peer = editor->getPeer())
              forwardKeyToHost(peer->getNativeHandle(), key);
            return juce::var(true);
          }))
      .withNativeFunction(
          // Console output forwarded from the WebView so it lands in the
          // on-disk log even in release builds (where the Web Inspector is
          // disabled). See the user script below for the console.* shims.
          "webLog", guarded(0, juce::var(), [](const juce::Array<juce::var>& args) {
            const juce::String level = args.size() > 0 ? args[0].toString() : "log";
            const juce::String msg = args.size() > 1 ? args[1].toString() : juce::String{};
            juce::Logger::writeToLog("[webview:" + level + "] " + msg);
            return juce::var{};
          }))
      .withNativeFunction(
          "copyLogs", guarded(0, false, [](const juce::Array<juce::var>&) {
            const juce::File logFile = TONE3000Processor::getLogFile();
            if (!logFile.existsAsFile())
              return juce::var(false);
            // Ship only the tail so we never dump a multi-MB file onto the clipboard.
            juce::String text = logFile.loadFileAsString();
            constexpr int maxChars = 200000;
            if (text.length() > maxChars)
              text = text.getLastCharacters(maxChars);
            juce::SystemClipboard::copyTextToClipboard(text);
            return juce::var(true);
          }))
      .withNativeFunction(
          "revealLogs", guarded(0, juce::var(""), [](const juce::Array<juce::var>&) {
            const juce::File logFile = TONE3000Processor::getLogFile();
            if (!logFile.existsAsFile())
              return juce::var("");
            logFile.revealToUser();
            return juce::var(logFile.getFullPathName());
          }))
      // --- Web Inspector (Settings -> Diagnostics) ------------------------
      // Debugging aid for prod builds: right-click -> Inspect Element on the
      // plugin UI, off by default. macOS only: JUCE's WebView2 backend hard-
      // disables dev tools, so the UI hides the toggle when unsupported.
      .withNativeFunction(
          "getWebInspectorEnabled", guarded(0, juce::var(), [](const juce::Array<juce::var>&) {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
#if JUCE_MAC
            obj->setProperty("supported", true);
#else
            obj->setProperty("supported", false);
#endif
            obj->setProperty("enabled", TONE3000Processor::readPersistedWebInspectorEnabled());
            return juce::var(obj.get());
          }))
      .withNativeFunction(
          "setWebInspectorEnabled", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            const bool enabled = coerceBool(args[0]);
            TONE3000Processor::persistWebInspectorEnabled(enabled);
#if JUCE_MAC
            if (auto* peer = editor->getPeer())
              setWebInspectorEnabled(peer->getNativeHandle(), enabled);
#else
            (void)editor;
#endif
            return juce::var(true);
          }))
      .withUserScript(R"(
            document.documentElement.style.backgroundColor = '#000000';
            // This script runs at document start, where document.body is still
            // null; touching it directly throws and silently kills the rest of
            // this script (including the console-forwarding shim below).
            if (document.body) document.body.style.backgroundColor = '#000000';

            // Forward WebView console output to the native logger so it is
            // captured in the on-disk log even in release builds.
            (function () {
              // Error fields are non-enumerable, so JSON.stringify(err) is
              // '{}' -- and an Error is exactly what a crash report needs.
              // React logs the caught exception as an object, so the one line
              // worth having used to arrive empty. Unwrap by hand, and duck-type
              // rather than instanceof so cross-realm errors survive too.
              const describe = (p) => {
                if (typeof p === 'string') return p;
                if (p && typeof p.message === 'string' && typeof p.stack === 'string')
                  return (p.name || 'Error') + ': ' + p.message + '\n' + p.stack;
                try {
                  const json = JSON.stringify(p);
                  if (json !== undefined && json !== '{}') return json;
                } catch (e) {
                  return String(p);
                }
                // DOM events and other exotic objects also stringify to '{}'.
                // Naming the type keeps the line worth reading.
                const type = p && p.constructor && p.constructor.name;
                return (type || typeof p) + ' ' + String(p);
              };

              const forward = (level, parts) => {
                try {
                  const text = parts.map(describe).join(' ');
                  // The raw __juce__invoke protocol (what getNativeFunction
                  // wraps). window.__JUCE__.backend only exists once the app
                  // bundle has loaded (it's defined by the JUCE frontend
                  // module compiled into main.js), so fall back to the bare
                  // postMessage the native bootstrap provides at document
                  // start; backend.emitEvent is just this JSON envelope.
                  // resultId -1 is fire-and-forget: no PromiseHandler entry
                  // ever matches it.
                  const payload = {
                    eventId: '__juce__invoke',
                    payload: { name: 'webLog', params: [level, text], resultId: -1 },
                  };
                  if (window.__JUCE__.backend)
                    window.__JUCE__.backend.emitEvent(payload.eventId, payload.payload);
                  else
                    window.__JUCE__.postMessage(JSON.stringify(payload));
                } catch (e) {
                  /* bridge not available (plain-browser dev); drop this line */
                }
              };
              ['log', 'info', 'warn', 'error', 'debug'].forEach((level) => {
                const original = console[level] ? console[level].bind(console) : null;
                console[level] = (...parts) => {
                  if (original) original(...parts);
                  forward(level, parts);
                };
              });
              window.addEventListener('error', (e) => {
                forward('error', [e.message + ' @ ' + e.filename + ':' + e.lineno +
                                  (e.error ? '\n' + describe(e.error) : '')]);
              });
              window.addEventListener('unhandledrejection', (e) => {
                forward('error', ['Unhandled promise rejection: ' + describe(e.reason)]);
              });
            })();

            // The engine string only ever reached the log from the boot
            // watchdog in index.html, which stays quiet once the UI boots. On
            // macOS this is the system WebKit and its version decides which
            // language and DOM features exist, so log it every load: without it
            // a bug report from an old OS is unreproducible guesswork.
            console.log("Main WebView: JUCE C++ Backend loaded | " + navigator.userAgent);
          )");
}

}  // namespace EditorWebViewSetup
