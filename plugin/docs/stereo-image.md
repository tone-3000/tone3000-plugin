# Stereo image: Spread and Align

The plate's stereo-image slot holds one of two features, keyed to the chain
mode: **Spread** (mono mode) builds a stereo image from the single chain,
**Align** (stereo mode) time-aligns the two chains. They are independent
features with separate parameters and lifecycles, but they share one "deck"
of advanced sections (wobble, crossover, diffusion, correlation meter) so
the two faces of the slot sound and read alike.

Implementation:

- Spread: `plugin/include/Spread.h` / `plugin/src/Spread.cpp`
- Align: `plugin/include/StereoOffset.h` / `plugin/src/StereoOffset.cpp`
- Shared deck primitives: `plugin/include/ImageDeck.h`
- Tests: `test/src/spread_tests.cpp`

## Spread (mono chain mode)

Spread turns the mono chain output into a stereo image the way an engineer
would with automatic double tracking (ADT): one side gets the performance,
the other gets a slightly late, subtly drifting copy. The drift is the whole
trick. A static delay reads as one guitar through a comb filter; a delay
that wanders by a few tenths of a millisecond reads as a second take.

```
mono chain out
      |
      +-- LR4 crossover (default 130 Hz, 32.5-520 Hz, bypassable)
      |         |
      |     low band  ----------------------> both channels, untouched
      |         |
      |     high band --+-------------------> dry channel
      |                 |
      |                 +--> lag deck ------> lagged channel
      |
lag deck = fractional delay (4-point Lagrange, wobbling)
         + 6-stage allpass diffusion cascade (300 Hz - 6 kHz, log-spaced,
           bypassable)
         + 1.5 dB precedence trim
```

Design choices, and why:

- **Crossover, default 130 Hz.** The low band feeds both channels
  identically, so the low E string and everything below it stays dual-mono.
  Mono compatibility problems live in the lows; this makes them impossible
  by construction. The cutoff is adjustable 32.5-520 Hz on a log map whose
  center is exactly the 130 Hz default (4x per half turn), and the section
  can be switched off entirely for full-band doubling: maximum width, mono
  safety traded away knowingly.
- **Wobbling fractional delay.** The delay time is modulated by a random
  walk: white noise through two cascaded 0.3 Hz one-poles. One pole is not
  enough; its 6 dB/oct tail leaves audible noise variance above 20 Hz, which
  FMs the lag channel into broadband fizz (pinned by
  `SpreadTest.WobbleAddsNoBroadbandFizz`). Interpolation is 4-point Lagrange
  because linear interpolation under a time-varying fractional offset
  low-passes in rhythm with the wobble.
- **Diffusion cascade.** Six first-order allpasses with fixed, log-spaced
  corner frequencies decorrelate phase without touching magnitude. The
  coefficients are static on purpose: movement comes from the delay wobble,
  and modulating allpass coefficients would reintroduce phasiness. The
  approach follows the allpass decorrelators evaluated in O. Das, "An
  Open-Source Stereo Widening Plugin", Proc. 27th Int. Conf. on Digital
  Audio Effects (DAFx24), Guildford, UK, 2024
  ([paper](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf)).
- **+1.5 dB precedence trim.** The precedence effect pulls the image toward
  the earlier (dry) side; a small level bump on the lag side patches most of
  that pull. It is a patch, not a cure, which is also why the offset knob's
  sign "points at the fake one".

Engage/bypass is a ~25 ms equal-gain crossfade between the untouched input
and the doubled image. There is no wet/dry mix: while spread is on, the deck
runs at full strength and Offset is the only musical dimension.

## Align (stereo chain mode)

Align applies a short corrective delay (up to 24 ms, sub-sample precise) to
one chain, for chains that land a few ms apart: captures of the same
performance, or NAM models / IRs with different baked-in latency. The
auto-align button measures the inter-chain lag with an internal sweep and
writes the correction into the offset (see `AutoOffset.h`; the probe
method, applied per model and validated against trainer metadata and real
DI, is written up in [`model-latency.md`](model-latency.md)).

On top of the corrective delay sits the same deck as Spread's, with two
deliberate differences (rationale in `StereoOffset.h`):

- **No precedence trim.** These are two real chains; a corrective tool must
  not color levels (`StereoOffsetTest.DiffusePreservesMagnitudeWithNoTrim`).
