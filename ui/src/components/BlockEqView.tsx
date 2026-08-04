import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { EqBand, EqBandType } from '../types/chain';
import {
  EQ_MAX_ABS_GAIN_DB,
  EQ_MAX_Q,
  EQ_MIN_Q,
  EQ_MIN_FREQ_HZ,
  EQ_MAX_FREQ_HZ,
  eqBandTypeOptions,
} from '../types/chain';
import { eqResponseDb, formatFreq, freqToNorm, normToFreq } from './eqMath';
import {
  BAND_TYPE_LABELS,
  GRAPH_H,
  GRAPH_W,
  TYPE_GLYPHS,
  clamp,
  gainToY,
  hasGain,
  yToGain,
} from './eqShared';
import { BODY_PADDING } from './chainLayout';
import { EqSliders } from './EqSliders';
import { SpectrumBackdrop } from './SpectrumBackdrop';
import { HELP, bandTypeHelp, helpProps, pinHelp, unpinHelp } from './helpText';
import {
  ICON_BOX_RADIUS,
  ICON_SIZE,
  MUTED,
  SEGMENTED_TRACK,
  SUBTLE,
  TEXT_BOX_HEIGHT,
  segmentedCellStyle,
  segmentedGroupStyle,
} from './theme';

/**
 * 6-band EQ editor shown in the card body while the header EQ toggle is
 * active, with two interchangeable views (chosen via the EQ menu that lives
 * in the card header while the editor is open; see ChainBlock):
 *
 * - Graph: the full editor. Grid bleeds edge-to-edge, controls float over
 *   it. Drag dots for freq/gain (vertical drag tunes Q on cut bands),
 *   scroll to tune the selected band's Q.
 * - Sliders (EqSliders.tsx): a Mesa-style graphic EQ mirroring the same
 *   bands. Gain only; frequency/type/Q are editable in the graph view.
 *
 * Shared interaction conventions (mirroring KnobControl):
 * - Shift while dragging a dot (or wheeling Q) = 8x finer control.
 * - Alt/Option-click a dot resets the band (gain to 0, or Q to default on
 *   cut bands) without touching its frequency.
 * - The Freq/Gain/Q chips double as text entry: click one to type an exact
 *   value (Enter commits, Escape cancels). Freq accepts "1.2k" style.
 *
 * The spectrum backdrop (SpectrumBackdrop.tsx) is its own leaf so its
 * ~30 fps updates never re-render the editor; both views render it.
 *
 * Band roles are fixed channel-strip style (enforced natively too): band 1 is
 * low cut/low shelf, band 6 is high cut/high shelf, bands 2-5 are bells.
 */

export type EqViewMode = 'graph' | 'sliders';

// Controls stay black/white/gray (like the knobs); the only color in the EQ
// is the brand-gradient spectrum behind everything.
const CURVE_COLOR = '#8E8E93';

/** Q defaults applied when switching a band's curve type. */
const TYPE_DEFAULT_Q: Partial<Record<EqBandType, number>> = {
  lowcut: 0.71,
  highcut: 0.71,
  lowshelf: 0.71,
  highshelf: 0.71,
};

// One sample per horizontal pixel so even a Q=10 spike is drawn cleanly
// (coarse sampling clips the narrow notch into a flat-bottomed wedge).
const CURVE_POINTS = GRAPH_W;
const CURVE_FREQS = Array.from({ length: CURVE_POINTS }, (_, i) =>
  normToFreq(i / (CURVE_POINTS - 1))
);

const GRID_FREQS = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const GRID_LABELS: Record<number, string> = {
  50: '50',
  100: '100',
  200: '200',
  500: '500',
  1000: '1k',
  2000: '2k',
  5000: '5k',
  10000: '10k',
};

/** Readout chip that doubles as text entry: click to type, Enter commits,
    Escape cancels, blur commits (same conventions as the knobs). The value
    area is a fixed width (sized to the longest possible reading) so the chip
    never resizes while values change or while editing. */
