import React, { useCallback, useEffect, useRef, useState } from 'react';
import { BRAND_YELLOW, BRAND_RED, MUTED, WHITE } from './theme';
import { dbToLevel, levelToDb } from './decayEnvelope';
import {
  buildDecayFillPath,
  buildDecaySegments,
  buildWaveformPath,
  dbToY,
  formatContentLength,
  yToDb,
} from './WaveformDisplay';
import { curveScale, percentScale } from './knobScale';
import type { KnobScale } from './knobScale';
import { HELP, helpProps, pinHelp, unpinHelp } from './helpText';

/**
 * Interactive replacement for WaveformDisplay in the IR block's main editor
 * (ChainBlock.tsx) - Phase 2 of the envelope shaping feature. Draws the same
 * backdrop/curve WaveformDisplay does (shared helpers, pixel-identical), but
 * the three curve control points (Init/Peak/End) are draggable directly on
 * the graph instead of via the separate fader row.
 *
 * Fixed-window philosophy, matching the original Length knob and
 * WaveformDisplay: the backdrop (waveform, axis) never rescales - only the
 * overlay (cut line, curve, points) moves within it. Dragging End's X slides
 * along that same fixed axis. Because Peak's on-screen x is
 * attackFraction * decayLength * width (a fraction of a fraction), dragging
 * End alone visibly moves Peak's on-screen position even though Peak's own
 * parameter (attackLength) didn't change - that's correct, not a bug.
 *
 * Curve-line dragging (Init->Peak, Peak->End): a vertical drag on either
 * segment bows that segment's own Curve parameter. The sign of "drag down ->
 * curve increases or decreases" flips between the two segments, and this is
 * intentional, not a bug - see decayCurveExponent's own doc comment. Init->
 * Peak *rises* (init level up to unity), so increasing its curve exponent
 * (k, via decayCurveExponent) shrinks fraction^k at any fixed fraction<1,
 * pulling the sampled value *down* toward its start (a lower, more
 * attenuated dB - visually a downward bow); dragging the line down should
 * therefore *increase* attackCurve. Peak->End *falls* (unity down to decay
 * level), where (to-from) is negative, so the same shrinking fraction^k
 * makes the sampled value *less* negative (less attenuated - visually an
 * upward bow) as curve increases; dragging the line down should therefore
 * *decrease* decayCurve. Both reduce to the same rule stated once, in
 * `handlePointerMove` below, as a per-segment sign.
 *
 * Hover rings, the floating readout, cursor states, Shift-fine and
 * Alt-click-reset all follow this file's own established conventions rather
 * than inventing new ones: Shift-fine and the readout's show/hold timing
 * mirror EnvelopeFader/KnobControl; Alt-click-reset and the hover ring mirror
 * BlockEqView's dot handling exactly (down to reading modifier state off the
 * pointer event itself rather than separate keydown/keyup listeners, since
 * BlockEqView's own comment elsewhere in this codebase notes the plugin
 * webview doesn't reliably deliver bare-modifier key events).
 */

export interface EnvelopePatch {
  initLevel?: number;
  attackLength?: number;
  attackCurve?: number;
  decayLength?: number;
  decayLevel?: number;
  decayCurve?: number;
}

type DragTarget = 'init' | 'peak' | 'end' | 'attack' | 'decay';

const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), hi);

/** Same hold as EnvelopeFader/KnobControl's own drag readouts. */
const READOUT_HOLD_MS = 250;

const targetHelp = (target: DragTarget): string => {
  switch (target) {
    case 'init':
      return HELP.envelopeInitPoint;
    case 'peak':
      return HELP.envelopePeakPoint;
    case 'end':
      return HELP.envelopeEndPoint;
    case 'attack':
      return HELP.envelopeAttackCurve;
    case 'decay':
      return HELP.envelopeDecayCurve;
  }
};

/** "Nice" axis-tick step (the standard 1-2-5-times-a-power-of-ten sequence
    chart libraries use) for a background time grid spanning `totalMs`,
    aiming for roughly `targetTicks` gridlines. */
