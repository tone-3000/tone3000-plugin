#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

// Floating browser window with control bar + select webview for tone selection
class BrowserWindow : public juce::Component {
public:
  BrowserWindow(juce::WebBrowserComponent* mainWebViewToFront,
                const juce::WebBrowserComponent::Options& webviewOptions);

  juce::WebBrowserComponent* getWebView() { return selectWebView.get(); }

private:
  void userTriedToCloseWindow() override;
  void paint(juce::Graphics& g) override;
  void resized() override;

  juce::DrawableButton backButton;
  juce::DrawableButton forwardButton;
  std::unique_ptr<juce::WebBrowserComponent> selectWebView;
  juce::WebBrowserComponent* mainWebViewToFront;
};
