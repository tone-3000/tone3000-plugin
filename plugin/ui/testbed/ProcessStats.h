// This process's CPU time and memory, the way the OS accounts them, for
// --bench. Plain C++ (no JUCE) so the platform headers stay out of the rest
// of the testbed.
#pragma once

namespace t3k::ui::testbed::stats {

// CPU seconds (user + system) since start: every thread of the process.
double processCpuSeconds();
// CPU seconds of the calling thread (the message thread, from the bench).
double threadCpuSeconds();

struct Memory {
  double currentMb = 0;
  double peakMb = 0;
};
// macOS: physical footprint (Activity Monitor's "Memory"); Linux: RSS;
// Windows: working set.
Memory memory();
const char* memoryKind();

}  // namespace t3k::ui::testbed::stats
