# Oversampling: the chain rate raiser

The Advanced "oversampling" setting runs the whole tone chain at 2x, 4x, or
8x the 48 kHz base rate. The point is aliasing: NAM models are nonlinear, so
they generate harmonics, and any harmonic above Nyquist folds back into the
audible band as an inharmonic artifact. Raising the rate gives those
harmonics somewhere legal to land; the decimation filter then removes
everything above the base Nyquist on the way back down.

Implementation lives in `plugin/include/ChainOversampler.h` (header-only)
and `plugin/include/NamEngine.h` / `plugin/src/NamEngine.cpp`. Coverage is
in `test/src/dsp_tests.cpp` (null, transparency, aliasing, interleaving
exactness, island equivalence).

## The filter

Each doubling is one polyphase half-band stage built from two cascades of
six first-order allpass sections each (the classic HIIR structure). Design
properties, and why they matter here:

- **Minimum-phase, zero reported latency.** Group delay is a fraction of a
  sample across the audible band, so toggling oversampling never changes
  the plugin's PDC (pinned by `ProcessorTest.OversamplingTogglesWithoutPdcChange`).
  Linear-phase FIR half-bands would add latency and cost more.
- **Cheap.** 24 multiply-adds per branch pair per sample, with >90 dB
  stopband rejection.
- **Fixed, not user-selectable.** The phase warp near the base Nyquist is
  inaudible; offering filter choices would be a knob without a sound.

Coefficients are adapted from
[DLC86/NAM-Oversampler](https://github.com/DLC86/NAM-Oversampler)'s
AudioDSPTools fork (MIT), following the published HIIR half-band allpass
tables. That project pioneered oversampled NAM processing, and this design
follows its lead in several places (including the time-scaled handling of
LSTM models noted below).

## Phase-interleaved NAM

Running a WaveNet/ConvNet model at N times its native rate normally requires
scaling its convolution dilations by N. We don't patch NeuralAmpModelerCore;
instead we use an identity: an oversampled model with scaled dilations is
mathematically equal to N independent copies of the unscaled model, each
processing every Nth sample of the oversampled stream. Every scaled dilation
lands its taps exactly N samples apart (within one phase), and the 1x1
convolutions and activations are per-sample, so nothing crosses phases.

`NamEngine` therefore holds N instances of the same model and interleaves
them. The receptive field stays constant in seconds, so the model sounds the
same; only the alias behavior improves. `ChainOversamplerTest.PhaseInterleavedDilatedConvIsExact`
pins the identity sample-exactly.

LSTM models update state on consecutive samples and can't be phase-split;
they get a single instance run time-scaled at the full chain rate.

## IR islands

Convolution is linear: it creates no harmonics, so oversampling an IR buys
nothing and costs roughly quadratically (kernel length and rate both scale
with the factor). Each IR block therefore runs inside a "base-rate island":
`processBaseRateIsland()` decimates the oversampled stream down to 48 kHz,
convolves there, and interpolates back. IR CPU and sound are identical at
every factor (`IrConvolutionTest.IslandedConvolutionInOversampledChainMatchesBaseRate`).

## Frame-count contract

Every base-rate input frame becomes exactly `factor` chain frames, and the
decimator never carries a partial pair across blocks. This is what lets the
phase-interleaved engine assume buffers divisible by the factor, and it is
asserted rather than handled: a violation is a bug upstream, not a runtime
condition.
