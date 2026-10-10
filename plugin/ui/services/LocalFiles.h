// Local .nam / IR .wav loading: the tile menus' Load File / Load Folder rows
// open the OS picker here, and OS file drops onto a tile land here too. Same
// targeting rules for both: an insert slot adds, a tone tile swaps in place.
// Errors surface as a toast.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

#include "ChainStore.h"
#include "Toast.h"

namespace t3k::ui {

class LocalFiles {
public:
  enum class Kind { file, folder };

  LocalFiles(ChainStore& chain, Toast& toast);
  ~LocalFiles();

  // Open the OS picker (one at a time: a second request while a dialog is up
  // is ignored, like the editor resolved it as cancelled) and load the pick.
  void pick(const std::string& targetBlockId, Kind kind);
  // An OS drag dropped files onto a tile: load the first (a file or folder).
  void drop(const std::string& targetBlockId, const juce::StringArray& paths);

private:
  void report(const juce::String& error);

  ChainStore& chain_;
  Toast& toast_;
  std::unique_ptr<juce::FileChooser> chooser_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(LocalFiles)
};

}  // namespace t3k::ui
