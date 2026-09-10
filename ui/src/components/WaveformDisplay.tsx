import React, { useMemo } from 'react';
import { BRAND_YELLOW, BRAND_RED, MUTED, WHITE } from './theme';
import { envelopeDbAt } from './decayEnvelope';

/** Visual dB floor for the envelope overlay's Y-axis - matches
    decayEnvelope.ts's SILENCE_DB exactly, so the plotted line never clips
    before the real math does. (An earlier, shallower floor here (-48dB)
    predated levels reaching true silence at -100dB and was clamping/
    flattening most of any real envelope well before its actual endpoint -
    a visualization bug, not a DSP one; the underlying dB math was already
    smooth and monotonic at every curve/level combination.) */
export const ENVELOPE_DB_FLOOR = -100;

/** dB -> Y (0 at unity/top, `height` at ENVELOPE_DB_FLOOR/bottom), and its
    inverse. Shared with IrEnvelopeGraph.tsx so a dragged point's pixel
    position round-trips through the same dB domain the curve itself is
    plotted in, rather than a separate linear shortcut that could drift from
    this if the floor ever changed. */
export const dbToY = (db: number, height: number): number => {
  const t = Math.max(0, Math.min(1, (0 - db) / (0 - ENVELOPE_DB_FLOOR)));
  return t * height;
};
export const yToDb = (y: number, height: number): number => {
  const t = Math.max(0, Math.min(1, y / height));
  return t * ENVELOPE_DB_FLOOR; // t=0 -> 0dB, t=1 -> ENVELOPE_DB_FLOOR
};

/** "480ms" under a second, "2.67s" at or above. Single source of truth for
    this formatting - also used by the Length knob's readout (knobScale.ts's
    lengthMsScale), so the chip and the knob always agree. */
export function formatContentLength(ms: number): string | null {
  if (!Number.isFinite(ms) || ms <= 0) return null;
  return ms < 1000 ? `${Math.round(ms)}ms` : `${(ms / 1000).toFixed(2)}s`;
}

/** Builds the filled top/bottom polygon path for a min/max waveform strip.
    Shared with IrEnvelopeGraph.tsx so the interactive editor draws the exact
    same backdrop shape as this display-only view. */
export function buildWaveformPath(
  mins: number[],
  maxs: number[],
  width: number,
  height: number
): string {
  const n = Math.min(mins.length, maxs.length);
  if (n < 2) return '';
  const clamp = (v: number) => Math.max(-1, Math.min(1, v));
  const midY = height / 2;
  const xStep = width / (n - 1);
  const top = maxs.map(
    (v, i) => `${(i * xStep).toFixed(1)} ${(midY - clamp(v) * midY).toFixed(1)}`
  );
  const bottom = mins
    .map((v, i) => `${(i * xStep).toFixed(1)} ${(midY - clamp(v) * midY).toFixed(1)}`)
    .reverse();
  return `M${top.join(' L')} L${bottom.join(' L')} Z`;
}

export interface EnvelopeDecayParams {
  initLevel: number;
  attackCurve: number;
  attackFraction: number;
  decayLevel: number;
  decayCurve: number;
}

export interface DecaySegmentPaths {
  /** Init -> Peak. */
  attackPath: string;
  /** Peak -> End. */
  decayPath: string;
}

interface CurvePoint {
  x: number;
  y: number;
}

/** Shared sampling for buildDecaySegments/buildDecayFillPath: the envelope's
    Init->Peak and Peak->End segments, each as its own ordered point list
    (both include the shared boundary point at Peak, so either concatenation
    - path-per-segment or one continuous fill outline - lands exactly on the
    same spot with no seam). Each segment is sampled (and biased) separately
    rather than off one uniform 0..1 grid so the Attack/Decay boundary always
    gets an exact point at its true x-position regardless of curve, and each
    segment's steepest region (x^k's unbounded derivative at t=0 for
    curve<0.5, its bounded-but-steepest point at t=1 for curve>=0.5) gets
    extra sampling resolution. */
function sampleDecayPoints(
  decay: EnvelopeDecayParams,
  cutX: number,
  height: number
): { attack: CurvePoint[]; decay: CurvePoint[] } {
  const biasedT = (t: number, curveNormalized: number) =>
    curveNormalized < 0.5 ? t * t : 1 - (1 - t) * (1 - t);
  const stepsPerSegment = 16;
  const boundary = Math.max(0, Math.min(1, decay.attackFraction));
  const pointAt = (fraction: number): CurvePoint => {
    const db = envelopeDbAt(
      fraction,
      decay.attackFraction,
      decay.initLevel,
      decay.attackCurve,
      decay.decayLevel,
      decay.decayCurve
    );
    return { x: fraction * cutX, y: dbToY(db, height) };
  };
  const attack: CurvePoint[] = [];
  for (let i = 0; i <= stepsPerSegment; i++) {
    attack.push(pointAt(biasedT(i / stepsPerSegment, decay.attackCurve) * boundary));
  }
  const decayPts: CurvePoint[] = [pointAt(boundary)];
  for (let i = 1; i <= stepsPerSegment; i++) {
    decayPts.push(
      pointAt(boundary + biasedT(i / stepsPerSegment, decay.decayCurve) * (1 - boundary))
    );
  }
  return { attack, decay: decayPts };
}