const EditableChip: React.FC<{
  label: string;
  text: string;
  /** Prefill for the editor (number only, unit-free where possible). */
  editText: string;
  /** Fixed width of the value area in px: the widest reading the chip shows. */
  valueWidth: number;
  onCommit: (raw: string) => void;
  disabled?: boolean;
  /** One-line hint for the faceplate help readout (see helpText.ts). */
  help?: string;
  style?: React.CSSProperties;
}> = ({ label, text, editText, valueWidth, onCommit, disabled = false, help, style }) => {
  const [draft, setDraft] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const editing = draft !== null;

  useEffect(() => {
    if (editing) inputRef.current?.focus();
  }, [editing]);

  const commit = () => {
    if (draft !== null && draft.trim() !== '') onCommit(draft);
    setDraft(null);
  };

  return (
    <div
      {...(help && !disabled ? helpProps(help) : {})}
      onClick={() => {
        // Editing starts from an empty box (caret at the left) with the
        // current value as placeholder; committing empty is a cancel.
        if (!disabled && !editing) setDraft('');
      }}
      style={{ ...style, cursor: disabled || editing ? undefined : 'text' }}
    >
      <span style={{ fontSize: '12px', fontFamily: 'monospace', color: SUBTLE }}>{label}</span>
      {editing ? (
        <input
          ref={inputRef}
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            e.stopPropagation();
            if (e.key === 'Enter') commit();
            else if (e.key === 'Escape') setDraft(null);
          }}
          inputMode="decimal"
          placeholder={editText}
          style={{
            width: `${valueWidth}px`,
            background: 'transparent',
            border: 'none',
            color: '#ffffff',
            fontSize: '12px',
            fontFamily: 'monospace',
            textAlign: 'left',
            outline: 'none',
            padding: 0,
          }}
        />
      ) : (
        <span
          style={{
            width: `${valueWidth}px`,
            fontSize: '12px',
            fontFamily: 'monospace',
            color: '#ffffff',
            textAlign: 'left',
            whiteSpace: 'nowrap',
          }}
        >
          {text}
        </span>
      )}
    </div>
  );
};

/** Parse a typed frequency: plain Hz ("800") or k-notation ("1.2k"). */
const parseFreqInput = (raw: string): number | null => {
  const cleaned = raw.trim().toLowerCase().replace(',', '.').replace(/hz$/, '').trim();
  const hasK = cleaned.includes('k');
  const value = Number.parseFloat(cleaned.replace('k', ''));
  if (!Number.isFinite(value)) return null;
  return hasK ? value * 1000 : value;
};

interface BlockEqViewProps {
  blockId: string;
  bands: EqBand[];
  /** EQ power state; a bypassed EQ renders its curve/dots dimmed. */
  eqEnabled: boolean;
  sampleRate: number;
  /** Which editor to show; owned by ChainBlock (the header EQ menu). */
  view: EqViewMode;
  /** Fire-and-forget whole-band setter (safe at drag rates). */
  onSetBand: (blockId: string, bandIndex: number, band: EqBand) => void;
}

