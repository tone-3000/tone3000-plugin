#include "EditorWebViewSetup.h"

#import <AppKit/AppKit.h>

namespace EditorWebViewSetup {

// Hover states and cursor changes inside WKWebView are driven entirely by
// mouseMoved: NSEvents, and AppKit only delivers those when the hosting
// NSWindow has acceptsMouseMovedEvents enabled. JUCE turns it on for its own
// windows (which is why Standalone works), but DAW hosts own the plugin
// window and most never set it, leaving the web UI with dead :hover rules
// and a permanent arrow cursor. Clicks are unaffected, which is the telltale
// symptom. Called from the editor whenever it lands in a (possibly new)
// window; messaging a nil window is a harmless no-op.
void enableHostWindowMouseMovedEvents(void* nsViewPtr) {
  NSView* view = (__bridge NSView*)nsViewPtr;
  [[view window] setAcceptsMouseMovedEvents:YES];
}

}  // namespace EditorWebViewSetup
