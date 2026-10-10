#include "StandaloneStateAutosave.h"

namespace StandaloneStateAutosave {

// Nothing to do off Apple platforms: the standalone app saves its state on
// window close there (StandaloneFilterWindow::closeButtonPressed).
void install() {}

}  // namespace StandaloneStateAutosave
