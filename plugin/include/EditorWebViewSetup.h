#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class TONE3000Editor;

namespace EditorWebViewSetup {

juce::WebBrowserComponent::Options buildMainWebViewOptions(TONE3000Editor* editor);

}  // namespace EditorWebViewSetup
