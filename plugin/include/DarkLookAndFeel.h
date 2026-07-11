#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Brand-dark theme for the few JUCE-drawn surfaces in the plugin — the
 * standalone Audio/MIDI Settings dialog, dialog/window backgrounds and alert
 * boxes. The main UI is the WebView and styles itself.
 *
 * Installed as the process default by the editor via SharedResourcePointer so
 * multiple plugin instances share one object; the destructor un-installs it
 * when the last instance goes away.
 */
class DarkLookAndFeel : public juce::LookAndFeel_V4 {
public:
  DarkLookAndFeel()
      : juce::LookAndFeel_V4({
            juce::Colour(0xff000000),  // windowBackground
            juce::Colour(0xff1c1c1e),  // widgetBackground
            juce::Colour(0xff151517),  // menuBackground
            juce::Colour(0xff3a3a3c),  // outline
            juce::Colour(0xffffffff),  // defaultText
            juce::Colour(0xff2c2c2e),  // defaultFill
            juce::Colour(0xff000000),  // highlightedText
            juce::Colour(0xffffff00),  // highlightedFill (brand yellow)
            juce::Colour(0xffffffff),  // menuText
        }) {}

  ~DarkLookAndFeel() override {
    // Never leave a dangling default if we're the last instance being torn down.
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == this)
      juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
  }
};
