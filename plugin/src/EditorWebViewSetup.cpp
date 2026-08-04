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
 * handler is a plain synchronous `args -> var` — every bridge function here
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
  // stuck on a dead page. Recover by reloading the plugin UI — chain state
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
      .withOptionsFrom(editor->stereoOffsetEnabledRelay)
      .withOptionsFrom(editor->stereoOffsetTimeRelay)
      .withOptionsFrom(editor->chainPanLeftRelay)
      .withOptionsFrom(editor->chainPanRightRelay)
      .withOptionsFrom(editor->chainPanLinkedRelay)
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
          // (toneJson, targetInsertId?) — the tone lands in the insert slot
          // the user clicked; absent/stale ids fall back to the active
          // lane's first insert.
          "loadTone", guarded(1, juce::var(""), [editor](const juce::Array<juce::var>& args) {
            const std::string targetInsertId =
                args.size() >= 2 ? args[1].toString().toStdString() : std::string();
            return juce::var(editor->processor.loadTone(args[0].toString(), targetInsertId));
          }))
      .withNativeFunction(
          // Replace the tone of an existing block (Swap action). Keeps the
          // block's chain position and user params.
          "swapTone", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.swapTone(args[0].toString().toStdString(),
                                                        args[1].toString()));
          }))
      .withNativeFunction(
          // (blockId, modelId, modelJson) — native only stores the active
          // model, so the full model object always rides along.
          "switchModel", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            const juce::var modelData =
                args[2].isObject() ? args[2] : juce::JSON::parse(args[2].toString());
            return juce::var(editor->processor.switchModel(args[0].toString().toStdString(),
                                                           static_cast<int>(args[1]), modelData));
          }))
      .withNativeFunction(
          // Retry a failed model download (block.loadFailed) — re-queues the
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
          // (blockId, "left" | "right", targetIndex) — drag across lanes.
          "moveBlockToChain", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.moveBlockToChain(
                args[0].toString().toStdString(), args[1].toString(), static_cast<int>(args[2])));
          }))
      .withNativeFunction(
          // (sourceBlockId, "left" | "right", targetIndex) — clone a tone
          // block with all its settings (copy/paste and alt-drag duplicate).
          // Returns the new block id, "" on failure.
          "duplicateChainBlock",
          guarded(3, juce::var(""), [editor](const juce::Array<juce::var>& args) {
            return juce::var(juce::String(editor->processor.duplicateChainBlock(
                args[0].toString().toStdString(), args[1].toString(), static_cast<int>(args[2]))));
          }))
      .withNativeFunction(
          "swapChains", guarded(0, false, [editor](const juce::Array<juce::var>&) {
            return juce::var(editor->processor.swapChains());
          }))
      .withNativeFunction(
          // ("left" | "right", afterBlockId) — branch the other lane off the
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
          // ("stereo" | "left" | "right") — which channels of a stereo
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
          // Machine-wide NAM A2 size (false = lite, true = full). Retiers
          // every loaded NAM block immediately and persists in the shared
          // settings file; the current value rides getChainState as
          // `namFullSize`.
          "setNamFullSize", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setNamFullSize(coerceBool(args[0]));
            return juce::var(true);
          }))
      .withNativeFunction(
          // Machine-wide multi-core stereo (true = process the two stereo
          // chains on separate cores). Applies instantly (pure scheduling —
          // output is bit-identical) and persists in the shared settings
          // file; the current value rides getChainState as `multiCore`.
          "setMultiCore", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.setMultiCoreEnabled(coerceBool(args[0]));
            return juce::var(true);
          }))
      // --- Per-block params / EQ / spectrum ---------------------------------
      .withNativeFunction(
          // Single entry point for per-block user params:
          // (blockId, "enabled" | "normalize" | "inputGain" | "outputGain" |
          //  "mix", numeric value — booleans as 0/1).
          "setBlockParam", guarded(3, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.setBlockParam(
                args[0].toString().toStdString(), args[1].toString(), coerceDouble(args[2])));
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
          // Fetched on demand (browser open, after mutations) — the active
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
          // (id, delta) — one step up (-1) / down (+1) within the preset's
          // browser section. Prev/next and MIDI program changes follow it.
          "movePreset", guarded(2, false, [editor](const juce::Array<juce::var>& args) {
            return juce::var(editor->processor.movePreset(
                args[0].toString(), static_cast<int>(coerceDouble(args[1]))));
          }))
      // --- Audio device settings (standalone only) ---------------------------
      // All of these route through the StandaloneAudioSettings controller,
      // which exists only under the standalone holder — in hosts they resolve
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
          // ("input" | "output" | "linked", deviceName — "" = no device)
          "setAudioDevice", guarded(2, juce::var(), [editor](const juce::Array<juce::var>& args) {
            return editor->audioSettings != nullptr
                       ? editor->audioSettings->setDevice(args[0].toString(), args[1].toString())
                       : juce::var();
          }))
      .withNativeFunction(
          // ([deviceChannelIndices]) — 1 = mono, 2 = stereo.
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
          // (identifier, enabled) — which hardware feeds the plugin.
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
          // (channel) — 0 = omni, 1–16 = that channel only.
          "setMidiChannelFilter", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->processor.midiMapper.setChannelFilter(static_cast<int>(coerceDouble(args[0])));
            return juce::var(true);
          }))
      .withNativeFunction(
          // (targetId) — arm learn; the next CC / note-on wins.
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
          // In hosts this becomes a resize request to the DAW.
          "setExtraContentHeight", guarded(1, false, [editor](const juce::Array<juce::var>& args) {
            editor->setExtraContentHeight(static_cast<int>(coerceDouble(args[0])));
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
          // Arm a one-shot chain time-alignment measurement (stereo chain
          // mode); the UI polls pollAutoOffset for progress/result (see
          // Processor.h / AutoOffset.h).
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
      .withUserScript(R"(
            document.documentElement.style.backgroundColor = '#000000';
            document.body.style.backgroundColor = '#000000';

            // Forward WebView console output to the native logger so it is
            // captured in the on-disk log even in release builds.
            (function () {
              const forward = (level, parts) => {
                try {
                  const text = parts
                    .map((p) => {
                      if (typeof p === 'string') return p;
                      try { return JSON.stringify(p); } catch (e) { return String(p); }
                    })
                    .join(' ');
                  window.__JUCE__.backend.callFunction('webLog', [level, text]);
                } catch (e) {
                  /* backend not ready yet; drop this line */
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
                forward('error', [e.message + ' @ ' + e.filename + ':' + e.lineno]);
              });
              window.addEventListener('unhandledrejection', (e) => {
                forward('error', ['Unhandled promise rejection: ' + (e.reason && e.reason.stack ? e.reason.stack : e.reason)]);
              });
            })();

            console.log("Main WebView: JUCE C++ Backend loaded");
          )");
}

}  // namespace EditorWebViewSetup
