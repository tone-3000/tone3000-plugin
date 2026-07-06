# Stereo Support Plan

Branch: `stereo`

Two related features:

1. **Mono-mode stereo IR** — when a stereo IR block has **no NAM block downstream** and the
   host buffer is stereo (L+R both in use), convolve true-stereo (audio L → IR ch0, audio R →
   IR ch1). Otherwise keep collapsing to the IR's left channel (today's behavior).
2. **Stereo mode** — an optional mode where the user builds **two independent chains** (Left /
   Right). Left chain processes channel 0, Right chain processes channel 1. In stereo mode IRs
   are always applied **mono** (IR ch0) within their single-channel path.

A new control strip at the top of the chain enables stereo mode and toggles which chain
(Left / Right) is being edited.

---

## Why this design

- A mono NAM block (1-in/1-out) collapses stereo: it reads ch0, runs the model, copies the
  result to ch1. So a stereo IR placed **before** a NAM is pointless — the stereo image is
  discarded immediately after. Hence the "no NAM after it" condition for true-stereo IR in
  mono mode.
- Stereo mode sidesteps the collapse entirely: each side is a self-contained mono path
  (`mono in → NAM → NAM → IR(mono)`), merged only at the output. Stereo width comes from
  using different models/IRs per side, not from a 2-channel IR file.

---

## JUCE Convolution behavior (verified against juce_Convolution.cpp)

A single `juce::dsp::Convolution` prepared for 2 channels:

- Loaded with `Stereo::no` → IR is 1 channel; the engine applies that **same kernel
  independently to every input channel** (`MultichannelEngine` clamps the IR channel index to
  `jmin(irChannels-1, channel)`). This is exactly the "use IR left channel on both" fallback.
- Loaded with `Stereo::yes` → IR keeps up to 2 channels; audio ch0 convolves with IR ch0,
  audio ch1 with IR ch1. This is true-stereo IR.

So we hold up to two convolvers per IR block:

- `convolverMono` — always present for a loaded IR (`Stereo::no`).
- `convolverStereo` — present **only when the IR file has ≥2 channels** (`Stereo::yes`).

The choice between them is made **per audio block at runtime**, so reordering/adding a NAM
after the IR changes behavior without reloading.

---

## Data model changes

### `ChainBlock` (plugin/include/ChainBlock.h)
- Replace `convolverLeft` / `convolverRight` with:
  - `std::unique_ptr<juce::dsp::Convolution> convolverMono;`
  - `std::unique_ptr<juce::dsp::Convolution> convolverStereo;`
  - `int irNumChannels{1};`
- Add `enum class ChainSide { Left, Right };` (shared with Processor).

### `PreparedBlockModel` (plugin/include/Processor.h)
- Same convolver fields + `irNumChannels`.

### `TONE3000Processor` (plugin/include/Processor.h)
- `std::vector<std::unique_ptr<ChainBlock>> rightChainBlocks;` (left stays `chainBlocks`)
- `std::atomic<bool> stereoEnabled{false};`
- `ChainSide activeEditSide{ChainSide::Left};`
- Pre-allocated mono scratch buffers `stereoChainBufferL/R` (1 ch, maxBlockSize).
- New methods: `processChainOnBuffer(...)`, `setStereoMode(bool)`, `setActiveEditChain(...)`,
  `activeChain()`, `prepareChain(...)`.

---

## Processing (plugin/src/Processor.cpp)

`processBlock`:
1. Input gain + noise gate on the full buffer (unchanged, shared).
2. Chain section (under `chainMutex`):
   - **Stereo mode** (`stereoEnabled && numChannels >= 2`): copy ch0→`stereoChainBufferL`,
     ch1→`stereoChainBufferR`; run `processChainOnBuffer(chainBlocks, L)` and
     `processChainOnBuffer(rightChainBlocks, R)`; copy results back to ch0/ch1.
   - **Mono mode**: `processChainOnBuffer(chainBlocks, buffer)`.
3. DC blocker → EQ → output gain → metering (unchanged, shared on full buffer).

