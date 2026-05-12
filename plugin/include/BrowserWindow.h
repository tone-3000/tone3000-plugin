#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

// Floating window that hosts the select webview for tone selection. The
// webview itself is full-bleed — TONE3000's `menubar=true` Select-flow option
// renders the back / forward / refresh / close controls inside the page, so we
// don't draw a native control bar around it.
class BrowserWindow : public juce::Component {
public:
  BrowserWindow(juce::WebBrowserComponent* mainWebViewToFront,
                const juce::WebBrowserComponent::Options& webviewOptions);

  juce::WebBrowserComponent* getWebView() { return selectWebView.get(); }

private:
  void userTriedToCloseWindow() override;
  void resized() override;

  std::unique_ptr<juce::WebBrowserComponent> selectWebView;
  juce::WebBrowserComponent* mainWebViewToFront;
};
