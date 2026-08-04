# Spread: the mono-to-stereo doubler

Spread turns the mono chain output into a stereo image the way an engineer
would with automatic double tracking (ADT): one side gets the performance,
the other gets a slightly late, subtly drifting copy. The drift is the whole
trick. A static delay reads as one guitar through a comb filter; a delay that
wanders by a few tenths of a millisecond reads as a second take.

Implementation lives in `plugin/include/Spread.h` / `plugin/src/Spread.cpp`.
Test coverage is in `test/src/spread_tests.cpp`.

## Signal path

```
mono chain out
      |
      +-- LR4 crossover @ 130 Hz
      |         |
      |     low band  ----------------------> both channels, untouched
      |         |
      |     high band --+-------------------> dry channel
      |                 |
      |                 +--> lag deck ------> lagged channel
      |
lag deck = fractional delay (4-point Lagrange, wobbling)
         + 6-stage allpass decorrelation cascade (300 Hz - 6 kHz, log-spaced)
         + 1.5 dB precedence trim
```

Design choices, and why:

- **130 Hz crossover.** The low band feeds both channels identically, so the
  low E string and everything below it stays dual-mono. Mono compatibility
  problems live in the lows; this makes them impossible by construction.
- **Wobbling fractional delay.** The delay time is modulated by a random
  walk: white noise through two cascaded 0.3 Hz one-poles. One pole is not
  enough; its 6 dB/oct tail leaves audible noise variance above 20 Hz, which
  FMs the lag channel into broadband fizz (pinned by
  `SpreadTest.WobbleAddsNoBroadbandFizz`). Interpolation is 4-point Lagrange
  because linear interpolation under a time-varying fractional offset
  low-passes in rhythm with the wobble.
- **Allpass cascade.** Six first-order allpasses with fixed, log-spaced
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

## Controls

There is exactly one musical control, plus one hidden refinement:

- **Offset** (bipolar, +-24 ms): the sign picks which channel lags, the
  magnitude sets the base delay. The signed value is smoothed by a ~100 ms
  one-pole, so sweeping through center passes cleanly through identity and
  knob moves read as a tape-style varispeed glide. Below 1 ms of |offset|
  the lag path blends back to the dry high band, making the center detent
  exactly dual-mono while sweeps stay continuous.
- **Wobble** (0-100%, right-click the offset knob for the advanced panel):
  depth of the random-walk drift, up to +-1.2 ms around the dialed offset.
  That is roughly +-2-4 cents of continuous pitch wander (pitch shift is the
  derivative of delay time). Depth is absolute, not relative to the offset,
  so a small offset can still carry a full-depth wobble.

Engage/bypass is a ~25 ms equal-gain crossfade between the untouched input
and the doubled image. There is no wet/dry mix: while spread is on, the deck
runs at full strength and Offset is the only musical dimension.

## Mono safety

The advanced panel carries a correlation LED fed by a ~300 ms running
normalized L/R cross-correlation of the spread output (computed in
`process()`, published through an atomic for the UI). Below 0.5 a mono
fold-down audibly thins; below 0 it actively cancels. The wobble also keeps
the mono-sum comb moving, so fold-down reads as gentle chorus rather than a
stationary notch.

## Threading

`process()` runs on the audio thread only and allocates nothing after
`prepare()`. The correlation atomic is the single cross-thread output.