`processChainOnBuffer(blocks, buffer)` is the existing per-block loop, generalized to operate
on whatever buffer it's handed (1 channel in stereo mode, 1–2 in mono mode). Everything is
keyed on `buffer.getNumChannels()`, which already guards channel-1 work.

IR section inside the loop:
- Precompute `lastNamIndex` = highest index of an enabled+loaded NAM block in `blocks`.
- `noNamAfter = (idx > lastNamIndex)`.
- `useStereoIr = irNumChannels > 1 && buffer.getNumChannels() > 1 && noNamAfter &&
  convolverStereo != nullptr`.
- Process the whole `AudioBlock` in one call with the chosen convolver.

Latency: `calculateTotalLatency` sums NAM latency per chain; in stereo mode the plugin latency
is `max(leftLatency, rightLatency)`.

---

## Chain management (plugin/src/ProcessorChain.cpp)

- `activeChain()` returns `rightChainBlocks` when `stereoEnabled && activeEditSide == Right`,
  else `chainBlocks`.
- Add/`loadTone`, `reorderChainBlocks`, `getChainStatus` operate on `activeChain()`.
- `removeChainBlock`, `switchModel`, `setBlock*`, and background loaders find a block by id
  across **both** chains (ids are globally unique).
- Each chain owns its own INSERT placeholder. Left = `select-insert`, Right =
  `select-insert-right`. INSERT detection uses the block **type**, not the id.
- `setStereoMode(bool)`: toggles `stereoEnabled`; lazily seeds the right chain's insert block;
  prepares right-chain engines; updates latency.
- `setActiveEditChain(side)`: sets `activeEditSide`.
- `getChainStatus()` adds `stereoEnabled` and `activeSide` to its result.

---

## State (plugin/src/ProcessorState.cpp)

- Serialize a `stereoEnabled` flag on the root state.
- Keep `ChainBlocks` as the left/primary chain (**backward compatible** — old presets load into
  the left chain). Add a `RightChainBlocks` tree.
- Factor block (de)serialization into helpers used for both chains.

---

## Native bridge (plugin/src/EditorWebViewSetup.cpp)

Add native functions: `setStereoMode(bool)`, `setActiveEditChain("left"|"right")`.

---

## UI (ui/src)

- `types/tone.ts`: chain status gains `stereoEnabled: boolean` and `activeSide: 'left'|'right'`.
- New `StereoControls` strip above `ChainView`: a Stereo on/off toggle and a Left/Right segmented
  switcher (visible only when stereo is on).
- `Plugin.tsx`: track `stereoEnabled` / `activeSide`, wire `setStereoMode` /
  `setActiveEditChain`, reload chain after toggles.

---

## Scope notes / future work

- Stereo mode splits host ch0→Left, ch1→Right. A literal mono track has only one channel, so
  stereo mode is inactive there (the plugin's bus layout forces in==out). "Dual-mono from one
  source" works by feeding the same signal to both channels upstream; an explicit dual-mono
  input toggle is deferred.
- Shared front/back end (input gain, gate, EQ, output) stays global across both chains.
- Switching an IR between mono/stereo convolvers (by adding/removing a downstream NAM) can
  cause a small discontinuity; crossfade smoothing is deferred.

---

## Execution checklist

- [x] ChainBlock.h: convolver fields + `irNumChannels` + `ChainSide`
- [x] Processor.h: members, PreparedBlockModel, method decls
- [x] ProcessorModelLoader.cpp: stereo-aware IR load + apply
- [x] Processor.cpp: prepareToPlay (both chains + scratch), processChainOnBuffer, processBlock branch, latency
- [x] ProcessorChain.cpp: active chain, cross-chain find, stereo toggles, status
- [x] ProcessorState.cpp: persist stereo + both chains
- [x] EditorWebViewSetup.cpp: native functions
- [x] UI: types, StereoControls, Plugin wiring
- [x] Build / lint — `make TONE3000` + `TONE3000_Standalone` link clean; `tsc -b` passes

## Build notes

- The packaged `WebAssets` BinaryData is generated from the UI `dist/` bundle. To see the new
  Stereo UI in a built plugin, rebuild the UI (`cd ui && npm run build`) before building the
  plugin target.