const niceGridStepMs = (totalMs: number, targetTicks = 6): number => {
  if (!Number.isFinite(totalMs) || totalMs <= 0) return 0;
  const rough = totalMs / targetTicks;
  const magnitude = Math.pow(10, Math.floor(Math.log10(rough)));
  const residual = rough / magnitude;
  const step = residual < 1.5 ? 1 : residual < 3.5 ? 2 : residual < 7.5 ? 5 : 10;
  return step * magnitude;
};

/** Points sit exactly on the content's true edges (Init at x=0, Peak at
    y=0/unity, End's x/y reaching the same edges at extreme values) - their
    hit-circle would be half-clipped by a viewBox that ends exactly there.
    The viewBox is padded by this on every side so the full circle always has
    room; content (waveform/curve/points) keeps its true 0..width/0..height
    coordinates unchanged, it just no longer sits flush against the SVG's own
    edge. */
const HIT_RADIUS = 14;
const EDGE_PAD = HIT_RADIUS + 2;

export const IrEnvelopeGraph: React.FC<{
  mins: number[];
  maxs: number[];
  width: number;
  height: number;
  contentLengthMs?: number;
  initLevel: number;
  attackLength: number;
  attackCurve: number;
  decayLength: number;
  decayLevel: number;
  decayCurve: number;
  /** Dynamic per-block scales (see knobScale.ts) - only needed here for the
      floating readout's text; the graph's own geometry works in plain
      normalized 0..1 fractions regardless. */
  attackLengthScale: KnobScale;
  decayLengthScale: KnobScale;
  /** Normalized Predelay (BlockParams.predelay) - display-only here (a
      left-edge line, shown only when > 0, mirroring the cut line's own
      treatment); the Delay knob in the Input rail is still the only way to
      change it. Not part of the envelope patch/onChange - a separate,
      real-time DSP stage. */
  predelay: number;
  /** Called on every pointermove with just the axis/axes that point moves
      (Init: initLevel only; Peak: attackLength only; End: both decayLength
      and decayLevel together, so a diagonal drag commits as one native
      rebuild rather than two racing ones). */
  onChange: (patch: EnvelopePatch) => void;
  onDragStateChange?: (dragging: boolean) => void;
}> = ({
  mins,
  maxs,
  width,
  height,
  contentLengthMs,
  initLevel,
  attackLength,
  attackCurve,
  decayLength,
  decayLevel,
  decayCurve,
  attackLengthScale,
  decayLengthScale,
  predelay,
  onChange,
  onDragStateChange,
}) => {
  const graphRef = useRef<SVGSVGElement | null>(null);
  const dragStateRef = useRef<{ target: DragTarget; lastX: number; lastY: number } | null>(null);
  // Mirrors dragStateRef.current?.target, but as state so it can drive
  // re-renders (ring, cursor, readout visibility) - the ref alone is enough
  // for the pointer math, which needs no re-render.
  const [dragTarget, setDragTarget] = useState<DragTarget | null>(null);
  const [hoverTarget, setHoverTarget] = useState<DragTarget | null>(null);
  // Which target's floating readout is showing - set on grab, cleared only
  // once the post-release hold timer expires (unlike dragTarget, which
  // clears immediately on release), so the readout lingers on the value the
  // user just landed on rather than vanishing the instant they let go.
  const [readoutTarget, setReadoutTarget] = useState<DragTarget | null>(null);
  const readoutHoldTimerRef = useRef<number | null>(null);
  useEffect(
    () => () => {
      if (readoutHoldTimerRef.current !== null) window.clearTimeout(readoutHoldTimerRef.current);
    },
    []
  );

  const waveformPath = buildWaveformPath(mins, maxs, width, height);

  // Background time grid (Space Designer-style reference lines) across the
  // fixed window's full real-world span - contentLengthMs is what x=width
  // itself represents, regardless of where the cut line currently sits.
  const gridStepMs = niceGridStepMs(contentLengthMs ?? 0);
  const gridLinesX: number[] = [];
  if (gridStepMs > 0 && contentLengthMs) {
    for (let ms = gridStepMs; ms < contentLengthMs; ms += gridStepMs) {
      gridLinesX.push((ms / contentLengthMs) * width);
    }
  }

  const cutFraction = clamp(decayLength, 0, 1);
  const cutX = width * cutFraction;
  const attackFraction = clamp(attackLength, 0, 1);

  const hasDecay = initLevel !== 1 || decayLevel !== 1;
  const decayParams = { initLevel, attackCurve, attackFraction, decayLevel, decayCurve };
  const segments = hasDecay ? buildDecaySegments(decayParams, cutX, height) : null;
  // Fill is zero-area (invisible) exactly when the curve sits flush on the
  // 0dB ceiling the whole way (the flat/no-op case) - a direct geometric
  // consequence of "area under the curve", not a separate condition.
  const fillPath = hasDecay ? buildDecayFillPath(decayParams, cutX, height) : '';

  const initY = dbToY(levelToDb(initLevel), height);
  const peakX = cutX * attackFraction;
  const endY = dbToY(levelToDb(decayLevel), height);

  // Floating readout: anchored at the point's own coordinates for Init/Peak/
  // End, or the segment's straight-line midpoint for Attack/Decay curve (a
  // fixed, always-derivable position - unlike the live pointer, it needs no
  // extra state to track).
  const readoutAnchor = (target: DragTarget): { x: number; y: number } => {
    switch (target) {
      case 'init':
        return { x: 0, y: initY };
      case 'peak':
        return { x: peakX, y: 0 };
      case 'end':
        return { x: cutX, y: endY };
      case 'attack':
        return { x: peakX / 2, y: initY / 2 };
      case 'decay':
        return { x: (peakX + cutX) / 2, y: endY / 2 };
    }
  };
  // Value only, no parameter-name prefix (e.g. "-6dB", not "Init -6dB") -
  // besides being less cluttered, the shorter text also gives the top-edge
  // clipping fix above more effective margin to work with.
  const readoutLabel = (target: DragTarget): string => {
    switch (target) {
      case 'init':
        return percentScale.format(initLevel);
      case 'peak':
        return attackLengthScale.format(attackLength);
      case 'end':
        return `${decayLengthScale.format(decayLength)} · ${percentScale.format(decayLevel)}`;
      case 'attack':
        return curveScale.format(attackCurve);
      case 'decay':
        return curveScale.format(decayCurve);
    }
  };

  // Pixel <-> parameter-unit conversions, all going through the same dB
  // domain the curve itself is plotted in (see dbToY/yToDb's own comment).
  const yToLevel = useCallback((y: number) => dbToLevel(yToDb(y, height)), [height]);
  const xToAttackFraction = useCallback((x: number) => (cutX > 0 ? x / cutX : 0), [cutX]);
  const xToDecayLength = useCallback((x: number) => x / width, [width]);

  // Rect spans the padded viewBox (preserveAspectRatio="none" stretches it
  // exactly to the element's box), so a fraction across rect maps linearly
  // to [-EDGE_PAD, width+EDGE_PAD] / [-EDGE_PAD, height+EDGE_PAD], not to
  // [0, width]/[0, height] directly.
  const graphPointFromEvent = useCallback(
    (e: PointerEvent | React.PointerEvent) => {
      const rect = graphRef.current?.getBoundingClientRect();
      if (!rect) return { x: 0, y: 0 };
      return {
        x: ((e.clientX - rect.left) / rect.width) * (width + EDGE_PAD * 2) - EDGE_PAD,
        y: ((e.clientY - rect.top) / rect.height) * (height + EDGE_PAD * 2) - EDGE_PAD,
      };
    },
    [width, height]
  );

  // Point reset (Init/Peak/End only, not the curve segments): Alt/Option-
  // click and double-click both trigger it - the pointerdown handler for the
  // altKey case, and a dedicated onDoubleClick for the click case (points
  // only; curve segments carry neither).
  const resetTarget = useCallback(
    (target: DragTarget) => {
      if (target === 'init') onChange({ initLevel: 1.0 });
      else if (target === 'peak') onChange({ attackLength: 0.0 });
      else if (target === 'end') onChange({ decayLength: 1.0, decayLevel: 1.0 });
      else if (target === 'attack') onChange({ attackCurve: 0.5 });
      else onChange({ decayCurve: 0.5 });
    },
    [onChange]
  );

  // Alt/Option-click resets without engaging a drag - mirrors BlockEqView's
  // resetBand-then-early-return.
  const handlePointerDown = useCallback(
    (target: DragTarget) => (e: React.PointerEvent<SVGCircleElement | SVGPathElement>) => {
      e.preventDefault();
      if (e.altKey) {
        resetTarget(target);
        return;
      }
      e.currentTarget.setPointerCapture(e.pointerId);
      const { x, y } = graphPointFromEvent(e);
      dragStateRef.current = { target, lastX: x, lastY: y };
      setDragTarget(target);
      if (readoutHoldTimerRef.current !== null) {
        window.clearTimeout(readoutHoldTimerRef.current);
        readoutHoldTimerRef.current = null;
      }
      setReadoutTarget(target);
      pinHelp(targetHelp(target));
      onDragStateChange?.(true);
    },
    [graphPointFromEvent, resetTarget, onDragStateChange]
  );

  // Delta-based (not absolute pointer position), each delta computed in the
  // target parameter's own unit space rather than raw pixels - mirrors
  // BlockEqView's dot dragging, and (unlike a raw-pixel delta) stays correct
  // if the pixel<->unit mapping is ever non-linear. Curve deltas are the one
  // exception: Curve is already a plain linear 0..1 shape parameter with no
  // natural pixel-domain unit of its own (mirroring how BlockEqView's own Q
  // drag uses an arbitrary tuned constant rather than a unit conversion), so
  // a raw dY fraction of the graph height is used directly.
  const handlePointerMove = useCallback(
    (target: DragTarget) => (e: React.PointerEvent<SVGCircleElement | SVGPathElement>) => {
      const drag = dragStateRef.current;
      if (!drag || drag.target !== target) return;
      const { x, y } = graphPointFromEvent(e);
      const fine = e.shiftKey ? 1 / 8 : 1;

      if (target === 'init') {
        const dLevel = (yToLevel(y) - yToLevel(drag.lastY)) * fine;
        onChange({ initLevel: clamp(initLevel + dLevel, 0, 1) });
      } else if (target === 'peak') {
        const dFraction = (xToAttackFraction(x) - xToAttackFraction(drag.lastX)) * fine;
        onChange({ attackLength: clamp(attackLength + dFraction, 0, 1) });
      } else if (target === 'end') {
        const dLength = (xToDecayLength(x) - xToDecayLength(drag.lastX)) * fine;
        const dLevel = (yToLevel(y) - yToLevel(drag.lastY)) * fine;
        onChange({
          decayLength: clamp(decayLength + dLength, 0, 1),
          decayLevel: clamp(decayLevel + dLevel, 0, 1),
        });
      } else {
        // See this component's own doc comment for why the sign flips
        // between a rising (attack) and falling (decay) segment.
        const sign = target === 'attack' ? 1 : -1;
        const dCurve = (sign * (y - drag.lastY) * fine) / height;
        if (target === 'attack') {
          onChange({ attackCurve: clamp(attackCurve + dCurve, 0, 1) });
        } else {
          onChange({ decayCurve: clamp(decayCurve + dCurve, 0, 1) });
        }
      }
      drag.lastX = x;
      drag.lastY = y;
    },
    [
      graphPointFromEvent,
      yToLevel,
      xToAttackFraction,
      xToDecayLength,
      height,
      initLevel,
      attackLength,
      attackCurve,
      decayLength,
      decayLevel,
      decayCurve,
      onChange,
    ]
  );

  const handlePointerUp = useCallback(() => {
    const target = dragStateRef.current?.target;
    if (!target) return;
    dragStateRef.current = null;
    setDragTarget(null);
    unpinHelp(targetHelp(target));
    if (readoutHoldTimerRef.current !== null) window.clearTimeout(readoutHoldTimerRef.current);
    readoutHoldTimerRef.current = window.setTimeout(() => setReadoutTarget(null), READOUT_HOLD_MS);
    onDragStateChange?.(false);
  }, [onDragStateChange]);

  const lengthLabel = formatContentLength(contentLengthMs ?? NaN);
  const labelFontSize = Math.max(9, height * 0.16);
  const labelPaddingX = labelFontSize * 0.55;
  const labelPaddingY = labelFontSize * 0.3;
  const labelMargin = labelFontSize * 0.5;
  const labelWidth = lengthLabel ? lengthLabel.length * labelFontSize * 0.6 + labelPaddingX * 2 : 0;
  const labelHeight = labelFontSize + labelPaddingY * 2;
  const gradientId = 'ir-envelope-graph-gradient';

  const hitCircle = (target: DragTarget, cx: number, cy: number) => (
    <circle
      cx={cx}
      cy={cy}
      r={HIT_RADIUS}
      fill="transparent"
      style={{ cursor: dragTarget === target ? 'grabbing' : 'grab', touchAction: 'none' }}
      {...helpProps(targetHelp(target))}
      onPointerEnter={() => setHoverTarget(target)}
      onPointerLeave={() => setHoverTarget((t) => (t === target ? null : t))}
      onPointerDown={handlePointerDown(target)}
      onPointerMove={handlePointerMove(target)}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onDoubleClick={() => resetTarget(target)}
    />
  );

  /** EQ's r=9 selection-ring outline, shown while hovered or dragging. */
  const ring = (target: DragTarget, cx: number, cy: number) =>
    (hoverTarget === target || dragTarget === target) && (
      <circle
        cx={cx}
        cy={cy}
        r={9}
        fill="none"
        stroke="#ffffff"
        strokeWidth={1.5}
        opacity={0.9}
        style={{ pointerEvents: 'none' }}
      />
    );

  // pointerEvents: 'none' - purely decorative, sits on top of (and would
  // otherwise steal clicks from) its own larger hitCircle underneath; they're
  // siblings, not parent/child, so a captured click on this circle would
  // dead-end here rather than bubbling down to the hit target.
  const dot = (cx: number, cy: number) => (
    <circle
      cx={cx}
      cy={cy}
      r={4.5}
      fill={WHITE}
      stroke="#000000"
      strokeWidth={1.25}
      style={{ pointerEvents: 'none' }}
    />
  );

  /** Invisible thick path under the visible curve segment - carries the
      curve-line drag. `pointerEvents: 'stroke'` (fill is always none) means
      only the rendered 14-unit-wide stroke itself is hit-tested, not the
      empty area a filled shape covering the same bounding box would
      include. */
  const segmentHitPath = (target: 'attack' | 'decay', d: string) => (
    <path
      d={d}
      fill="none"
      stroke="transparent"
      strokeWidth={14}
      style={{ pointerEvents: 'stroke', cursor: 'ns-resize', touchAction: 'none' }}
      {...helpProps(targetHelp(target))}
      onPointerEnter={() => setHoverTarget(target)}
      onPointerLeave={() => setHoverTarget((t) => (t === target ? null : t))}
      onPointerDown={handlePointerDown(target)}
      onPointerMove={handlePointerMove(target)}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onDoubleClick={() => resetTarget(target)}
    />
  );

  // HTML overlay (not SVG text) for the readout so its font isn't stretched
  // by the SVG's non-uniform preserveAspectRatio="none" scaling. Position is
  // expressed as a percentage of the padded viewBox, which the SVG element
  // fills exactly (width/height 100%), so it lines up with the anchor
  // regardless of the card's actual rendered size.
  const viewBoxWidth = width + EDGE_PAD * 2;
  const viewBoxHeight = height + EDGE_PAD * 2;
  // Edge avoidance: the default placement offsets the chip above its anchor
  // (translateY -140%, i.e. the box's own height plus a small gap) - fine
  // everywhere except near the top of the graph (dragging Init toward unity,
  // Peak is always here), where that pushes the chip above the card's own
  // clipped boundary (same class of edge-clipping the points themselves hit
  // before EDGE_PAD, but this is a plain HTML overlay, not SVG, so the fix
  // is a flip rather than a viewBox pad). Mirrors typical tooltip
  // edge-avoidance: flip to sit below instead when there's no room above.
  const readoutAnchorPoint = readoutTarget ? readoutAnchor(readoutTarget) : null;
  const readoutNearTop = readoutAnchorPoint !== null && readoutAnchorPoint.y < height * 0.2;
  const readout = readoutTarget && readoutAnchorPoint && (
    <div
      style={{
        position: 'absolute',
        left: `${((readoutAnchorPoint.x + EDGE_PAD) / viewBoxWidth) * 100}%`,
        top: `${((readoutAnchorPoint.y + EDGE_PAD) / viewBoxHeight) * 100}%`,
        transform: `translate(-50%, ${readoutNearTop ? '40%' : '-140%'})`,
        padding: '3rem 6rem',
        borderRadius: '4rem',
        backgroundColor: 'rgba(10, 10, 14, 0.85)',
        color: WHITE,
        fontSize: '11rem',
        whiteSpace: 'nowrap',
        pointerEvents: 'none',
      }}
    >
      {readoutLabel(readoutTarget)}
    </div>
  );

  return (
    <div style={{ position: 'relative', width: '100%', height: '100%' }}>
      <svg
        ref={graphRef}
        width="100%"
        height="100%"
        viewBox={`${-EDGE_PAD} ${-EDGE_PAD} ${viewBoxWidth} ${viewBoxHeight}`}
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
        <line
          x1={0}
          y1={height / 2}
          x2={width}
          y2={height / 2}
          stroke="rgba(235, 235, 245, 0.18)"
          strokeWidth={1}
        />
        {/* Background time grid: fainter than EQ's own grid (0.05 vs 0.07) -
            this one has to stay out of the waveform/curve's way, not read as
            a peer axis. */}
        {gridLinesX.map((x, i) => (
          <line
            key={i}
            x1={x}
            y1={0}
            x2={x}
            y2={height}
            stroke="rgba(235, 235, 245, 0.05)"
            strokeWidth={1}
          />
        ))}
        {/* Under-curve fill (Space Designer style): the area between the
            envelope curve and the graph floor below it (where the waveform
            lives), not a flat rectangular wash - so it follows the curve's
            actual shape (thin near a deep cut, filling most of the height
            near Init/Peak where little is attenuated). Plain white at very
            low opacity, not a new hue - keeps this passive, informational
            distinction out of the way of the waveform's own yellow->red
            gradient and the white curve/points drawn on top of it. Rendered
            before the waveform so that stays fully crisp. */}
        {fillPath && <path d={fillPath} fill="#ffffff" fillOpacity={0.06} />}
        {waveformPath && (
          <path
            d={waveformPath}
            fill={`url(#${gradientId})`}
            fillOpacity={0.3}
            stroke={`url(#${gradientId})`}
            strokeWidth={1}
            strokeOpacity={0.9}
          />
        )}
        {cutFraction < 1 && (
          <g>
            <rect
              x={cutX}
              y={0}
              width={width - cutX}
              height={height}
              fill="rgba(10, 10, 14, 0.55)"
            />
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
        {segments && (
          <>
            {segmentHitPath('attack', segments.attackPath)}
            {segmentHitPath('decay', segments.decayPath)}
            <path
              d={segments.attackPath}
              fill="none"
              stroke={WHITE}
              strokeWidth={1.5}
              strokeOpacity={0.85}
            />
            <path
              d={segments.decayPath}
              fill="none"
              stroke={WHITE}
              strokeWidth={1.5}
              strokeOpacity={0.85}
            />
          </>
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
        {/* Predelay indicator: a left-edge line mirroring the cut line's own
            treatment (same stroke/dash), shown only when there's actually a
            predelay to signal - zero visual cost at the default (0) case.
            Display-only: the Delay knob in the Input rail is still what
            changes this. Rendered after the fill/waveform/curve so it stays
            visible on top of them at x=0. */}
        {predelay > 0 && (
          <line
            x1={0}
            y1={0}
            x2={0}
            y2={height}
            stroke={MUTED}
            strokeWidth={1.5}
            strokeDasharray="3 2"
          />
        )}

        {/* Points render last (on top) so they win hit-testing within their
          own radius over anything drawn beneath them. */}
        {ring('init', 0, initY)}
        {hitCircle('init', 0, initY)}
        {dot(0, initY)}
        {ring('peak', peakX, 0)}
        {hitCircle('peak', peakX, 0)}
        {dot(peakX, 0)}
        {ring('end', cutX, endY)}
        {hitCircle('end', cutX, endY)}
        {dot(cutX, endY)}
      </svg>
      {readout}
    </div>
  );
};
