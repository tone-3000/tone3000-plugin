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
  /** Text-entry prefill (number only, no unit — easier to retype). */
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
    too hot at unity) — see irOffsetDb in Processor.cpp. The knob deliberately
    shows relative dB (0 at center) to keep it simple. */
export const gainDbScale = linearScale(-24, 24, 'dB', 1);

/** Stereo balance trim: 0.5 = centered, ±12 dB per channel at the ends. */
export const balanceDbScale = linearScale(-12, 12, 'dB', 1);

/** Gate threshold: normalized spans -100..0 dB. */
export const gateDbScale = linearScale(-100, 0, 'dB', 0);

/** Faceplate tone stack: parameter range 0.01..10, shown as 0..10. */
export const toneScale = linearScale(0.01, 10, '', 1);

/** Spread offset (bipolar): center = 0 ms, ends delay L/R by 24 ms. */
export const offsetMsScale: KnobScale = {
  toDisplay: (n) => (n - 0.5) * 48,
  fromDisplay: (d) => 0.5 + d / 48,
  format: (n) => {
    const ms = (n - 0.5) * 48;
    if (Math.abs(ms) < 0.05) return '0 ms';
    return `${Math.abs(ms).toFixed(1)} ms ${ms < 0 ? 'L' : 'R'}`;
  },
  editText: (n) => ((n - 0.5) * 48).toFixed(1),
};

/** Spread jitter: 0..4 ms. */
export const jitterMsScale = linearScale(0, 4, 'ms', 1);

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
