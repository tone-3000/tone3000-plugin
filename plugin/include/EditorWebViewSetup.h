#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class TONE3000Editor;

namespace EditorWebViewSetup {

juce::WebBrowserComponent::Options buildMainWebViewOptions(TONE3000Editor* editor);

/**
 * Main-UI WebView with a navigation allowlist.
 *
 * Native integration (loadTone, presets, clipboard, auth token, ...) is
 * injected into every page this view loads, so navigation is restricted to
 * origins we trust: the embedded resource provider, the Vite dev server, and
 * tone3000.com (the OAuth Select flow navigates the view there by design).
 * Anything else — a stray link, a dropped file, a window.open — is blocked
 * in-view and handed to the system browser instead.
 */
class GuardedWebView : public juce::WebBrowserComponent {
public:
  using juce::WebBrowserComponent::WebBrowserComponent;

  bool pageAboutToLoad(const juce::String& newUrl) override;
  void newWindowAttemptingToLoad(const juce::String& newUrl) override;

private:
  static bool isAllowedUrl(const juce::String& url);
};

}  // namespace EditorWebViewSetup
