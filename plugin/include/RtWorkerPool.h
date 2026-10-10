#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

// Realtime worker pool: fork/join helper threads for the chain stage
// (design overview in plugin/docs/multicore.md).
//
// Two parallel sections use it, both pure scheduling with bit-identical
// output regardless of which thread runs what:
//   - stereo lanes: the two chain lanes are fully independent (own engines,
//     own scratch), so the audio thread forks them (see processLanePair);
//   - NAM phases: an oversampled NamEngine holds N independent model
//     instances over disjoint per-phase buffers, so a lane forks them
//     (see NamEngine::process).
// The second fork can happen *inside* a job of the first (a lane job forking
// its NAM block's phases), so the pool supports nesting depth 1. It stays
// deadlock-free by construction: a joiner never parks, it claims and runs
// its own group's unclaimed jobs inline and only spins on jobs another
// thread is actively executing, so every waiter makes progress itself.
//
// The pool is an extension of the audio callback, not a general scheduler:
// workers only ever run strictly inside the audio thread's chainMutex
// critical section, so jobs take no locks and the message-thread mutation
// story is unchanged.
//
// Job protocol (all lock-free, one atomic state per slot):
//
//   Free ──publisher CAS──▶ Building ──▶ Armed ──worker CAS──▶ Claimed ──▶ Done
//                                          │                                │
//                                          └─join() CAS ▶ run inline ─▶ Free ◀┘ (join recycles)
//
// The Armed→Claimed transition is a compare-exchange raced between the
// workers and the publishing thread's join loop: whoever wins runs the job.
// That race is the safety valve. If every worker is descheduled or busy,
// the joiner steals its jobs back and runs them inline, so the callback
// degrades to exactly the serial cost instead of stalling. Slots are pool
// memory (never freed), and a job's context pointer is only dereferenced
// between Claimed and Done, while forkJoin() is still on the publisher's
// stack, so pointing contexts at stack locals is safe.
//
// Scheduling: workers run at realtime priority
// (juce::Thread::startRealtimeThread: time-constraint on macOS, MMCSS "Pro
// Audio" on Windows; on Linux hosts without rtprio privileges the realtime
// start is refused and workers fall back to normal highest-priority
// threads) and, when the host provides one, join the device's audio
// workgroup (os_workgroup on macOS; without it, Apple Silicon parks
// them on efficiency cores and joins miss deadlines). The workgroup arrives
// via AudioProcessor::audioWorkgroupContextChanged and is re-joined from
// each worker's own loop whenever it changes (tokens are thread-affine).
//
// Wakeups are precise, not broadcast: parked workers advertise themselves in
// a bitmask and forkJoin signals only as many of them as it published jobs.
// Broadcasting to a full pool costs more scheduler time than the forked
// work saves (a dozen realtime threads wake, find the one job already
// claimed, park again), enough to cancel the lane fork's speedup outright
// on a 14-core machine. The park/publish race is closed the standard way: a
// worker sets its parked bit and *then* re-checks for armed jobs, while a
// publisher arms jobs and *then* reads the mask (with a seq_cst fence
// pairing the two), so either the worker sees the job or the publisher sees
// the bit. If both sides still miss (the worker was mid-park during an
// earlier fork's mask read), the joiner's steal bounds the damage to one
// serial-cost block, and the 100 ms park timeout is a last-resort re-check.
// The timeout is deliberately long: every timed-out wait is a kernel wakeup
// on a realtime thread, and a full pool of them at 1 ms costs more idle CPU
// than the DSP itself.
//
// Jobs are a plain function pointer + context pointer: the publishing thread
// builds small context structs on its own stack (alive until forkJoin
// returns), so forking allocates nothing.
class RtWorkerPool {
public:
  using JobFn = void (*)(void*);

  /** Slot registry capacity == the largest single fork. Concurrent forks
      share the registry: at the design maximum (one lane fork publishing 1
      job + two nested ×8 phase forks publishing 7 each) 15 slots are in
      flight. A fork that finds the registry full runs the overflow jobs
      inline immediately: still correct, just less parallel. */
  static constexpr int kMaxJobs = 16;

  /** How long a parked worker sleeps before re-checking on its own (see the
      worker loop). Every wake it relies on is signalled explicitly. */
  static constexpr int kParkTimeoutMs = 100;

  RtWorkerPool() = default;
  ~RtWorkerPool() { stop(); }