- **The crossover splits both channels**, so both sides get the identical
  LR4 phase rotation and the inter-chain alignment (the whole point of the
  engine) is preserved. Flip side: with the crossover on, lows are no longer
  corrected by the delay; it is a creative width control, not part of the
  corrective path.

Every Align transition (power, side swap, knob through center) glides the
delay to zero and blends the deck out first, so it passes through identity
and never clicks; the interpolator-tap clamp that protects the first samples
after a delay-line clear is documented in `StereoOffset.h`.

## Controls

Each feature shows one musical control on the plate, plus an advanced panel
(right-click the group) with the shared deck sections:

- **Offset** (bipolar, ±24 ms): the sign picks which channel is delayed, the
  magnitude sets the delay. In Spread the signed value is smoothed by a
  ~100 ms one-pole (sweeps read as a tape-style varispeed glide) and below
  1 ms the lag path blends back to the dry high band, so the center detent
  is exactly dual-mono. In Align the detent window is far tighter (~1 sample
  at 48 kHz): auto-align corrections of a few samples must not vanish into
  the detent. Alt-click resets the offset and the whole deck.
- **Wobble** (0-100% + power): depth of the random-walk drift, up to ±1.2 ms
  around the dialed offset. That is roughly ±2-4 cents of continuous pitch
  wander (pitch shift is the derivative of delay time). Depth is absolute,
  not relative to the offset, so a small offset can still carry a full-depth
  wobble. The power switch folds to zero depth through the depth smoother,
  so it never clicks.
- **Crossover** (32.5-520 Hz + power, default 130 Hz): the cutoff below
  which lows skip the deck. Off treats the full band.
- **Diffuse** (power only): the phase-diffusion cascade. Off leaves the
  delayed side a pure delay: more coherent, more comb-like on a mono sum.

Spread's three sections default **on**: the deck is its core sound, and the
panel just exposes what was always running. Align's default **off**: it
stays a pure corrective tool until the deck is asked for, at which point the
delayed chain reads as an ADT-style second take.

The section switches are ~25 ms blends rather than hard toggles (both
endpoints are magnitude-flat but differ in phase, so an instant switch would
step the waveform), and the bypassed filters keep running so re-engaging
lands on warm state.

## Mono outputs

A rig that can't reproduce stereo (a mono host bus, or a standalone output
device with a single channel; the latter still hands the processor a stereo
buffer but plays only channel 0) is reported to the UI as the `stereoOutput`
capability flag in `getChainState` (the output-side twin of `stereoInput`).
The two features respond differently, because only one of them survives a
mono fold:

**Spread** needs two output channels to mean anything, so the processor
keeps the deck idle regardless of the `spreadEnabled` parameter: the output
is the plain mono chain, never a summed or half-heard double. The parameter
keeps its value, so a preset saved with spread on round-trips intact and
comes alive again on a stereo rig. The UI greys the group out. Pinned by
`ProcessorTest.SpreadStaysIdleWithoutAStereoOutput`.

**Stereo chains** stay fully alive: both lanes process (on a mono host
buffer the Right lane runs on the scratch channel), and the image matrix
folds them to mono at the very end, `½(balL·L + balR·R)`: the same fold a
host applies when it sums a stereo bus down to mono at the default hard
pans. The ½ keeps levels consistent: a rig moved between a stereo and a
mono track doesn't jump, and one chain duplicated into both lanes sums back
to exactly its mono-mode level. Balance, solo and polarity keep working
inside the sum (a blend control, in effect); the pans are inert and the UI
dims them, showing a MONO chip on the pan rail. Align keeps running too
(time-aligning two chains matters most right before they are summed), so
auto-align and the correlation LED stay meaningful. Pinned by
`ProcessorTest.StereoChainsFoldToMonoWithoutAStereoOutput`.

## Mono safety

Both panels carry a correlation LED fed by a ~300 ms running normalized L/R
cross-correlation of the feature's output (`DeckCorrelation`, published
through an atomic for the UI). Below 0.5 a mono fold-down audibly thins;
below 0 it actively cancels. The wobble also keeps the mono-sum comb moving,
so fold-down reads as gentle chorus rather than a stationary notch.

## Threading

Both engines run `process()` on the audio thread only and allocate nothing
after `prepare()`. The correlation atomic is the single cross-thread output.
