#include "BrowserWindow.h"
#include <cstring>

BrowserWindow::BrowserWindow(juce::WebBrowserComponent* mainWebViewToFront,
                             const juce::WebBrowserComponent::Options& webviewOptions)
    : mainWebViewToFront(mainWebViewToFront),
      backButton("back", juce::DrawableButton::ImageFitted),
      forwardButton("forward", juce::DrawableButton::ImageFitted) {
  setOpaque(true);

  selectWebView = std::make_unique<juce::WebBrowserComponent>(webviewOptions);
  selectWebView->setOpaque(true);
  addAndMakeVisible(*selectWebView);

  const char* backSvgNormal =
      R"SVG(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M9 14L4 9L9 4" stroke="#95959C" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 9H14.5C15.2223 9 15.9375 9.14226 16.6048 9.41866C17.272 9.69506 17.8784 10.1002 18.3891 10.6109C18.8998 11.1216 19.3049 11.728 19.5813 12.3952C19.8577 13.0625 20 13.7777 20 14.5C20 15.2223 19.8577 15.9375 19.5813 16.6048C19.3049 17.272 18.8998 17.8784 18.3891 18.3891C17.8784 18.8998 17.272 19.3049 16.6048 19.5813C15.9375 19.8577 15.2223 20 14.5 20H11" stroke="#95959C" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)SVG";
  const char* backSvgHover =
      R"SVG(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M9 14L4 9L9 4" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 9H14.5C15.2223 9 15.9375 9.14226 16.6048 9.41866C17.272 9.69506 17.8784 10.1002 18.3891 10.6109C18.8998 11.1216 19.3049 11.728 19.5813 12.3952C19.8577 13.0625 20 13.7777 20 14.5C20 15.2223 19.8577 15.9375 19.5813 16.6048C19.3049 17.272 18.8998 17.8784 18.3891 18.3891C17.8784 18.8998 17.272 19.3049 16.6048 19.5813C15.9375 19.8577 15.2223 20 14.5 20H11" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)SVG";
  const char* forwardSvgNormal =
      R"SVG(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><g transform="translate(24, 0) scale(-1, 1)"><path d="M9 14L4 9L9 4" stroke="#95959C" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 9H14.5C15.2223 9 15.9375 9.14226 16.6048 9.41866C17.272 9.69506 17.8784 10.1002 18.3891 10.6109C18.8998 11.1216 19.3049 11.728 19.5813 12.3952C19.8577 13.0625 20 13.7777 20 14.5C20 15.2223 19.8577 15.9375 19.5813 16.6048C19.3049 17.272 18.8998 17.8784 18.3891 18.3891C17.8784 18.8998 17.272 19.3049 16.6048 19.5813C15.9375 19.8577 15.2223 20 14.5 20H11" stroke="#95959C" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></g></svg>)SVG";
  const char* forwardSvgHover =
      R"SVG(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><g transform="translate(24, 0) scale(-1, 1)"><path d="M9 14L4 9L9 4" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 9H14.5C15.2223 9 15.9375 9.14226 16.6048 9.41866C17.272 9.69506 17.8784 10.1002 18.3891 10.6109C18.8998 11.1216 19.3049 11.728 19.5813 12.3952C19.8577 13.0625 20 13.7777 20 14.5C20 15.2223 19.8577 15.9375 19.5813 16.6048C19.3049 17.272 18.8998 17.8784 18.3891 18.3891C17.8784 18.8998 17.272 19.3049 16.6048 19.5813C15.9375 19.8577 15.2223 20 14.5 20H11" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></g></svg>)SVG";

  auto backNormal = juce::Drawable::createFromImageData(backSvgNormal, strlen(backSvgNormal));
  auto backHover = juce::Drawable::createFromImageData(backSvgHover, strlen(backSvgHover));
  auto forwardNormal =
      juce::Drawable::createFromImageData(forwardSvgNormal, strlen(forwardSvgNormal));
  auto forwardHover =
      juce::Drawable::createFromImageData(forwardSvgHover, strlen(forwardSvgHover));

  backButton.setImages(backNormal.get(), backHover.get(), backHover.get());
  backButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  backButton.onClick = [this]() { selectWebView->goBack(); };
  addAndMakeVisible(backButton);

  forwardButton.setImages(forwardNormal.get(), forwardHover.get(), forwardHover.get());
  forwardButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  forwardButton.onClick = [this]() { selectWebView->goForward(); };
  addAndMakeVisible(forwardButton);
}

void BrowserWindow::userTriedToCloseWindow() {
  DBG("User closed browser window via native close button");
  setVisible(false);
  removeFromDesktop();
  if (mainWebViewToFront)
    mainWebViewToFront->toFront(false);
}

void BrowserWindow::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds();
  auto controlBar = bounds.removeFromTop(44);
  g.setColour(juce::Colours::black);
  g.fillRect(controlBar);
  g.setColour(juce::Colour(84, 84, 88).withAlpha(0.65f));
  g.drawLine(0.0f, (float)controlBar.getBottom(), (float)getWidth(),
             (float)controlBar.getBottom(), 1.0f);
}

void BrowserWindow::resized() {
  auto bounds = getLocalBounds();
  int controlBarHeight = 44;
  auto controlBar = bounds.removeFromTop(controlBarHeight);
  int buttonSize = 24;
  int buttonGap = 16;
  int verticalPadding = (controlBarHeight - buttonSize) / 2;
  int rightEdge = getWidth();
  forwardButton.setBounds(rightEdge - buttonGap - buttonSize, verticalPadding, buttonSize,
                          buttonSize);
  backButton.setBounds(rightEdge - buttonGap - buttonSize - buttonGap - buttonSize,
                       verticalPadding, buttonSize, buttonSize);
  selectWebView->setBounds(bounds);
}