  /** Start (or restart) the pool for the given callback geometry; sizes the
      realtime scheduling contract. `numWorkers` < 0 picks one worker per
      spare hardware core (capped at kMaxJobs - 1); 0 is allowed and leaves
      forkJoin fully inline. Message thread / prepareToPlay only. */
  void start(double sampleRate, int samplesPerBlock, int numWorkers = -1) {
    stop();
    for (auto& slot : slots)  // defensive: no fork is in flight across start
      slot.state.store(static_cast<int>(SlotState::Free), std::memory_order_relaxed);
    if (numWorkers < 0)
      numWorkers = juce::SystemStats::getNumCpus() - 1;
    numWorkers = juce::jlimit(0, kMaxJobs - 1, numWorkers);
    workers.reserve(static_cast<size_t>(numWorkers));
    // One buffer is the natural per-callback budget, but Mach rejects
    // time-constraint computations much over 50 ms and hosts can run rates
    // that put a buffer far past that (clap-validator probes 1234.5678 Hz:
    // a 512-sample buffer is 415 ms). A rejected policy is worse than an
    // imprecise one: JUCE 9's macOS realtime start then exits the pthread
    // before threadEntryPoint, leaving a stale thread handle that makes the
    // fallback startThread() a silent no-op and every later stopThread()
    // burn its full timeout (see the matching T3K_RT_START_STALE_HANDLE
    // JUCE patch in the root CMakeLists). The budget is a scheduling hint,
    // not a correctness contract, so clamp it to a range the kernel always
    // accepts.
    const double frameMs =
        1000.0 * juce::jmax(1, samplesPerBlock) / juce::jmax(1.0, sampleRate);
    const double budgetMs = juce::jlimit(0.1, 40.0, frameMs);
    for (int i = 0; i < numWorkers; ++i) {
      auto worker = std::make_unique<Worker>(*this, i);
      const bool startedRealtime = worker->startRealtimeThread(
          juce::Thread::RealtimeOptions{}
              .withPriority(10)
              .withMaximumProcessingTimeMs(budgetMs));
      // Linux without rtprio privileges refuses SCHED_RR at pthread_create,
      // so the realtime start fails outright there (macOS and Windows always
      // succeed). A normal-priority worker still parallelizes, and the
      // join-steal path absorbs any scheduling misses, so fall back rather
      // than running with no workers at all.
      if (!startedRealtime)
        worker->startThread(juce::Thread::Priority::highest);
      workers.push_back(std::move(worker));
    }
  }

  /** Stop all workers. Armed-but-unclaimed jobs are abandoned to their
      joiner (which steals and runs them inline); claimed ones are finished
      before the thread exits. Never called on the RT path. */
  void stop() {
    for (auto& worker : workers) {
      worker->signalThreadShouldExit();
      worker->wake.signal();
    }
    for (auto& worker : workers)
      worker->stopThread(2000);
    workers.clear();
  }

  /** True when forkJoin() can actually hand work off. Purely advisory: a
      fork raced by a dying pool still completes via the join-steal path. */
  bool isRunning() const { return aliveWorkers.load(std::memory_order_acquire) > 0; }

  int numWorkers() const { return static_cast<int>(workers.size()); }

  /** Hand the device's audio workgroup over (empty = leave). Safe from any
      thread; each worker joins/leaves from its own loop (tokens are
      thread-affine). */
  void setAudioWorkgroup(juce::AudioWorkgroup newWorkgroup) {
    {
      const juce::SpinLock::ScopedLockType l(workgroupLock);
      if (pendingWorkgroup == newWorkgroup)
        return;
      pendingWorkgroup = std::move(newWorkgroup);
    }
    workgroupEpoch.fetch_add(1, std::memory_order_release);
    for (auto& worker : workers)
      worker->wake.signal();
  }

