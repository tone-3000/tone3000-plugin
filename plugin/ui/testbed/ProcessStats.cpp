#include "ProcessStats.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#endif

namespace t3k::ui::testbed::stats {

namespace {
constexpr double kMb = 1024.0 * 1024.0;

#if defined(_WIN32)
double seconds(const FILETIME& kernel, const FILETIME& user) {
  auto ticks = [](const FILETIME& f) {
    return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
  };
  return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;  // 100 ns units
}
#else
double clockSeconds(clockid_t clock) {
  timespec ts{};
  clock_gettime(clock, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}
#endif
}  // namespace

double processCpuSeconds() {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  return seconds(kernel, user);
#else
  return clockSeconds(CLOCK_PROCESS_CPUTIME_ID);
#endif
}

double threadCpuSeconds() {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
  return seconds(kernel, user);
#else
  return clockSeconds(CLOCK_THREAD_CPUTIME_ID);
#endif
}

Memory memory() {
  Memory m;
#if defined(__APPLE__)
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    m.currentMb = static_cast<double>(info.phys_footprint) / kMb;
    if (count >= TASK_VM_INFO_REV1_COUNT) m.peakMb = static_cast<double>(info.ledger_phys_footprint_peak) / kMb;
  }
#elif defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string key;
  double kb = 0;
  while (status >> key) {
    if (key == "VmRSS:" && status >> kb) m.currentMb = kb / 1024.0;
    else if (key == "VmHWM:" && status >> kb) m.peakMb = kb / 1024.0;
  }
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    m.currentMb = static_cast<double>(counters.WorkingSetSize) / kMb;
    m.peakMb = static_cast<double>(counters.PeakWorkingSetSize) / kMb;
  }
#endif
  return m;
}

const char* memoryKind() {
#if defined(__APPLE__)
  return "phys footprint";
#elif defined(_WIN32)
  return "working set";
#else
  return "RSS";
#endif
}

}  // namespace t3k::ui::testbed::stats
