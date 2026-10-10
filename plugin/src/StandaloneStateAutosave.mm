#include "StandaloneStateAutosave.h"

// The standalone filter window header expects the full GUI/audio module set
// to be visible first (same include order as StandaloneAudioSettings.cpp).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#if JUCE_IOS && JucePlugin_Build_Standalone && ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#import <UIKit/UIKit.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace StandaloneStateAutosave {

void install() {
  static bool installed = false;
  if (installed)
    return;
  installed = true;

  // queue:nil runs the block synchronously on the posting thread, which for
  // UIApplicationDidEnterBackgroundNotification is the main (message) thread.
  // That matters: with a queue the block is scheduled asynchronously and the
  // app can be suspended before the write lands.
  [[NSNotificationCenter defaultCenter]
      addObserverForName:UIApplicationDidEnterBackgroundNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification*) {
                auto* holder = juce::StandalonePluginHolder::getInstance();
                if (holder == nullptr)
                  return;  // AUv3, or the holder is already gone.

                holder->savePluginState();

                // savePluginState() only calls PropertySet::setValue, and
                // PropertiesFile defers the write to a 3-second timer that a
                // suspended app never gets to run.
                if (auto* file = dynamic_cast<juce::PropertiesFile*>(holder->settings.get()))
                  file->saveIfNeeded();
              }];
}

}  // namespace StandaloneStateAutosave

#else

namespace StandaloneStateAutosave {
void install() {}
}  // namespace StandaloneStateAutosave

#endif
