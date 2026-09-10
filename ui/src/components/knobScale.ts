import { formatContentLength } from './WaveformDisplay';

/**
 * Display scales for knobs. Every knob's value is normalized (0..1 range,
 * matching APVTS / chain params); a KnobScale maps that to real units for
 * the drag readout and the double-click text entry, and back again when the
 * user types a value.
 */
export interface KnobScale {
  /** Normalized -> real units (dB, %, ms, ...). */
  toDisplay(norm: number): number;
  /** Real units -> normalized. Caller clamps to the knob's min/max. */
  fromDisplay(display: number): number;
  /** Full readout string, units included (e.g. "-3.2 dB"). */
  format(norm: number): string;
  /** Text-entry prefill (number only, no unit, which is easier to retype). */
  editText(norm: number): string;
}

const makeScale = (
  toDisplay: (n: number) => number,
  fromDisplay: (d: number) => number,
  unit: string,
  decimals: number
): KnobScale => ({
  toDisplay,
  fromDisplay,
  format: (n) => `${toDisplay(n).toFixed(decimals)}${unit ? ` ${unit}` : ''}`,
  editText: (n) => toDisplay(n).toFixed(decimals),
});

/** Straight-line map from normalized 0..1 to [min..max] display units. */
export const linearScale = (min: number, max: number, unit = '', decimals = 1): KnobScale =>
  makeScale(
    (n) => min + n * (max - min),
    (d) => (d - min) / (max - min),
    unit,
    decimals
  );

/** 0..1 -> 0..100%. Default for knobs that don't declare a scale. */
export const percentScale: KnobScale = makeScale(
  (n) => n * 100,
  (d) => d / 100,
  '%',
  0
);

/** Main/per-block gain: normalized 0.5 = unity, full range ±24 dB.
    Note: IR blocks read the same ±24 dB on their Out knob, but the DSP bakes
    in an extra -18 dB (IR files are typically peak-normalized to 0 dBFS, far
    too hot at unity); see irOffsetDb in Processor.cpp. The knob deliberately
    shows relative dB (0 at center) to keep it simple. */
export const gainDbScale = linearScale(-24, 24, 'dB', 1);

/** Stereo balance trim: 0.5 = centered, ±12 dB per channel at the ends. */
export const balanceDbScale = linearScale(-12, 12, 'dB', 1);

/** Gate threshold: normalized spans -100..0 dB. */
export const gateDbScale = linearScale(-100, 0, 'dB', 0);

/** Per-block IR predelay: normalized 0..1 -> 0-1000ms, applied before the
    wet signal reaches the convolver (see BlockPredelay.h). */
export const predelayMsScale = linearScale(0, 1000, 'ms', 0);

/** Per-block IR content length: normalized 0..1 against the block's own
    detected content (BlockParams.length / irContentLengthMs), from a small
    floor up to the full detected length - mirrors the floor/lerp
    prepareIrShapeRebuild (ProcessorModelLoader.cpp) uses closely enough for
    a live readout (exact sample rounding is native's job, not the knob's).
    Parameterized per block (unlike this file's other scales, the max varies
    with what's loaded), same shape as sidedMsScale/panScale. `format` reuses
    WaveformDisplay's formatContentLength so the knob readout and the
    waveform's own corner label always agree. */
export const lengthMsScale = (contentLengthMs: number): KnobScale => {
  const minMs = 5;
  const maxMs = Math.max(minMs, contentLengthMs);
  const toDisplay = (n: number) => minMs + n * (maxMs - minMs);
  const fromDisplay = (d: number) => (maxMs > minMs ? (d - minMs) / (maxMs - minMs) : 0);
  return {
    toDisplay,
    fromDisplay,
    format: (n) => formatContentLength(toDisplay(n)) ?? '0ms',
    editText: (n) => toDisplay(n).toFixed(0),
  };
};

