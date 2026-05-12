#include "BrowserWindow.h"

BrowserWindow::BrowserWindow(juce::WebBrowserComponent* mainWebViewToFront,
                             const juce::WebBrowserComponent::Options& webviewOptions)
    : mainWebViewToFront(mainWebViewToFront) {
  setOpaque(true);

  selectWebView = std::make_unique<juce::WebBrowserComponent>(webviewOptions);
  selectWebView->setOpaque(true);
  addAndMakeVisible(*selectWebView);
}

void BrowserWindow::userTriedToCloseWindow() {
  DBG("User closed browser window via native close button");
  setVisible(false);
  removeFromDesktop();
  if (mainWebViewToFront)
    mainWebViewToFront->toFront(false);
}

void BrowserWindow::resized() {
  selectWebView->setBounds(getLocalBounds());
}
