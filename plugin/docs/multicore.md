# Multi-core stereo: the lane worker

In stereo mode the two chain lanes are fully independent: separate engines,
separate scratch buffers, no shared state inside the chain stage. The
multi-core setting (Advanced, on by default, machine-wide) exploits that by
handing one lane to a realtime helper thread while the audio thread
processes the other, then joining. With two heavy NAM lanes the chain stage
approaches the cost of one.

Implementation is `plugin/include/LaneWorker.h` (header-only). The contract
is pinned by `test/src/multicore_tests.cpp`: parallel output is bit-identical
to serial across topologies, host rates, and oversampling factors, and the
worker survives host lifecycle churn.

## Design rules

The worker is an extension of the audio callback, not a general thread pool:

- It only ever runs strictly inside the audio thread's `chainMutex` critical
  section, so it takes no locks and the message-thread mutation story is
  unchanged.
- The job is a plain function pointer plus a context struct on the audio
  thread's stack (alive until `join()` returns). Dispatch allocates nothing.
- Exactly one dispatch may be in flight, always paired with a `join()`.

## Handoff protocol

All lock-free, one atomic state variable:

```
Idle --dispatch()--> Armed --worker CAS--> Claimed --> Done --join()--> Idle
                       |
                       +--join() CAS (worker never woke) --> run inline
```

The Armed-to-Claimed transition is a compare-exchange raced between the
worker and `join()`: whoever wins runs the job. That race is the safety
valve. If the worker is descheduled and never picks the job up, the audio
thread steals it back and runs it inline, so the callback degrades to
exactly the serial cost instead of stalling. Once the worker has claimed,
`join()` spins with CPU pause hints; the lanes are similarly sized, so the
residual wait is short. This is why the toggle is pure scheduling: both
paths run the same code on the same buffers, and the output is
bit-identical either way.

## Scheduling

The thread runs at realtime priority (time-constraint on macOS, MMCSS
"Pro Audio" on Windows). On macOS it also joins the audio device's
workgroup (`os_workgroup`) when the host provides one; without that, Apple
Silicon parks the worker on efficiency cores and the join misses deadlines.
Workgroup tokens are thread-affine, so the worker re-joins from its own
loop whenever the device changes.

One easy-to-miss detail: FTZ/DAZ denormal flags are per-thread CPU state.
The worker sets `ScopedNoDenormals` in its own loop; without it, NAM decay
tails hit denormal range at roughly 100x cost on the worker while the audio
thread runs fine.

## Branch mode

When a chain is branched, the parallel split is the branch lane versus the
trunk's post-tap remainder rather than left versus right. Mono mode never
uses the worker.
