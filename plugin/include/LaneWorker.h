#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

// ── Lane worker: one realtime helper thread for parallel stereo lanes ──
//
// In stereo mode the two chain lanes are fully independent (own engines, own
// dry scratch — see Processor.h), so the audio thread can hand one lane to
// this worker, process the other itself, and join. The worker is an
// *extension of the audio callback*: it only ever runs strictly inside the
// audio thread's chainMutex critical section, so it takes no locks and the
// message-thread mutation story is unchanged.
//
// Handoff protocol (all lock-free):
//
//   Idle ──dispatch()──▶ Armed ──worker CAS──▶ Claimed ──▶ Done ──join()──▶ Idle
//                          │
//                          └──join() CAS (worker never woke) ──▶ run inline
//
// The Armed→Claimed transition is a compare-exchange raced between the worker
// and join(): whoever wins runs the job. That gives the audio thread a
// built-in escape hatch — if the worker is descheduled and never picks the
// job up, join() steals it back and runs it inline, degrading to today's
// serial behavior instead of stalling the callback. Once the worker has
// claimed, join() spins (the lanes are similarly sized, so the residual wait
// is short).
//
// Scheduling: the thread runs at realtime priority
// (juce::Thread::startRealtimeThread — time-constraint on macOS, MMCSS "Pro
// Audio" on Windows) and, when the host provides one, joins the device's
// audio workgroup (os_workgroup on macOS — without it, Apple Silicon parks
// the worker on efficiency cores and the join misses deadlines). The
// workgroup arrives via AudioProcessor::audioWorkgroupContextChanged and is
// re-joined from the worker thread itself whenever it changes (tokens are
// thread-affine).
//
// The job is a plain function pointer + context pointer: the audio thread
// builds a small context struct on its own stack (alive until join returns),
// so dispatching allocates nothing.
class LaneWorker : private juce::Thread {
public:
  LaneWorker() : juce::Thread("TONE3000 Lane Worker") {}

  ~LaneWorker() override { stop(); }

  /** Start (or restart) the worker for the given callback geometry — sizes
      the realtime scheduling contract. Message thread / prepareToPlay only. */
  void start(double sampleRate, int samplesPerBlock) {
    stop();
    startRealtimeThread(RealtimeOptions{}
                            .withPriority(10)
                            .withApproximateAudioProcessingTime(juce::jmax(1, samplesPerBlock),
                                                                sampleRate));
  }

  /** Stop the thread. Any armed-but-unclaimed job is abandoned (the audio
      thread's join() steals and runs it inline), a claimed one is finished
      before the thread exits. Never called on the RT path. */
  void stop() {
    signalThreadShouldExit();
    wake.signal();
    stopThread(2000);
  }

  /** True when dispatch() can hand work off. */
  bool isRunning() const { return isThreadRunning(); }

  /** Hand the device's audio workgroup over (empty = leave). Safe from any
      thread; the worker joins/leaves from its own loop (tokens are
      thread-affine). */
  void setAudioWorkgroup(juce::AudioWorkgroup newWorkgroup) {
    {
      const juce::SpinLock::ScopedLockType l(workgroupLock);
      if (pendingWorkgroup == newWorkgroup)
        return;
      pendingWorkgroup = std::move(newWorkgroup);
    }
    workgroupEpoch.fetch_add(1, std::memory_order_release);
    wake.signal();
  }

  using JobFn = void (*)(void*);

  /** RT-safe. Publish a job for the worker. Returns false (nothing
      published) when the worker isn't running — the caller runs the work
      inline. Exactly one dispatch may be in flight; every successful
      dispatch MUST be paired with join() before the next one. `ctx` must
      stay alive until join() returns. */
  bool dispatch(JobFn fn, void* ctx) {
    if (!isThreadRunning())
      return false;
    jassert(jobState.load(std::memory_order_relaxed) == static_cast<int>(JobState::Idle));
    jobFn = fn;
    jobCtx = ctx;
    jobState.store(static_cast<int>(JobState::Armed), std::memory_order_release);
    wake.signal();
    return true;
  }

  /** RT-safe. Wait for the dispatched job to finish. If the worker never
      claimed it (descheduled, dying), steal it and run it inline — the
      callback then costs exactly what the serial path did. */
  void join() {
    for (;;) {
      int state = jobState.load(std::memory_order_acquire);
      if (state == static_cast<int>(JobState::Done)) {
        jobState.store(static_cast<int>(JobState::Idle), std::memory_order_relaxed);
        return;
      }
      if (state == static_cast<int>(JobState::Armed)) {
        int expected = static_cast<int>(JobState::Armed);
        if (jobState.compare_exchange_strong(expected, static_cast<int>(JobState::Claimed),
                                             std::memory_order_acquire)) {
          jobFn(jobCtx);
          jobState.store(static_cast<int>(JobState::Idle), std::memory_order_relaxed);
          return;
        }
        continue;  // the worker won the claim race — fall through to spin
      }
      cpuPause();  // Claimed: the worker is on it, the residual wait is short
    }
  }

private:
  enum class JobState : int { Idle = 0, Armed, Claimed, Done };

  static void cpuPause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  }

  void run() override {
    // FTZ/DAZ are per-thread CPU flags: without this the worker computes NAM
    // tails in denormal range at ~100× cost while the audio thread doesn't.
    juce::ScopedNoDenormals noDenormals;

    juce::WorkgroupToken workgroupToken;
    int seenEpoch = -1;

    while (!threadShouldExit()) {
      // Pick up a changed device workgroup (rare: device switches).
      const int epoch = workgroupEpoch.load(std::memory_order_acquire);
      if (epoch != seenEpoch) {
        seenEpoch = epoch;
        juce::AudioWorkgroup wg;
        {
          const juce::SpinLock::ScopedLockType l(workgroupLock);
          wg = pendingWorkgroup;
        }
        if (wg)
          wg.join(workgroupToken);  // re-join handles leaving the old one
        else
          workgroupToken.reset();
      }

      int expected = static_cast<int>(JobState::Armed);
      if (jobState.compare_exchange_strong(expected, static_cast<int>(JobState::Claimed),
                                           std::memory_order_acquire)) {
        jobFn(jobCtx);
        jobState.store(static_cast<int>(JobState::Done), std::memory_order_release);
        continue;
      }

      // Park until the next dispatch. The 1 ms timeout only bounds how long
      // shutdown/workgroup changes can go unnoticed — dispatch() signals, so
      // job pickup latency is the event wake (~µs), not the timeout.
      wake.wait(1);
    }
  }

  std::atomic<int> jobState{static_cast<int>(JobState::Idle)};
  JobFn jobFn = nullptr;
  void* jobCtx = nullptr;
  juce::WaitableEvent wake;

  // The workgroup handed over by setAudioWorkgroup, consumed by the worker
  // loop. SpinLock because AudioWorkgroup copies aren't atomic; both sides
  // hold it for nanoseconds and never on the audio thread.
  juce::SpinLock workgroupLock;
  juce::AudioWorkgroup pendingWorkgroup;
  std::atomic<int> workgroupEpoch{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneWorker)
};
