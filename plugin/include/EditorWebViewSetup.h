#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class TONE3000Editor;

namespace EditorWebViewSetup {

juce::WebBrowserComponent::Options buildMainWebViewOptions(TONE3000Editor* editor);

/**
 * Delete the webview's tone3000.com session state (cookies, site storage).
 *
 * Logout in the UI clears the tokens it holds, but the OAuth flows ride on
 * the site session inside the webview — with the cookie still present the
 * next authorize redirect silently re-issues a code without ever showing a
 * login screen. Platform-specific implementations (WebViewCookies.mm / .cpp).
 */
void clearAuthCookies();

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

  /**
   * URL of the plugin UI itself (embedded resources, or the Vite server in
   * dev). Failed navigations recover here: the OAuth flows redirect this
   * view to tone3000.com, and if that navigation dies (offline / site down)
   * the user would otherwise be stranded on a dead page with no way back.
   */
  void setRecoveryUrl(const juce::String& url) { recoveryUrl = url; }

  bool pageAboutToLoad(const juce::String& newUrl) override;
  void newWindowAttemptingToLoad(const juce::String& newUrl) override;
  bool pageLoadHadNetworkError(const juce::String& errorInfo) override;
  void pageFinishedLoading(const juce::String& url) override;

private:
  static bool isAllowedUrl(const juce::String& url);

  juce::String recoveryUrl;
  // True while a recovery load is in flight; stops the failure handler from
  // looping if the recovery URL itself fails (dev server down).
  bool recoveryInFlight = false;
};

}  // namespace EditorWebViewSetup
