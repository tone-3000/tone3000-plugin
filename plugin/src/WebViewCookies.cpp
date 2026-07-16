#include "EditorWebViewSetup.h"

namespace EditorWebViewSetup {

// Non-Apple fallback. JUCE doesn't expose WebView2's ICoreWebView2CookieManager
// (the store that actually holds the session on Windows), so this is best
// effort: clearCookies() flushes the legacy WinINet jar, which WebView2 may
// mirror for some configurations. If Windows logout proves sticky, the robust
// fix is recreating the webview with a fresh user-data folder.
void clearAuthCookies() {
  juce::WebBrowserComponent::clearCookies();
  juce::Logger::writeToLog("clearAuthCookies: cleared WinINet cookie jar (best effort)");
}

}  // namespace EditorWebViewSetup