export const BlockEqView: React.FC<BlockEqViewProps> = ({
  blockId,
  bands: bandsProp,
  eqEnabled,
  sampleRate,
  view,
  onSetBand,
}) => {
  // Optimistic local bands; native converges via chain-state resyncs. Skip
  // prop syncs mid-drag so a stale snapshot can't fight the pointer.
  const [bands, setBands] = useState<EqBand[]>(bandsProp);
  const [selected, setSelected] = useState(1);
  const draggingRef = useRef(false);
  const dragStateRef = useRef<{ index: number; lastX: number; lastY: number } | null>(null);
  const graphRef = useRef<SVGSVGElement | null>(null);

  useEffect(() => {
    if (!draggingRef.current) setBands(bandsProp);
  }, [bandsProp]);

  const updateBand = useCallback(
    (index: number, patch: Partial<EqBand>) => {
      setBands((prev) => {
        const next = prev.map((b, i) => (i === index ? { ...b, ...patch } : b));
        onSetBand(blockId, index, next[index]);
        return next;
      });
    },
    [blockId, onSetBand]
  );

  // --- dot dragging -------------------------------------------------------
  const graphPointFromEvent = useCallback((e: PointerEvent | React.PointerEvent) => {
    const rect = graphRef.current?.getBoundingClientRect();
    if (!rect) return { x: 0, y: 0 };
    return {
      x: ((e.clientX - rect.left) / rect.width) * GRAPH_W,
      y: ((e.clientY - rect.top) / rect.height) * GRAPH_H,
    };
  }, []);

  /** Alt/Option-click reset: neutralize the band's effect (gain or Q) while
      keeping its frequency, matching the knobs' Alt-click convention. */
  const resetBand = useCallback(
    (index: number) => {
      const band = bands[index];
      if (!band) return;
      updateBand(
        index,
        hasGain(band.type) ? { gainDb: 0 } : { q: TYPE_DEFAULT_Q[band.type] ?? 0.71 }
      );
    },
    [bands, updateBand]
  );

  const handleDotPointerDown = useCallback(
    (index: number) => (e: React.PointerEvent<SVGCircleElement>) => {
      e.preventDefault();
      setSelected(index);
      if (e.altKey) {
        resetBand(index);
        return;
      }
      e.currentTarget.setPointerCapture(e.pointerId);
      draggingRef.current = true;
      // Keep the hint up while the (captured) drag runs across the graph.
      pinHelp(HELP.eqDot);
      const { x, y } = graphPointFromEvent(e);
      dragStateRef.current = { index, lastX: x, lastY: y };
    },
    [graphPointFromEvent, resetBand]
  );

  // Delta-based dragging (not absolute pointer position) so Shift = 8x finer
  // control works and can toggle mid-drag without the dot jumping.
  const handleDotPointerMove = useCallback(
    (index: number) => (e: React.PointerEvent<SVGCircleElement>) => {
      const drag = dragStateRef.current;
      if (!draggingRef.current || drag?.index !== index) return;
      const { x, y } = graphPointFromEvent(e);
      const fine = e.shiftKey ? 1 / 8 : 1;
      const dX = (x - drag.lastX) * fine;
      const dGain = (yToGain(y) - yToGain(drag.lastY)) * fine;
      const dY = (y - drag.lastY) * fine;
      drag.lastX = x;
      drag.lastY = y;

      const band = bands[index];
      // Bands keep their left-to-right order: clamp between the neighbors'
      // frequencies (with a hair of margin so dots never sit exactly on top).
      const lo = index > 0 ? bands[index - 1].freqHz * 1.02 : EQ_MIN_FREQ_HZ;
      const hi = index < bands.length - 1 ? bands[index + 1].freqHz * 0.98 : EQ_MAX_FREQ_HZ;
      const freqNorm = clamp(freqToNorm(band.freqHz) + dX / GRAPH_W, 0, 1);
      const freqHz = clamp(normToFreq(freqNorm), lo, hi);
      if (hasGain(band.type)) {
        const gainDb = clamp(band.gainDb + dGain, -EQ_MAX_ABS_GAIN_DB, EQ_MAX_ABS_GAIN_DB);
        updateBand(index, { freqHz, gainDb });
      } else {
        // Cuts have no gain, so vertical drag tunes Q instead (up = tighter).
        const q = clamp(band.q * Math.exp(-dY * 0.02), EQ_MIN_Q, EQ_MAX_Q);
        updateBand(index, { freqHz, q });
      }
    },
    [bands, graphPointFromEvent, updateBand]
  );

  const handleDotPointerUp = useCallback(() => {
    draggingRef.current = false;
    dragStateRef.current = null;
    unpinHelp(HELP.eqDot);
  }, []);

  // --- wheel = Q of the selected band (non-passive so the page can't scroll)
  const containerRef = useRef<HTMLDivElement | null>(null);
  const wheelStateRef = useRef({ bands, selected, view });
  wheelStateRef.current = { bands, selected, view };
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      const { bands: current, selected: index, view: mode } = wheelStateRef.current;
      if (mode !== 'graph') return; // Q isn't editable in the sliders view
      e.preventDefault();
      const band = current[index];
      if (!band) return;
      const fine = e.shiftKey ? 1 / 8 : 1;
      const q = clamp(band.q * Math.exp(-e.deltaY * 0.003 * fine), EQ_MIN_Q, EQ_MAX_Q);
      updateBand(index, { q });
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [updateBand]);

  // Slider drag → gain-only band update.
  const handleSliderGain = useCallback(
    (index: number, gainDb: number) =>
      updateBand(index, { gainDb: clamp(gainDb, -EQ_MAX_ABS_GAIN_DB, EQ_MAX_ABS_GAIN_DB) }),
    [updateBand]
  );
  const handleSliderDragState = useCallback((dragging: boolean) => {
    draggingRef.current = dragging;
  }, []);

  // --- curve --------------------------------------------------------------
  const curve = useMemo(() => {
    const response = eqResponseDb(bands, sampleRate, CURVE_FREQS);
    const points = response.map((db, i) => {
      const x = (i / (CURVE_POINTS - 1)) * GRAPH_W;
      const y = clamp(gainToY(db), -8, GRAPH_H + 8);
      return `${x.toFixed(1)} ${y.toFixed(1)}`;
    });
    const line = `M${points.join(' L')}`;
    const zeroY = gainToY(0).toFixed(1);
    const area = `${line} L${GRAPH_W} ${zeroY} L0 ${zeroY} Z`;
    return { line, area };
  }, [bands, sampleRate]);

  const selectedBand = bands[selected];
  const typeOptions = eqBandTypeOptions(selected);

  const handleTypeChange = useCallback(
    (type: EqBandType) => {
      const patch: Partial<EqBand> = { type };
      const defaultQ = TYPE_DEFAULT_Q[type];
      if (defaultQ !== undefined) patch.q = defaultQ;
      updateBand(selected, patch);
    },
    [selected, updateBand]
  );

  // Chip text entry -> band updates (clamped like their drag equivalents).
  const commitFreq = useCallback(
    (raw: string) => {
      const parsed = parseFreqInput(raw);
      if (parsed === null) return;
      const lo = selected > 0 ? bands[selected - 1].freqHz * 1.02 : EQ_MIN_FREQ_HZ;
      const hi = selected < bands.length - 1 ? bands[selected + 1].freqHz * 0.98 : EQ_MAX_FREQ_HZ;
      updateBand(selected, { freqHz: clamp(parsed, lo, hi) });
    },
    [bands, selected, updateBand]
  );
  const commitGain = useCallback(
    (raw: string) => {
      const parsed = Number.parseFloat(raw.replace(',', '.'));
      if (!Number.isFinite(parsed)) return;
      updateBand(selected, { gainDb: clamp(parsed, -EQ_MAX_ABS_GAIN_DB, EQ_MAX_ABS_GAIN_DB) });
    },
    [selected, updateBand]
  );
  const commitQ = useCallback(
    (raw: string) => {
      const parsed = Number.parseFloat(raw.replace(',', '.'));
      if (!Number.isFinite(parsed)) return;
      updateBand(selected, { q: clamp(parsed, EQ_MIN_Q, EQ_MAX_Q) });
    },
    [selected, updateBand]
  );

  const chipStyle: React.CSSProperties = {
    display: 'flex',
    alignItems: 'center',
    gap: '6px',
    height: `${TEXT_BOX_HEIGHT}px`,
    padding: '0 4px',
    borderRadius: ICON_BOX_RADIUS,
    border: 'none',
    backgroundColor: SEGMENTED_TRACK,
    boxSizing: 'border-box',
    whiteSpace: 'nowrap',
  };

  return (
    <div
      ref={containerRef}
      style={{
        flex: 1,
        minHeight: 0,
        position: 'relative',
        backgroundColor: '#000000',
      }}
    >
      {/* Spectrum/grid bleed edge-to-edge; floating chrome + sliders inset. */}
      {view === 'sliders' ? (
        <>
          {/* Spectrum bleeds behind the inset faders. */}
          <svg
            width="100%"
            height="100%"
            viewBox={`0 0 ${GRAPH_W} ${GRAPH_H}`}
            preserveAspectRatio="none"
            style={{ display: 'block', position: 'absolute', inset: 0 }}
          >
            <SpectrumBackdrop blockId={blockId} />
          </svg>
          <div style={{ position: 'absolute', inset: 0, opacity: eqEnabled ? 1 : 0.4 }}>
            <EqSliders
              bands={bands}
              onGainChange={handleSliderGain}
              onDragStateChange={handleSliderDragState}
            />
          </div>
        </>
      ) : (
        <svg
          ref={graphRef}
          width="100%"
          height="100%"
          viewBox={`0 0 ${GRAPH_W} ${GRAPH_H}`}
          preserveAspectRatio="none"
          style={{ display: 'block', position: 'absolute', inset: 0 }}
        >
          {/* Grid */}
          {GRID_FREQS.map((f) => {
            const x = freqToNorm(f) * GRAPH_W;
            return (
              <g key={f}>
                <line
                  x1={x}
                  y1={0}
                  x2={x}
                  y2={GRAPH_H}
                  stroke="rgba(235, 235, 245, 0.07)"
                  strokeWidth={1}
                />
                {GRID_LABELS[f] && (
                  <text
                    x={x + 4}
                    y={GRAPH_H - BODY_PADDING}
                    fill="rgba(235, 235, 245, 0.35)"
                    fontSize={9}
                  >
                    {GRID_LABELS[f]}
                  </text>
                )}
              </g>
            );
          })}
          {[-7.5, 7.5].map((db) => (
            <line
              key={db}
              x1={0}
              y1={gainToY(db)}
              x2={GRAPH_W}
              y2={gainToY(db)}
              stroke="rgba(235, 235, 245, 0.05)"
              strokeWidth={1}
            />
          ))}
          {/* 0 dB line */}
          <line
            x1={0}
            y1={gainToY(0)}
            x2={GRAPH_W}
            y2={gainToY(0)}
            stroke="rgba(235, 235, 245, 0.18)"
            strokeWidth={1}
          />

          <SpectrumBackdrop blockId={blockId} />

          {/* EQ curve + dots (dimmed while the EQ is bypassed) */}
          <g opacity={eqEnabled ? 1 : 0.35}>
            <path d={curve.area} fill={CURVE_COLOR} opacity={0.1} />
            <path d={curve.line} fill="none" stroke={CURVE_COLOR} strokeWidth={1.25} />

            {/* Band dots */}
            {bands.map((band, i) => {
              const cx = freqToNorm(band.freqHz) * GRAPH_W;
              const cy = hasGain(band.type) ? gainToY(band.gainDb) : gainToY(0);
              const isSelected = i === selected;
              return (
                <g key={i}>
                  {isSelected && (
                    <circle
                      cx={cx}
                      cy={cy}
                      r={9}
                      fill="none"
                      stroke="#ffffff"
                      strokeWidth={1.5}
                      opacity={0.9}
                    />
                  )}
                  <circle
                    cx={cx}
                    cy={cy}
                    r={5.5}
                    fill={isSelected ? '#FFFFFF' : '#B8B8BE'}
                    stroke="#000000"
                    strokeWidth={1.5}
                    style={{ cursor: 'grab', touchAction: 'none' }}
                    {...helpProps(HELP.eqDot)}
                    onPointerDown={handleDotPointerDown(i)}
                    onPointerMove={handleDotPointerMove(i)}
                    onPointerUp={handleDotPointerUp}
                    onPointerCancel={handleDotPointerUp}
                  />
                </g>
              );
            })}
          </g>
        </svg>
      )}

      {/* Floating: band readout (top-left, graph view only). Spectrum/grid
          bleed edge-to-edge; chrome keeps the card's BODY_PADDING gutters. */}
      {view === 'graph' && (
        <div
          style={{
            position: 'absolute',
            top: `${BODY_PADDING}px`,
            left: `${BODY_PADDING}px`,
            fontSize: '11px',
            color: MUTED,
            pointerEvents: 'none',
            textShadow: '0 1px 2px rgba(0, 0, 0, 0.9)',
          }}
        >
          <span style={{ color: '#FFFFFF' }}>Band {selected + 1}</span>
          {' · '}
          {BAND_TYPE_LABELS[selectedBand?.type ?? 'bell']}
        </div>
      )}

      {/* Floating: type selector + selected band readouts (bottom-left, graph
          view only). Nudged 12px above the BODY_PADDING floor so the chips
          clear the Hz axis labels. */}
      {view === 'graph' && (
        <div
          style={{
            position: 'absolute',
            bottom: `${BODY_PADDING + 12}px`,
            left: `${BODY_PADDING}px`,
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
          }}
        >
          {/* Curve type: outer bands choose shelf vs pass; bells show their
            single (active) option so the selected shape is always visible.
            Same track language as LITE/FULL: fill + white/grey icons. */}
          <div style={segmentedGroupStyle()}>
            {typeOptions.map((type) => {
              const active = selectedBand?.type === type;
              return (
                <button
                  key={type}
                  onClick={() => handleTypeChange(type)}
                  {...helpProps(bandTypeHelp(BAND_TYPE_LABELS[type]))}
                  style={{
                    ...segmentedCellStyle(true),
                    cursor: typeOptions.length > 1 ? 'pointer' : 'default',
                    color: active ? '#ffffff' : MUTED,
                  }}
                >
                  <svg
                    width={ICON_SIZE}
                    height={ICON_SIZE}
                    viewBox="0 0 16 14"
                    style={{ display: 'block', flexShrink: 0 }}
                  >
                    <path
                      d={TYPE_GLYPHS[type]}
                      fill="none"
                      stroke="currentColor"
                      strokeWidth={1.6}
                      strokeLinecap="round"
                    />
                  </svg>
                </button>
              );
            })}
          </div>

          {/* Value widths fit each chip's longest reading ("999 Hz",
              "-15.0 dB", "10.00") so the row never shifts. */}
          <EditableChip
            label="Freq"
            text={formatFreq(selectedBand?.freqHz ?? 0)}
            editText={Math.round(selectedBand?.freqHz ?? 0).toString()}
            valueWidth={42}
            onCommit={commitFreq}
            help={HELP.eqFreqChip}
            style={chipStyle}
          />
          <EditableChip
            label="Gain"
            text={
              hasGain(selectedBand?.type ?? 'bell')
                ? `${(selectedBand?.gainDb ?? 0).toFixed(1)} dB`
                : '—'
            }
            editText={(selectedBand?.gainDb ?? 0).toFixed(1)}
            valueWidth={52}
            onCommit={commitGain}
            disabled={!hasGain(selectedBand?.type ?? 'bell')}
            help={HELP.eqGainChip}
            style={{ ...chipStyle, opacity: hasGain(selectedBand?.type ?? 'bell') ? 1 : 0.4 }}
          />
          <EditableChip
            label="Q"
            text={(selectedBand?.q ?? 1).toFixed(2)}
            editText={(selectedBand?.q ?? 1).toFixed(2)}
            valueWidth={34}
            onCommit={commitQ}
            help={HELP.eqQChip}
            style={chipStyle}
          />
        </div>
      )}
    </div>
  );
};
