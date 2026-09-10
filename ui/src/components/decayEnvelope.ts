/**
 * Client-side mirror of prepareIrShapeRebuild's 2-segment Attack/Decay
 * envelope math (ProcessorModelLoader.cpp) - used only to draw the overlay
 * line in WaveformDisplay, never to compute audio. Keep DECAY_CURVE_MAX,
 * SILENCE_DB and the formula shape in sync with the native side by hand;
 * there is no way to share the actual code across the C++/TS boundary, and a
 * mismatch here only misdraws a line, it can't affect what's heard.
 */

/** Must match ProcessorModelLoader.cpp's kCurveMax exactly. */
export const DECAY_CURVE_MAX = 6.0;

/** Must match ProcessorModelLoader.cpp's kSilenceDb exactly - the reachable,
    finite floor levelToDb's linear-in-dB mapping uses for 0 < normalized <=
    1; true silence at the literal normalized=0 case is a separate, exact
    discrete case (see levelToDb and prepareIrShapeRebuild's own
    snap-to-zero), not this constant's job. */
export const SILENCE_DB = -100.0;

/** Compresses sensitivity around curveNormalized=0.5 while leaving 0 and 1
    exactly where they were - must match ProcessorModelLoader.cpp's
    shapeCurveNormalized exactly (see its own comment for why: a straight
    linear (c-0.5) made even a small drag off center swing the exponent
    enough to make the curve sound almost like a step function well before
    it looked like one on the coarsely-sampled graph). */
function shapeCurveNormalized(c: number): number {
  const d = c - 0.5;
  return Math.sign(d) * Math.pow(Math.abs(2 * d), 3) * 0.5;
}

/** curveNormalized 0..1 -> the power-curve exponent k. 0.5 -> k=1 (linear
    in dB - constant-ratio/"exponential" decay, already natural-sounding);
    toward 0 shrinks below 1 (even steeper, more front-loaded: fast initial
    drop, long quiet tail); toward 1 grows past 1 (back-loaded: holds near
    the segment's start level, then drops right at the end). */
export function decayCurveExponent(curveNormalized: number): number {
  return Math.pow(DECAY_CURVE_MAX, 2 * shapeCurveNormalized(curveNormalized));
}

/** Level (0..1, unipolar attenuation-only; 1.0 = unity/0dB, 0.0 = genuine
    silence) -> dB. Linear-in-dB (SILENCE_DB at normalized=0, 0dB at
    normalized=1) - matches every other gain knob in this codebase
    (gainDbScale, gateDbScale, ...), so knob travel feels proportional
    across the whole range. A true logarithmic (20*log10(n)) mapping was
    tried first, treating the knob's own 0..1 as if it directly were linear
    amplitude - but that compresses almost the entire dB range into the
    last sliver of travel near normalized=0, making the knob feel like it
    does nothing for most of its travel then cliff-dives to silent. Genuine
    silence at the literal normalized=0 case is still exact - it's just a
    separate, discrete case (mirroring prepareIrShapeRebuild's own
    snap-to-zero), not something this formula's shape needs to produce. */
export function levelToDb(normalized: number): number {
  if (normalized <= 0) return SILENCE_DB;
  return SILENCE_DB * (1 - normalized);
}

/** Inverse of `levelToDb`: dB -> level (0..1, clamped). Used by the envelope
    graph (IrEnvelopeGraph.tsx) to turn a dragged point's on-screen dB
    position back into a normalized level. */
export function dbToLevel(db: number): number {
  if (db <= SILENCE_DB) return 0;
  if (db >= 0) return 1;
  return 1 - db / SILENCE_DB;
}

/** dB at a point `fraction` (0..1) through one segment, warped by that
    segment's own curve. The warp has to be applied in dB, not linear
    amplitude: true exponential decay is linear-in-dB, and this is also the
    domain WaveformDisplay's overlay plots on, so a Curve=0.5 (k=1) segment
    is a genuine straight line there. */
function segmentDbAt(
  fraction: number,
  fromDb: number,
  toDb: number,
  curveNormalized: number
): number {
  const k = decayCurveExponent(curveNormalized);
  const clampedFraction = Math.max(0, Math.min(1, fraction));
  return fromDb + (toDb - fromDb) * Math.pow(clampedFraction, k);
}

/** One point along the 2-segment envelope: `fraction` is 0..1 across the
    *whole* truncated content (Attack followed by Decay), matching
    WaveformDisplay's cutFraction-relative x-axis. `attackFraction` is where
    the Attack/Decay boundary sits within that same 0..1 span (i.e.
    attackLengthSamples / (attackLengthSamples + decayLengthSamples) -
    WaveformDisplay computes this the same way ChainBlock.tsx derives
    cutFraction, since both come from the same two length knobs). Attack's
    peak is pinned at unity/0dB (not a param) - standard AD-envelope
    semantics, mirroring prepareIrShapeRebuild's own hardcoded attackDb. */
export function envelopeDbAt(
  fraction: number,
  attackFraction: number,
  initLevelNormalized: number,
  attackCurveNormalized: number,
  decayLevelNormalized: number,
  decayCurveNormalized: number
): number {
  const initDb = levelToDb(initLevelNormalized);
  const attackDb = 0; // Attack's peak is pinned at unity/0dB
  const decayDb = levelToDb(decayLevelNormalized);
  const clampedFraction = Math.max(0, Math.min(1, fraction));
  const clampedBoundary = Math.max(0, Math.min(1, attackFraction));
  if (clampedFraction < clampedBoundary) {
    const segFraction = clampedBoundary > 0 ? clampedFraction / clampedBoundary : 1;
    return segmentDbAt(segFraction, initDb, attackDb, attackCurveNormalized);
  }
  const decaySpan = 1 - clampedBoundary;
  const segFraction = decaySpan > 0 ? (clampedFraction - clampedBoundary) / decaySpan : 1;
  return segmentDbAt(segFraction, attackDb, decayDb, decayCurveNormalized);
}
