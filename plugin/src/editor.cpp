#include "Editor.h"
#include "Processor.h"

void TONE3000Editor::parentHierarchyChanged() {
  if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(1024, 738);  // Add extra height for title bar
    setSize(1024, 710);
  }
}

TONE3000Editor::TONE3000Editor(TONE3000Processor& p) : AudioProcessorEditor(&p), processor(p) {
  selectWebViewOptions = EditorWebViewSetup::buildSelectWebViewOptions(this);
  mainWebView =
      std::make_unique<juce::WebBrowserComponent>(EditorWebViewSetup::buildMainWebViewOptions(this));

  setSize(1024, 710);

  juce::MessageManager::callAsync([this]() {
    // Add main webview (visible) - resized() will set bounds
    addAndMakeVisible(*mainWebView);
    
    // Set background to black to prevent white flash
    mainWebView->setOpaque(true);

    // Load main UI
    juce::String mainUrl;
#ifdef JUCE_DEBUG
    juce::Logger::writeToLog("Development mode: loading from localhost:5173");
    mainUrl = "http://localhost:5173/";
#else
    juce::Logger::writeToLog("Release mode: loading from embedded resources");
    mainUrl = juce::WebBrowserComponent::getResourceProviderRoot() + "index.html";
#endif
    mainWebView->goToURL(mainUrl);
  });


}

TONE3000Editor::~TONE3000Editor() {
  if (browserWindow) {
    browserWindow->removeFromDesktop();
    browserWindow.reset();
  }
  if (mainWebView) {
    removeChildComponent(mainWebView.get());
    mainWebView.reset();
  }
}

void TONE3000Editor::paint(juce::Graphics& g) {
  // Paint background black to eliminate white flash during loading
  g.fillAll(juce::Colours::black);
}

void TONE3000Editor::resized() {
  auto bounds = getLocalBounds();
  
  // Main webview always full size - stable!
  mainWebView->setBounds(bounds);
  
  // Update browser window position if visible
  if (browserWindow && browserWindow->isVisible()) {
    int meterWidth = 40 * 2;
    auto browserBounds = getScreenBounds().reduced(meterWidth, 0);
    browserWindow->setBounds(browserBounds);
  }
}

// Get the WebView UI resources from BinaryData
std::optional<juce::WebBrowserComponent::Resource> TONE3000Editor::getResource(
    const juce::String& url) {
  juce::Logger::writeToLog("Requested URL: " + url);

  // Extract filename and normalize to match BinaryData naming
  juce::String filename = juce::URL(url).getFileName().trim();
  juce::String resourceName = filename.removeCharacters("-").replaceCharacter('.', '_');

  int size = 0;
  const char* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), size);

  if (data == nullptr || size <= 0) {
    juce::Logger::writeToLog("Resource not found or empty: " + resourceName);
    return std::nullopt;
  }

  std::vector<std::byte> content(static_cast<size_t>(size));
  std::memcpy(content.data(), data, static_cast<size_t>(size));

  juce::String ext = filename.fromLastOccurrenceOf(".", false, false).toLowerCase();
  juce::String mime = getMimeForExtension(ext);
  if (mime.isEmpty())
    mime = "application/octet-stream";

  juce::Logger::writeToLog("Returning resource: " + resourceName + " (" + mime + ")");
  return juce::WebBrowserComponent::Resource{std::move(content), mime};
}

// Map file extensions to MIME types for serving embedded resources in the WebView UI
juce::String TONE3000Editor::getMimeForExtension(const juce::String& extension) {
  static const std::unordered_map<juce::String, juce::String> mimeMap = {
      {"htm", "text/html"},
      {"html", "text/html"},
      {"txt", "text/plain"},
      {"jpg", "image/jpeg"},
      {"jpeg", "image/jpeg"},
      {"svg", "image/svg+xml"},
      {"ico", "image/vnd.microsoft.icon"},
      {"json", "application/json"},
      {"png", "image/png"},
      {"css", "text/css"},
      {"map", "application/json"},
      {"js", "text/javascript"},
      {"woff2", "font/woff2"}};

  const auto lower = extension.toLowerCase();

  if (const auto it = mimeMap.find(lower); it != mimeMap.end())
    return it->second;

  jassertfalse;
  return "application/octet-stream";
}