/** Per-block IR Decay/Attack envelope shape: normalized 0..1, 0.5 (default)
    is a linear ramp, sweeping toward -1 (log: fast-then-level) below and +1
    (exp: holds-then-drops) above. Purely a display convention, plain signed
    number, no unit - the actual power-curve formula (see
    decayEnvelope.ts's DECAY_CURVE_MAX, mirroring prepareIrShapeRebuild's
    kCurveMax in ProcessorModelLoader.cpp) operates on the normalized value
    directly, not on this -1..+1 number. Shared by both the Attack and Decay
    segments' own Curve knob. */
export const curveScale: KnobScale = linearScale(-1, 1, '', 2);

/** Per-block IR Attack Length (the envelope's peak position): normalized
    0..1 against Decay Length's current real-ms value (BlockParams.
    attackLength is a fraction *of* the total Decay Length sets, not an
    independent length - totalMs is computed by the caller since it depends
    on the live Decay Length knob) - mirrors lengthMsScale's own floor/lerp
    shape and reuses its formatContentLength readout, just against a dynamic
    (not fixed) max that moves with Decay Length. */
export const attackLengthMsScale = (totalMs: number): KnobScale => {
  const minMs = 5;
  const maxMs = Math.max(minMs, totalMs);
  const toDisplay = (n: number) => minMs + n * (maxMs - minMs);
  const fromDisplay = (d: number) => (maxMs > minMs ? (d - minMs) / (maxMs - minMs) : 0);
  return {
    toDisplay,
    fromDisplay,
    format: (n) => formatContentLength(toDisplay(n)) ?? '0ms',
    editText: (n) => toDisplay(n).toFixed(0),
  };
};

/** Faceplate tone stack knobs: 0..10, 5 = flat. */
export const toneScale = linearScale(0, 10, '', 1);

/** Bipolar one-sided delay: center = 0 ms, ends reach ±maxMs. Display shows
    the magnitude plus the delayed side ("15.0 ms R"). */
const sidedMsScale = (maxMs: number): KnobScale => {
  const span = 2 * maxMs;
  return {
    toDisplay: (n) => (n - 0.5) * span,
    fromDisplay: (d) => 0.5 + d / span,
    format: (n) => {
      const ms = (n - 0.5) * span;
      if (Math.abs(ms) < 0.05) return '0 ms';
      return `${Math.abs(ms).toFixed(1)} ms ${ms < 0 ? 'L' : 'R'}`;
    },
    editText: (n) => ((n - 0.5) * span).toFixed(1),
  };
};

/** Offset (bipolar), shared by the mono-mode Spread lag and the stereo-mode
    Align delay: center = 0 ms, ends reach 24 ms toward L or R. */
export const offsetMsScale = sidedMsScale(24);

/** Deck crossover cutoff (spread + align advanced panels): log map over
    32.5-520 Hz, center = 130 Hz (mirrors the native deckCrossoverHz). */
export const crossoverHzScale: KnobScale = {
  toDisplay: (n) => 32.5 * Math.pow(16, n),
  fromDisplay: (d) => Math.log(d / 32.5) / Math.log(16),
  format: (n) => `${Math.round(32.5 * Math.pow(16, n))} Hz`,
  editText: (n) => Math.round(32.5 * Math.pow(16, n)).toString(),
};

/**
 * Chain pan halves. The left knob covers normalized 0..0.5 (hard left ..
 * center), the right 0.5..1 (center .. hard right). Display is the pan
 * amount toward the side, 100 = hard, 0 = center.
 */
export const panScale = (side: 'left' | 'right'): KnobScale => {
  const toDisplay = (n: number) => (side === 'left' ? (0.5 - n) * 200 : (n - 0.5) * 200);
  const fromDisplay = (d: number) => (side === 'left' ? 0.5 - d / 200 : 0.5 + d / 200);
  return {
    toDisplay,
    fromDisplay,
    format: (n) => {
      const amount = Math.round(toDisplay(n));
      return amount === 0 ? 'C' : `${amount}${side === 'left' ? 'L' : 'R'}`;
    },
    editText: (n) => Math.round(toDisplay(n)).toString(),
  };
};