  /** RT-safe. Run fn(ctxs[i]) for every i in [0, count), potentially in
      parallel, returning once all have completed. The calling thread always
      executes ctxs[0] itself (and any job no worker picks up), so completion
      never depends on worker liveness. Contexts must stay alive until this
      returns. Callable from the audio thread and, nested one level deep,
      from inside a job (a lane job forking its NAM phases). `count` must
      not exceed kMaxJobs. */
  void forkJoin(JobFn fn, void* const* ctxs, int count) {
    jassert(count <= kMaxJobs);
    if (count <= 0)
      return;
    if (count == 1 || !isRunning()) {
      for (int i = 0; i < count; ++i)
        fn(ctxs[i]);
      return;
    }

    // Publish jobs 1..count-1; keep job 0 for this thread. A full registry
    // (deep concurrent nesting) simply runs the overflow inline right away.
    int published[kMaxJobs];
    int numPublished = 0;
    for (int i = 1; i < count; ++i) {
      const int slotIdx = tryPublish(fn, ctxs[i]);
      if (slotIdx >= 0)
        published[numPublished++] = slotIdx;
      else
        fn(ctxs[i]);
    }

    if (numPublished > 0) {
      // Pairs with the workers' seq_cst parked-bit RMW: order the Armed
      // stores above before the mask read inside wakeParkedWorkers, so a
      // parking worker either sees the jobs or we see its bit.
      std::atomic_thread_fence(std::memory_order_seq_cst);
      wakeParkedWorkers(numPublished);
    }

    fn(ctxs[0]);

    // Join: steal still-Armed jobs and run them inline; spin (pause hints)
    // on jobs a worker has claimed. Each finished slot is recycled to Free
    // by this thread only (a slot belongs to its publisher until then), so
    // scanning by remembered index is ABA-safe.
    int remaining = numPublished;
    while (remaining > 0) {
      bool progressed = false;
      for (int k = 0; k < numPublished; ++k) {
        const int slotIdx = published[k];
        if (slotIdx < 0)
          continue;
        auto& slot = slots[static_cast<size_t>(slotIdx)];
        int state = slot.state.load(std::memory_order_acquire);
        if (state == static_cast<int>(SlotState::Armed)) {
          int expected = static_cast<int>(SlotState::Armed);
          if (slot.state.compare_exchange_strong(expected, static_cast<int>(SlotState::Claimed),
                                                 std::memory_order_acquire)) {
            slot.fn(slot.ctx);
            slot.state.store(static_cast<int>(SlotState::Free), std::memory_order_release);
            published[k] = -1;
            --remaining;
            progressed = true;
            continue;
          }
          state = slot.state.load(std::memory_order_acquire);
        }
        if (state == static_cast<int>(SlotState::Done)) {
          slot.state.store(static_cast<int>(SlotState::Free), std::memory_order_release);
          published[k] = -1;
          --remaining;
          progressed = true;
        }
      }
      if (!progressed)
        cpuPause();
    }
  }

private:
  enum class SlotState : int { Free = 0, Building, Armed, Claimed, Done };

  // One cache line per slot: the states are hammered by CAS from every
  // worker, so neighbours must not share a line.
  struct alignas(64) Slot {
    std::atomic<int> state{static_cast<int>(SlotState::Free)};
    JobFn fn = nullptr;
    void* ctx = nullptr;
  };

