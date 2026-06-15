#include "EditorWebViewSetup.h"
#include "Editor.h"

namespace EditorWebViewSetup {

juce::WebBrowserComponent::Options buildMainWebViewOptions(TONE3000Editor* editor) {
  return juce::WebBrowserComponent::Options{}
      .withNativeIntegrationEnabled()
      .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
      .withWinWebView2Options(
          juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(
              juce::File::getSpecialLocation(juce::File::tempDirectory)))
      .withResourceProvider(
          [editor](const auto& url) { return editor->getResource(url); },
          juce::URL{"http://localhost:5173/"}.getOrigin())
      .withOptionsFrom(editor->controlParameterIndexReceiver)
      .withOptionsFrom(editor->inputLevelRelay)
      .withOptionsFrom(editor->outputLevelRelay)
      .withOptionsFrom(editor->bassRelay)
      .withOptionsFrom(editor->midRelay)
      .withOptionsFrom(editor->trebleRelay)
      .withOptionsFrom(editor->gateThresholdRelay)
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
          "getChainStatus",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(editor->processor.getChainStatus());
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
          "setBlockOutputGain",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 2) {
              juce::String blockId = args[0].toString();
              float normalizedGain = static_cast<float>(
                  args[1].isDouble() ? static_cast<double>(args[1])
                                    : args[1].toString().getDoubleValue());
              editor->processor.setBlockOutputGain(blockId.toStdString(), normalizedGain);
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockMix",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 2) {
              juce::String blockId = args[0].toString();
              float normalizedMix = static_cast<float>(
                  args[1].isDouble() ? static_cast<double>(args[1])
                                    : args[1].toString().getDoubleValue());
              editor->processor.setBlockMix(blockId.toStdString(), normalizedMix);
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
          })
      .withNativeFunction(
          "setBlockNamSlimmableSize",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() >= 2) {
              juce::String blockId = args[0].toString();
              const double size = args[1].isDouble() ? static_cast<double>(args[1])
                                                     : args[1].toString().getDoubleValue();
              editor->processor.setBlockNamSlimmableSize(blockId.toStdString(), size);
              completion(juce::var(true));
            } else {
              completion(juce::var(false));
            }
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
          "getInputMeterLevel",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(juce::var(editor->processor.getInputMeterLevel()));
          })
      .withNativeFunction(
          "getOutputMeterLevel",
          [editor](const juce::Array<juce::var>& args,
                   juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(juce::var(editor->processor.getOutputMeterLevel()));
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
