#include "EditorWebViewSetup.h"
#include "Editor.h"

namespace EditorWebViewSetup {

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
      .withOptionsFrom(editor->inputBalanceRelay)
      .withOptionsFrom(editor->outputBalanceRelay)
      .withOptionsFrom(editor->spreadEnabledRelay)
      .withOptionsFrom(editor->spreadAmountRelay)
      .withOptionsFrom(editor->spreadJitterRelay)
      .withOptionsFrom(editor->chainPanLeftRelay)
      .withOptionsFrom(editor->chainPanRightRelay)
      .withOptionsFrom(editor->chainPanLinkedRelay)
      .withOptionsFrom(editor->bassRelay)
      .withOptionsFrom(editor->midRelay)
      .withOptionsFrom(editor->trebleRelay)
      .withOptionsFrom(editor->gateThresholdRelay)
      .withOptionsFrom(editor->gateEnabledRelay)
      .withOptionsFrom(editor->toneEqEnabledRelay)
      .withOptionsFrom(editor->normalizeRelay)
      .withOptionsFrom(editor->calibrateInputRelay)
      .withOptionsFrom(editor->inputCalibrationLevelRelay)
      .withNativeFunction(
          "loadTone",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1) {
              juce::String toneJsonString = args[0].toString();
              DBG("Loading tone from JSON (" << toneJsonString.length() << " chars)");
              std::string blockId = editor->processor.loadTone(toneJsonString);
              completion(blockId.empty() ? juce::var("") : juce::var(blockId));
            } else {
              completion(juce::var(""));
            }
          })
      .withNativeFunction(
          "swapTone",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Replace the tone of an existing block (Swap action). Keeps the
            // block's chain position and user params.
            if (args.size() >= 2) {
              juce::String blockId = args[0].toString();
              juce::String toneJsonString = args[1].toString();
              DBG("Swapping tone on block " << blockId << " (" << toneJsonString.length()
                  << " chars)");
              bool success =
                  editor->processor.swapTone(blockId.toStdString(), toneJsonString);
              completion(juce::var(success));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "switchModel",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 2) {
              juce::String blockId = args[0].toString();
              int modelId = (int)args[1];
              DBG("Switching model for block: " << blockId << " to model ID: " << modelId);
              bool success = editor->processor.switchModel(blockId.toStdString(), modelId);
              completion(juce::var(success));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "removeChainBlock",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1) {
              bool success =
                  editor->processor.removeChainBlock(args[0].toString().toStdString());
              completion(juce::var(success));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "getChainState",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Optional arg 0: last revision the UI saw; -1 forces a full state.
            // JS numbers may arrive as int, int64 or double depending on backend.
            const bool hasRevision = args.size() >= 1 &&
                                     (args[0].isInt() || args[0].isInt64() || args[0].isDouble());
            const int knownRevision = hasRevision ? static_cast<int>(args[0]) : -1;
            completion(editor->processor.getChainState(knownRevision));
          })
      .withNativeFunction(
          "showAudioSettings",
          [](const juce::Array<juce::var>&,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
            if (auto* pluginHolder = juce::StandalonePluginHolder::getInstance()) {
              pluginHolder->showAudioSettingsDialog();
              completion(juce::var(true));
              return;
            }
#endif
            completion(juce::var(false));
          })
      .withNativeFunction(
          "reorderChainBlocks",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            DBG("reorderChainBlocks native function called with " << args.size()
                << " arguments");
            if (args.size() >= 1 && args[0].isArray()) {
              juce::Array<juce::var>* orderArray = args[0].getArray();
              std::vector<std::string> newOrder;
              for (const auto& item : *orderArray)
                newOrder.push_back(item.toString().toStdString());
              bool success = editor->processor.reorderChainBlocks(newOrder);
              DBG("Reorder result: " << (success ? "success" : "failed"));
              completion(juce::var(success));
            } else {
              DBG("reorderChainBlocks called with invalid arguments");
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockParam",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Single entry point for per-block user params:
            // (blockId, "enabled" | "inputGain" | "outputGain" | "mix" |
            //  "namSlimmableSize", numeric value — booleans as 0/1).
            if (args.size() >= 3) {
              const juce::String blockId = args[0].toString();
              const juce::String param = args[1].toString();
              double value = 0.0;
              if (args[2].isBool())
                value = static_cast<bool>(args[2]) ? 1.0 : 0.0;
              else if (args[2].isDouble() || args[2].isInt() || args[2].isInt64())
                value = static_cast<double>(args[2]);
              else
                value = args[2].toString().getDoubleValue();
              completion(juce::var(
                  editor->processor.setBlockParam(blockId.toStdString(), param, value)));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockEqBand",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // (blockId, bandIndex, { type, freqHz, gainDb, q }). Whole-band
            // updates keep drags atomic and give undo/redo a clean unit later.
            if (args.size() >= 3 && args[2].isObject()) {
              const juce::String blockId = args[0].toString();
              const int bandIndex = static_cast<int>(args[1]);
              completion(juce::var(editor->processor.setBlockEqBand(blockId.toStdString(),
                                                                    bandIndex, args[2])));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockEqEnabled",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // EQ power/bypass: band settings stay, processing is skipped.
            if (args.size() >= 2) {
              completion(juce::var(editor->processor.setBlockEqEnabled(
                  args[0].toString().toStdString(), static_cast<bool>(args[1]))));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "resetBlockEq",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1) {
              completion(juce::var(
                  editor->processor.resetBlockEq(args[0].toString().toStdString())));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockSpectrumEnabled",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // The UI enables a block's analyzer only while its EQ view is
            // open; otherwise the audio thread does no analyzer work for it.
            if (args.size() >= 2) {
              const bool enabled = args[1].isBool()
                                       ? static_cast<bool>(args[1])
                                       : (args[1].isDouble() ? static_cast<double>(args[1]) > 0.5
                                                             : args[1].toString() == "true");
              completion(juce::var(editor->processor.setBlockSpectrumEnabled(
                  args[0].toString().toStdString(), enabled)));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "getBlockSpectrum",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Polled ~30 Hz by an open EQ view. Returns 64 log-spaced dB bins.
            if (args.size() >= 1) {
              completion(editor->processor.getBlockSpectrum(args[0].toString().toStdString()));
            } else {
              completion(juce::var());
            }
          })
      .withNativeFunction(
          "getPresetList",
          [editor](const juce::Array<juce::var>&,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Fetched on demand (browser open, after mutations) — the active
            // preset itself rides the revision-gated getChainState poll.
            completion(editor->processor.getPresetList());
          })
      .withNativeFunction(
          "savePreset",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Saves the current chain + faceplate params under a name; a
            // same-name user preset is overwritten (that's the update path).
            if (args.size() >= 1 && args[0].isString()) {
              completion(editor->processor.savePreset(args[0].toString()));
            } else {
              completion(juce::var());
            }
          })
      .withNativeFunction(
          "loadPreset",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1 && args[0].isString()) {
              completion(juce::var(editor->processor.loadPreset(args[0].toString())));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "renamePreset",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 2 && args[0].isString() && args[1].isString()) {
              completion(juce::var(
                  editor->processor.renamePreset(args[0].toString(), args[1].toString())));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "deletePreset",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1 && args[0].isString()) {
              completion(juce::var(editor->processor.deletePreset(args[0].toString())));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "undoChain",
          [editor](const juce::Array<juce::var>&,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(juce::var(editor->processor.undoChain()));
          })
      .withNativeFunction(
          "redoChain",
          [editor](const juce::Array<juce::var>&,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(juce::var(editor->processor.redoChain()));
          })
      .withNativeFunction(
          "setStereoMode",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1) {
              const bool enabled = args[0].isBool()
                                       ? static_cast<bool>(args[0])
                                       : (args[0].isDouble() ? static_cast<double>(args[0]) > 0.5
                                                             : args[0].toString() == "true");
              editor->processor.setStereoMode(enabled);
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setInputMode",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1 && args[0].isString()) {
              editor->processor.setStandaloneInputMode(
                  TONE3000Processor::inputModeFromString(args[0].toString()));
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "swapChains",
          [editor](const juce::Array<juce::var>&,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(juce::var(editor->processor.swapChains()));
          })
      .withNativeFunction(
          "setActiveEditChain",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1 && args[0].isString()) {
              editor->processor.setActiveEditChain(args[0].toString());
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setAccessToken",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Called by the main webview after the OAuth Select flow completes
            // (and again on every refresh). Stored on the processor so that
            // background model downloads can attach the Bearer header.
            if (args.size() >= 1 && args[0].isString()) {
              editor->processor.setAccessToken(args[0].toString());
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "getMeterLevels",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // One call per UI frame covers every meter in the plugin:
            // { input, output, blocks: { blockId: { in, out } } } (dB).
            completion(editor->processor.getMeterLevels());
          })
      .withNativeFunction(
          "copyToClipboard",
          [](const juce::Array<juce::var>& args,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Clipboard writes from the webview itself are unreliable across
            // the JUCE webview backends, so the UI routes them through native.
            if (args.size() >= 1) {
              juce::SystemClipboard::copyTextToClipboard(args[0].toString());
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setTunerEnabled",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 1) {
              const bool enabled = args[0].isBool()
                                       ? static_cast<bool>(args[0])
                                       : (args[0].isDouble() ? static_cast<double>(args[0]) > 0.5
                                                             : args[0].toString() == "true");
              editor->processor.setTunerEnabled(enabled);
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "getTunerReading",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(editor->processor.getTunerReading());
          })
      .withNativeFunction(
          "webLog",
          [](const juce::Array<juce::var>& args,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            // Console output forwarded from the WebView so it lands in the
            // on-disk log even in release builds (where the Web Inspector is
            // disabled). See the user script below for the console.* shims.
            const juce::String level = args.size() > 0 ? args[0].toString() : "log";
            const juce::String msg = args.size() > 1 ? args[1].toString() : juce::String{};
            juce::Logger::writeToLog("[webview:" + level + "] " + msg);
            completion(juce::var{});
          })
      .withNativeFunction(
          "copyLogs",
          [](const juce::Array<juce::var>&,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            const juce::File logFile = TONE3000Processor::getLogFile();
            if (!logFile.existsAsFile()) {
              completion(juce::var(false));
              return;
            }
            // Ship only the tail so we never dump a multi-MB file onto the clipboard.
            juce::String text = logFile.loadFileAsString();
            constexpr int maxChars = 200000;
            if (text.length() > maxChars)
              text = text.getLastCharacters(maxChars);
            juce::SystemClipboard::copyTextToClipboard(text);
            completion(juce::var(true));
          })
      .withNativeFunction(
          "revealLogs",
          [](const juce::Array<juce::var>&,
             juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            const juce::File logFile = TONE3000Processor::getLogFile();
            if (logFile.existsAsFile()) {
              logFile.revealToUser();
              completion(juce::var(logFile.getFullPathName()));
            } else {
              completion(juce::var(""));
            }
          })
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