  static void cpuPause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  }

  /** Claim a Free slot, write the job, arm it. Returns the slot index or -1
      when the registry is full. The Free→Building acquire synchronizes with
      the previous joiner's Free release-store, so the prior claimer's use of
      fn/ctx happened-before this overwrite. */
  int tryPublish(JobFn fn, void* ctx) {
    for (int s = 0; s < kMaxJobs; ++s) {
      auto& slot = slots[static_cast<size_t>(s)];
      int expected = static_cast<int>(SlotState::Free);
      if (slot.state.load(std::memory_order_relaxed) == expected &&
          slot.state.compare_exchange_strong(expected, static_cast<int>(SlotState::Building),
                                             std::memory_order_acquire)) {
        slot.fn = fn;
        slot.ctx = ctx;
        slot.state.store(static_cast<int>(SlotState::Armed), std::memory_order_release);
        return s;
      }
    }
    return -1;
  }

  /** Scan for an Armed slot from any group (workers are shared between
      concurrent forks), claim it, run it. Returns whether a job ran. */
  bool tryRunOneJob() {
    for (auto& slot : slots) {
      if (slot.state.load(std::memory_order_relaxed) != static_cast<int>(SlotState::Armed))
        continue;
      int expected = static_cast<int>(SlotState::Armed);
      if (slot.state.compare_exchange_strong(expected, static_cast<int>(SlotState::Claimed),
                                             std::memory_order_acquire)) {
        slot.fn(slot.ctx);
        slot.state.store(static_cast<int>(SlotState::Done), std::memory_order_release);
        return true;
      }
    }
    return false;
  }

  bool anyArmedJob() const {
    for (const auto& slot : slots)
      if (slot.state.load(std::memory_order_relaxed) == static_cast<int>(SlotState::Armed))
        return true;
    return false;
  }

  /** Signal up to `maxWakes` currently-parked workers (see the parked-mask
      note in the header comment). Busy workers rescan after their current
      job anyway, and the joiner steals whatever nobody claims, so waking
      fewer than `maxWakes` is a throughput hint gone soft, never a stall. */
  void wakeParkedWorkers(int maxWakes) {
    const juce::uint32 parked = parkedMask.load(std::memory_order_relaxed);
    if (parked == 0)
      return;
    for (size_t i = 0; i < workers.size() && maxWakes > 0; ++i) {
      if ((parked & (1u << i)) != 0) {
        workers[i]->wake.signal();
        --maxWakes;
      }
    }
  }

  class Worker : public juce::Thread {
  public:
    Worker(RtWorkerPool& ownerPool, int index)
        : juce::Thread("TONE3000 DSP Worker " + juce::String(index + 1)),
          pool(ownerPool),
          parkedBit(1u << index) {}

    ~Worker() override { stopThread(2000); }

    void run() override {
      // FTZ/DAZ are per-thread CPU flags: without this a worker computes NAM
      // tails in denormal range at ~100× cost while the audio thread doesn't,
      // and phase-parallel output would differ from serial, breaking the
      // bit-identical contract (the audio thread runs with ScopedNoDenormals).
      juce::ScopedNoDenormals noDenormals;

      pool.aliveWorkers.fetch_add(1, std::memory_order_release);

      juce::WorkgroupToken workgroupToken;
      int seenEpoch = -1;

      while (!threadShouldExit()) {
        // Pick up a changed device workgroup (rare: device switches).
        const int epoch = pool.workgroupEpoch.load(std::memory_order_acquire);
        if (epoch != seenEpoch) {
          seenEpoch = epoch;
          juce::AudioWorkgroup wg;
          {
            const juce::SpinLock::ScopedLockType l(pool.workgroupLock);
            wg = pool.pendingWorkgroup;
          }
          if (wg)
            wg.join(workgroupToken);  // re-join handles leaving the old one
          else
            workgroupToken.reset();
        }

        if (pool.tryRunOneJob())
          continue;  // drained one job; rescan immediately, forks come in bursts

        // Park until the next fork: advertise the bit, then re-check for
        // jobs armed while it was still clear (the seq_cst RMW pairs with
        // forkJoin's fence; see the wakeup note in the header comment).
        pool.parkedMask.fetch_or(parkedBit, std::memory_order_seq_cst);
        if (pool.anyArmedJob()) {
          pool.parkedMask.fetch_and(~parkedBit, std::memory_order_relaxed);
          continue;
        }
        // forkJoin, stop() and setAudioWorkgroup() all signal, so job pickup
        // latency is the event wake (~µs), not the timeout. The timeout is
        // only a safety net for a doubly-missed wakeup, and the joiner's
        // steal already bounds that to one serial block, so it can be long.
        // At 1 ms, 13 parked workers spend ~11% of a core in kernel time
        // (13,000 timed-out waits a second) with the plugin idle.
        wake.wait(kParkTimeoutMs);
        pool.parkedMask.fetch_and(~parkedBit, std::memory_order_relaxed);
      }

      pool.parkedMask.fetch_and(~parkedBit, std::memory_order_relaxed);
      pool.aliveWorkers.fetch_sub(1, std::memory_order_release);
    }

    juce::WaitableEvent wake;

  private:
    RtWorkerPool& pool;
    const juce::uint32 parkedBit;
  };

  std::array<Slot, kMaxJobs> slots;
  std::vector<std::unique_ptr<Worker>> workers;
  std::atomic<int> aliveWorkers{0};
  // Bit i set = worker i is parked on its wake event (kMaxJobs - 1 workers
  // max, so 32 bits always suffice). See the wakeup note up top.
  std::atomic<juce::uint32> parkedMask{0};

  // The workgroup handed over by setAudioWorkgroup, consumed by the worker
  // loops. SpinLock because AudioWorkgroup copies aren't atomic; both sides
  // hold it for nanoseconds and never on the audio thread.
  juce::SpinLock workgroupLock;
  juce::AudioWorkgroup pendingWorkgroup;
  std::atomic<int> workgroupEpoch{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RtWorkerPool)
};
