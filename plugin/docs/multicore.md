# Multi-core processing: the realtime worker pool

The multi-core setting (Plugin Settings, on by default, machine-wide)
spreads the chain stage's independent work across a pool of realtime helper
threads. Two sections fork:

- **Stereo lanes.** The two chain lanes are fully independent: separate
  engines, separate scratch buffers, no shared state inside the chain stage.
  The audio thread hands one lane to a worker, processes the other itself,
  and joins. With two heavy NAM lanes the chain stage approaches the cost of
  one.
- **NAM oversampling phases.** An oversampled `NamEngine` holds N
  independent instances of the same model over disjoint per-phase buffers
  (see `oversampling.md`). The lane processing the block forks the N phase
  jobs across the pool instead of looping them, so an 8x model costs about
  one native-rate model per core instead of eight on one. This fork nests
  inside a lane fork when both apply, and unlike the lane fork it doesn't
  need stereo: a mono chain's oversampled NAM blocks fork too.
- **Dual-mono voices.** In Dual Mono input mode a mono chain's `NamEngine`
  holds two voices (one per input channel), each with its own full set of
  phase instances. The engine forks the flat voice × phase set as one group
  (at most 2 × 8 = 16 jobs, the pool's `kMaxJobs`), so a dual-mono 8x
  model still costs about one native-rate model per core. The voices are
  as independent as the phases (separate models, separate buffers), and
  `MultiCoreTest.DualMonoParallelMatchesSerialBitExact` pins the output as
  bit-identical to the serial loop at every factor.

Implementation is `plugin/include/RtWorkerPool.h` (header-only). The
contract is pinned by `test/src/worker_pool_tests.cpp` (the scheduling
primitive: every job exactly once, nesting, degradation, churn) and
`test/src/multicore_tests.cpp` (the audio: parallel output is bit-identical
to serial across topologies, host rates, oversampling factors and mid-stream
toggles, and the pool survives host lifecycle churn).

## Design rules

The pool is an extension of the audio callback, not a general scheduler:

- Workers only ever run strictly inside the audio thread's `chainMutex`
  critical section, so jobs take no locks and the message-thread mutation
  story is unchanged.
- A job is a plain function pointer plus a context struct on the forking
  thread's stack (alive until `forkJoin()` returns). Forking allocates
  nothing.
- `forkJoin(fn, ctxs, count)` runs every job exactly once and returns only
  when all are complete. The caller always executes job 0 itself, so
  completion never depends on worker liveness.
- Nesting is supported one level deep (a lane job forking its NAM block's
  phases), and stays deadlock-free by construction: a joiner never parks, it
  claims and runs its own group's unclaimed jobs inline and only spins on
  jobs another thread is actively executing, so every waiter makes progress
  itself.

## Job protocol

All lock-free, one atomic state per slot in a fixed registry (slots are pool
memory, never freed; contexts on caller stacks are only dereferenced while
the owning `forkJoin` is still on that stack):

```
Free --publisher CAS--> Building --> Armed --worker CAS--> Claimed --> Done
                                       |                                |
                                       +--join CAS -> run inline -> Free <-+ (join recycles)
```

The Armed-to-Claimed transition is a compare-exchange raced between the
workers and the publisher's join loop: whoever wins runs the job. That race
is the safety valve. If every worker is descheduled or busy, the joiner
steals its jobs back and runs them inline, so the callback degrades to
exactly the serial cost instead of stalling. A full registry (deep
concurrent nesting) makes `forkJoin` run the overflow inline immediately:
less parallel, never blocked. This is why the toggle is pure scheduling:
every schedule runs the same code on the same buffers, and the output is
bit-identical regardless.

## Scheduling and wakeups

The threads run at realtime priority (time-constraint on macOS, MMCSS
"Pro Audio" on Windows); the pool sizes itself to one worker per spare
hardware core. Linux refuses realtime scheduling to processes without
rtprio privileges, so there the workers fall back to normal
highest-priority threads: still parallel, and the join steal absorbs any
scheduling misses. On macOS workers also join the audio device's workgroup
(`os_workgroup`) when the host provides one; without that, Apple Silicon
parks them on efficiency cores and joins miss deadlines. Workgroup tokens
are thread-affine, so each worker re-joins from its own loop whenever the
device changes.

Wakeups are precise, not broadcast: parked workers advertise themselves in
a bitmask and `forkJoin` signals only as many of them as it published jobs.
Broadcasting to the full pool costs more scheduler time than the forked
work saves (a dozen realtime threads wake to find the one job already
claimed), enough to cancel the lane fork's speedup outright on a 14-core
machine. The park/publish race is closed with a seq_cst fence pairing
(worker: set bit, then re-check for armed jobs; publisher: arm jobs, then
read the mask), and the join steal bounds a doubly-missed wakeup to one
serial-cost block. Parked workers also re-check on a 100 ms timeout as a
last resort. That timeout was 1 ms originally, and with every wake
signalled explicitly it only added kernel time: 13 parked workers timing
out 1,000 times a second cost about 11% of a core while the plugin was
idle, more than the neural net itself. Profiled on a 14-core M-series Mac
with `ps -M`: the worker threads showed ~0.8% system time each and ~0.1%
user time; at 100 ms they show 0.0%.

One easy-to-miss detail: FTZ/DAZ denormal flags are per-thread CPU state.
Each worker sets `ScopedNoDenormals` in its own loop; without it, NAM decay
tails hit denormal range at roughly 100x cost on workers, and the phase
fork would break bit-identity with the serial loop, which runs under the
audio thread's flags.

## Failure containment

Exceptions can't cross threads, so a NAM phase job traps its own and the
joining thread rethrows once every phase has completed
(`NamEngine::process`). The block's existing realtime failure path (disable
it, flag it, log from the message thread) then works exactly as it does for
a serial throw.

## Branch mode

When a chain is branched, the lane-level parallel split is the branch lane
versus the trunk's post-tap remainder rather than left versus right. Mono
mode never forks lanes; its oversampled NAM blocks still fork their phases.
