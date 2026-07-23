#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Brand-dark theme for the few JUCE-drawn surfaces that remain in the
 * standalone app now that audio settings and banners are bespoke web UI: the
 * title-bar Options menu, the Save/Load-state file dialogs, and any JUCE
 * AlertWindow. The main UI (and the audio settings panel) is the WebView and
 * styles itself.
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
