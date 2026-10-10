// UiTestbed --bench: the native UI under realistic load, measured.
//
// Each phase opens a real window on one scenario, feeds the mock a moving
// signal (MockSignal: meters, tile glows, the EQ spectrum, the tuner) and,
// for the interaction phases, drives input through the window's peer the
// way the OS delivers it (knob drags, EQ dot drags, wheel scrolling, a click
// tour across screens). After a warm-up it samples, over the phase:
//
//   proc CPU   all threads of the process, % of one core
//   UI thread  the message thread alone (layout, paint, input)
//   paints/s   paint passes the window made
//   repaint    area repainted per second, in whole windows (60 = the
//              full window every frame at 60 fps)
//   stalls     the bench's 16 ms timer ticks that came > 50 ms apart, and
//   max gap    the longest gap: the message thread busy for frames on end
//   memory     at the end of the phase, and the process peak so far
//
// The scripted part runs on the app's own message loop (timer callbacks),
// not a nested one, so what is measured is what the plugin's editor does.
// Window-server compositing (macOS WindowServer, DWM, X server) happens in
// another process and is not included.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace t3k::ui::testbed {

class Bench {
public:
  virtual ~Bench() = default;

  // `args` from "--bench" on. Starts the run and returns it (keep it alive
  // until `done`), or calls `done` straight away and returns null (--list,
  // bad arguments, nothing matched).
  static std::unique_ptr<Bench> start(const juce::StringArray& args, std::function<void(int exitCode)> done);
};

}  // namespace t3k::ui::testbed