const pointsToPath = (points: CurvePoint[]): string =>
  `M${points.map((p) => `${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(' L')}`;

/** Samples the 2-segment envelope into two SVG (sub)paths, from x=0 to the
    Attack/Decay boundary and from there to `cutX` - kept as two separate
    strings (rather than one continuous path) so IrEnvelopeGraph.tsx can pair
    each segment with its own curve-line drag hit-path. Concatenating both
    strings reproduces the exact single-path rendering this used to build
    directly (see `buildDecayPath` below) - they share the exact same
    boundary point, so there's no visible seam. */
export function buildDecaySegments(
  decay: EnvelopeDecayParams,
  cutX: number,
  height: number
): DecaySegmentPaths {
  const { attack, decay: decayPts } = sampleDecayPoints(decay, cutX, height);
  return {
    attackPath: pointsToPath(attack),
    decayPath: pointsToPath(decayPts),
  };
}

/** Closed fill polygon for the "area under the curve" wash (IrEnvelopeGraph):
    the envelope curve itself as the top/side edge, closed off with two
    straight segments back along y=height (the graph's floor, where the
    waveform/silence sits) to (cutX, height) and (0, height) - the classic
    "area chart filled down to the baseline" technique, mirroring
    buildWaveformPath's own top/bottom polygon but closed against a flat
    floor instead of a second sampled curve. This fills the region the
    envelope actually passes THROUGH (down to where the waveform lives), not
    the headroom it's carved away above. */
export function buildDecayFillPath(
  decay: EnvelopeDecayParams,
  cutX: number,
  height: number
): string {
  const { attack, decay: decayPts } = sampleDecayPoints(decay, cutX, height);
  const curvePoints = [...attack, ...decayPts.slice(1)]; // dedupe the shared boundary point
  const last = curvePoints[curvePoints.length - 1];
  return `${pointsToPath(curvePoints)} L${last.x.toFixed(1)} ${height} L0 ${height} Z`;
}

/** Samples the 2-segment envelope into one continuous SVG path, from x=0 to
    `cutX`. Shared with IrEnvelopeGraph.tsx so the interactive editor's curve
    is pixel-identical to this display-only rendering. */
export function buildDecayPath(decay: EnvelopeDecayParams, cutX: number, height: number): string {
  const { attackPath, decayPath } = buildDecaySegments(decay, cutX, height);
  return `${attackPath} ${decayPath}`;
}

/**
 * Static IR waveform for a block card. Mirrors the EQ graph's visual
 * language (BlockEqView.tsx / SpectrumBackdrop.tsx): a faint center
 * reference line under a gradient-filled curve, no separate grid. Takes
 * independent width/height (rather than a single square boxSize) so it can
 * render as a wide, short strip - the shape the IR block's compact layout
 * uses, and the one the future drag-to-shape v2 surface will want more
 * horizontal resolution for. Display-only for this POC: no drag/edit points.
 */
export const WaveformDisplay: React.FC<{
  mins: number[];
  maxs: number[];
  width: number;
  height: number;
  /** Detected content length in ms (native, RMS-threshold based) - the same
      window these mins/maxs are already trimmed to, so the label always
      matches what's drawn. Omitted/non-finite hides the label. */
  contentLengthMs?: number;
  /** Length knob's current position, normalized 0..1 against this same
      fixed window (1.0 = untrimmed, matching lengthMsScale's own mapping).
      Drawn as a cut line + dimmed region live, entirely client-side - the
      backdrop (mins/maxs) never moves or rescales while dragging; only this
      overlay does. Omitted or >=1 hides it. */
  cutFraction?: number;
  /** Envelope knobs' current position (see BlockParams.initLevel/
      attackCurve/decayLevel/decayCurve). Attack's peak is pinned at
      unity/0dB (not a param). `attackFraction` is where the Attack/Decay
      boundary sits within [0, cutFraction] (0..1 - the caller derives this
      from the same Attack/Decay Length knobs that feed cutFraction). Drawn
      as a static 2-segment line from x=0 to the cut point (the envelope
      only lives within the truncated content), live and client-side like
      cutFraction - v1 is a visualization only, not yet draggable (see
      decayEnvelope.ts for the shared curve math). Omitted, or both levels
      exactly 1.0 (flat/no-op, mirroring prepareIrShapeRebuild's own
      envelopeIsFlat skip), hides it. */
  decay?: {
    initLevel: number;
    attackCurve: number;
    attackFraction: number;
    decayLevel: number;
    decayCurve: number;
  };
}> = ({ mins, maxs, width, height, contentLengthMs, cutFraction, decay }) => {
  const path = useMemo(
    () => buildWaveformPath(mins, maxs, width, height),
    [mins, maxs, width, height]
  );

  if (!path) return null;

  const hasCut = Number.isFinite(cutFraction) && (cutFraction as number) < 1;
  const cutX = hasCut ? width * Math.max(0, cutFraction as number) : width;

  // Envelope line: only within [0, cutX] (the truncated content the
  // envelope actually shapes), only when every level isn't at its 1.0 no-op
  // unity - mirrors prepareIrShapeRebuild's own envelopeIsFlat skip exactly.
  // Y-axis is dB, not the waveform's linear amplitude - true exponential
  // decay is linear-in-dB, and a linear-amplitude plot would misrepresent
  // the actual curve shape. Unity (1.0/0dB) sits at the top edge now (not a
  // center line - levels are attenuation-only, never above unity); the
  // bottom edge is ENVELOPE_DB_FLOOR, a visual-only floor (see its own
  // comment) since the levels' true floor (SILENCE_DB) is far deeper than
  // is useful to actually plot.
  // Sampled/biased separately per segment (rather than one uniform 0..1
  // grid) so the Attack/Decay boundary always gets an exact point at its
  // true x-position regardless of curve, and each segment's steepest region
  // (x^k's unbounded derivative at t=0 for curve<0.5, its bounded-but-
  // steepest point at t=1 for curve>=0.5) gets extra resolution - see
  // buildDecayPath.
  const hasDecay = !!decay && (decay.initLevel !== 1 || decay.decayLevel !== 1);
  const decayPath = hasDecay && decay ? buildDecayPath(decay, cutX, height) : '';

  const lengthLabel = formatContentLength(contentLengthMs ?? NaN);
  const labelFontSize = Math.max(9, height * 0.16);
  const labelPaddingX = labelFontSize * 0.55;
  const labelPaddingY = labelFontSize * 0.3;
  const labelMargin = labelFontSize * 0.5;
  const labelWidth = lengthLabel ? lengthLabel.length * labelFontSize * 0.6 + labelPaddingX * 2 : 0;
  const labelHeight = labelFontSize + labelPaddingY * 2;
  const gradientId = 'ir-waveform-gradient';
  return (
    <svg
      width="100%"
      height="100%"
      viewBox={`0 0 ${width} ${height}`}
      preserveAspectRatio="none"
      style={{ display: 'block' }}
    >
      <defs>
        <linearGradient
          id={gradientId}
          gradientUnits="userSpaceOnUse"
          x1="0"
          y1={height}
          x2="0"
          y2="0"
        >
          <stop offset="0%" stopColor={BRAND_YELLOW} />
          <stop offset="100%" stopColor={BRAND_RED} />
        </linearGradient>
      </defs>
      {/* Center (zero-amplitude) reference line, under the waveform - same
          treatment as the EQ graph's 0dB line. */}
      <line
        x1={0}
        y1={height / 2}
        x2={width}
        y2={height / 2}
        stroke="rgba(235, 235, 245, 0.18)"
        strokeWidth={1}
      />
      <path
        d={path}
        fill={`url(#${gradientId})`}
        fillOpacity={0.3}
        stroke={`url(#${gradientId})`}
        strokeWidth={1}
        strokeOpacity={0.9}
      />
      {hasCut && (
        <g>
          {/* Everything past the cut: this is what Length would remove. */}
          <rect x={cutX} y={0} width={width - cutX} height={height} fill="rgba(10, 10, 14, 0.55)" />
          <line
            x1={cutX}
            y1={0}
            x2={cutX}
            y2={height}
            stroke={MUTED}
            strokeWidth={1.5}
            strokeDasharray="3 2"
          />
        </g>
      )}
      {hasDecay && decayPath && (
        <path d={decayPath} fill="none" stroke={WHITE} strokeWidth={1.5} strokeOpacity={0.85} />
      )}
      {lengthLabel && (
        <g>
          <rect
            x={width - labelMargin - labelWidth}
            y={height - labelMargin - labelHeight}
            width={labelWidth}
            height={labelHeight}
            rx={labelHeight / 3}
            fill="rgba(10, 10, 14, 0.55)"
          />
          <text
            x={width - labelMargin - labelWidth / 2}
            y={height - labelMargin - labelHeight / 2}
            fontSize={labelFontSize}
            fill={MUTED}
            textAnchor="middle"
            dominantBaseline="central"
          >
            {lengthLabel}
          </text>
        </g>
      )}
    </svg>
  );
};
