import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { EqBand, EqBandType } from '../types/chain';
import {
  EQ_MAX_ABS_GAIN_DB,
  EQ_MAX_Q,
  EQ_MIN_Q,
  EQ_MIN_FREQ_HZ,
  EQ_MAX_FREQ_HZ,
  EQ_NUM_BANDS,
  eqBandTypeOptions,
} from '../types/chain';
import { eqResponseDb, formatFreq, freqToNorm, normToFreq } from './eqMath';
import { useBlockSpectrum, SPECTRUM_MIN_DB } from '../hooks/useBlockSpectrum';
import { CARD_WIDTH } from './chainLayout';

/**
 * 6-band EQ editor shown in the card body while the header EQ toggle is
 * active, with two interchangeable views (chosen via the EQ menu that lives
 * in the card header while the editor is open — see ChainBlock):
 *
 * - Graph: the full editor. Grid bleeds edge-to-edge, controls float over
 *   it. Drag dots for freq/gain (vertical drag tunes Q on cut bands),
 *   scroll to tune the selected band's Q.
 * - Sliders: a Mesa-style graphic EQ mirroring the same bands. Sliders move
 *   gain only; frequency, type (shelf/pass on the outer bands) and Q are
 *   shown but only editable in the graph view.
 *
 * The spectrum backdrop lives in its own leaf component so its ~30 fps
 * updates never re-render the editor; both views render it.
 *
 * Band roles are fixed channel-strip style (enforced natively too): band 1 is
 * low cut/low shelf, band 6 is high cut/high shelf, bands 2-5 are bells.
 */

export type EqViewMode = 'graph' | 'sliders';

// Full card body: card width minus 2px border; 216 card - 2px border - 40 header tall.
const GRAPH_W = CARD_WIDTH - 2;
const GRAPH_H = 174;
const GRAPH_PAD_Y = 12; // keep dots inside the frame at ±15 dB

const MUTED = 'rgba(235, 235, 245, 0.60)';
const SUBTLE = 'rgba(235, 235, 245, 0.40)';
const BORDER = '1px solid rgba(84, 84, 88, 0.65)';
const OVERLAY_BG = 'rgba(0, 0, 0, 0.72)';

// Controls stay black/white/gray (like the knobs); the only color in the EQ
// is the brand-gradient spectrum behind everything.
const CURVE_COLOR = '#8E8E93';

const BAND_TYPE_LABELS: Record<EqBandType, string> = {
  lowcut: 'Low Cut',
  lowshelf: 'Low Shelf',
  bell: 'Bell',
  highshelf: 'High Shelf',
  highcut: 'High Cut',
};

/** 16x14 curve glyphs for the type selector. */
const TYPE_GLYPHS: Record<EqBandType, string> = {
  lowshelf: 'M1 11 C5 11 6 3 10 3 L15 3',
  bell: 'M1 11 C4 11 5 3 8 3 C11 3 12 11 15 11',
  highshelf: 'M1 3 C5 3 6 11 10 11 L15 11',
  lowcut: 'M1 13 C4 13 5 3 9 3 L15 3',
  highcut: 'M1 3 L7 3 C11 3 12 13 15 13',
};

/** Q defaults applied when switching a band's curve type. */
const TYPE_DEFAULT_Q: Partial<Record<EqBandType, number>> = {
  lowcut: 0.71,
  highcut: 0.71,
  lowshelf: 0.71,
  highshelf: 0.71,
};

const hasGain = (type: EqBandType) =>
  type === 'bell' || type === 'lowshelf' || type === 'highshelf';

const gainToY = (gainDb: number) =>
  GRAPH_H / 2 - (gainDb / EQ_MAX_ABS_GAIN_DB) * (GRAPH_H / 2 - GRAPH_PAD_Y);
const yToGain = (y: number) =>
  ((GRAPH_H / 2 - y) / (GRAPH_H / 2 - GRAPH_PAD_Y)) * EQ_MAX_ABS_GAIN_DB;

const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), hi);

// One sample per horizontal pixel so even a Q=10 spike is drawn cleanly
// (coarse sampling clips the narrow notch into a flat-bottomed wedge).
const CURVE_POINTS = GRAPH_W;
const CURVE_FREQS = Array.from({ length: CURVE_POINTS }, (_, i) =>
  normToFreq(i / (CURVE_POINTS - 1))
);

const GRID_FREQS = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const GRID_LABELS: Record<number, string> = { 100: '100', 1000: '1k', 10000: '10k' };

/**
 * Spectrum backdrop — isolated so 30 fps polling re-renders only this leaf.
 * Filled with the brand meter ramp (blue at the bottom → yellow → red at the
 * top), so louder content climbs into the red just like the meters.
 */
const SpectrumBackdrop: React.FC<{ blockId: string }> = ({ blockId }) => {
  const bins = useBlockSpectrum(blockId);
  const path = useMemo(() => {
    if (bins.length < 2) return '';
    // Display window: -80..0 dB across the graph height.
    const topDb = 0;
    const bottomDb = Math.max(SPECTRUM_MIN_DB, -80);
    const points = bins.map((db, i) => {
      const x = (i / (bins.length - 1)) * GRAPH_W;
      const t = clamp((db - bottomDb) / (topDb - bottomDb), 0, 1);
      return `${x.toFixed(1)} ${(GRAPH_H * (1 - t)).toFixed(1)}`;
    });
    return `M0 ${GRAPH_H} L${points.join(' L')} L${GRAPH_W} ${GRAPH_H} Z`;
  }, [bins]);

  if (!path) return null;
  const gradientId = `eq-spectrum-${blockId}`;
  return (
    <>
      <defs>
        <linearGradient
          id={gradientId}
          gradientUnits="userSpaceOnUse"
          x1="0"
          y1={GRAPH_H}
          x2="0"
          y2="0"
        >
          {/* Yellow/red pulled down so hot content reads red well before 0 dBFS */}
          <stop offset="0%" stopColor="#0000FF" />
          <stop offset="40%" stopColor="#FFFF00" />
          <stop offset="70%" stopColor="#FF0000" />
          <stop offset="100%" stopColor="#FF0000" />
        </linearGradient>
      </defs>
      <path
        d={path}
        fill={`url(#${gradientId})`}
        fillOpacity={0.3}
        stroke="rgba(235, 235, 245, 0.2)"
        strokeWidth={1}
      />
    </>
  );
};

// --- Mesa-style sliders view ------------------------------------------------

const SLIDER_DB_MARKS = [15, 7.5, 0, -7.5, -15];
const SLIDER_FREQ_ROW_H = 16;

// Same grayscale ramp as the knob sweep (KnobInner), laid out vertically:
// dark at the bottom of the travel, white at the cap.
const SLIDER_FILL_GRADIENT =
  'linear-gradient(to top, rgba(30, 30, 30, 0.3), rgba(50, 50, 50, 0.6) 20%, ' +
  'rgba(100, 100, 100, 0.75) 40%, rgba(160, 160, 160, 0.85) 60%, ' +
  'rgba(220, 220, 220, 0.9) 80%, rgba(255, 255, 255, 1))';

interface EqSlidersProps {
  bands: EqBand[];
  onGainChange: (index: number, gainDb: number) => void;
  /** Mirrors the graph view's drag flag so prop syncs pause mid-drag. */
  onDragStateChange: (dragging: boolean) => void;
  /** Fader scale, matching the card knobs' size prop. Cap width = size. */
  size?: number;
}

const EqSliders: React.FC<EqSlidersProps> = ({
  bands,
  onGainChange,
  onDragStateChange,
  size = 30,
}) => {
  const draggingIndexRef = useRef<number | null>(null);

  // All fader dimensions derive from `size` (like the knobs): the cap is
  // `size` wide, roughly half as tall, riding a track ~size/4 wide.
  const capW = size;
  const capH = Math.round(size * 0.47);
  const trackW = Math.max(4, Math.round(size * 0.27));
  const thumbPad = Math.ceil(capH / 2); // keeps the cap inside the travel

  const gainFromEvent = (e: React.PointerEvent<HTMLDivElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const travel = Math.max(1, rect.height - 2 * thumbPad);
    const t = clamp((e.clientY - rect.top - thumbPad) / travel, 0, 1);
    return (1 - t) * 2 * EQ_MAX_ABS_GAIN_DB - EQ_MAX_ABS_GAIN_DB;
  };

  const handlePointerDown = (index: number) => (e: React.PointerEvent<HTMLDivElement>) => {
    if (!hasGain(bands[index].type)) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    draggingIndexRef.current = index;
    onDragStateChange(true);
    onGainChange(index, gainFromEvent(e));
  };

  const handlePointerMove = (index: number) => (e: React.PointerEvent<HTMLDivElement>) => {
    if (draggingIndexRef.current !== index) return;
    onGainChange(index, gainFromEvent(e));
  };

  const handlePointerUp = () => {
    draggingIndexRef.current = null;
    onDragStateChange(false);
  };

  const rowCellStyle: React.CSSProperties = {
    flex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    minWidth: 0,
  };

  return (
    <div
      style={{
        position: 'absolute',
        inset: 0,
        display: 'flex',
        padding: '10px 18px 6px',
        boxSizing: 'border-box',
      }}
    >
      {/* dB axis, aligned to the thumbs' travel */}
      <div
        style={{
          width: '30px',
          flexShrink: 0,
          position: 'relative',
          marginTop: `${thumbPad}px`,
          marginBottom: `${SLIDER_FREQ_ROW_H + thumbPad}px`,
        }}
      >
        {[15, 0, -15].map((db) => (
          <span
            key={db}
            style={{
              position: 'absolute',
              right: '8px',
              top: `${(1 - (db + EQ_MAX_ABS_GAIN_DB) / (2 * EQ_MAX_ABS_GAIN_DB)) * 100}%`,
              transform: 'translateY(-50%)',
              fontSize: '9px',
              color: SUBTLE,
              whiteSpace: 'nowrap',
            }}
          >
            {db > 0 ? `+${db}` : db}
          </span>
        ))}
      </div>

      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0 }}>
        {/* Tracks */}
        <div style={{ flex: 1, position: 'relative', minHeight: 0 }}>
          {/* dB grid lines across the travel region */}
          <div
            style={{
              position: 'absolute',
              left: 0,
              right: 0,
              top: `${thumbPad}px`,
              bottom: `${thumbPad}px`,
              pointerEvents: 'none',
            }}
          >
            {SLIDER_DB_MARKS.map((db) => (
              <div
                key={db}
                style={{
                  position: 'absolute',
                  left: 0,
                  right: 0,
                  top: `${(1 - (db + EQ_MAX_ABS_GAIN_DB) / (2 * EQ_MAX_ABS_GAIN_DB)) * 100}%`,
                  height: '1px',
                  backgroundColor: `rgba(235, 235, 245, ${db === 0 ? 0.18 : 0.06})`,
                }}
              />
            ))}
          </div>

          <div style={{ position: 'absolute', inset: 0, display: 'flex' }}>
            {bands.map((band, i) => {
              const editable = hasGain(band.type);
              const gain = editable ? band.gainDb : 0;
              const t = (gain + EQ_MAX_ABS_GAIN_DB) / (2 * EQ_MAX_ABS_GAIN_DB);
              return (
                <div
                  key={i}
                  onPointerDown={handlePointerDown(i)}
                  onPointerMove={handlePointerMove(i)}
                  onPointerUp={handlePointerUp}
                  onPointerCancel={handlePointerUp}
                  title={
                    editable
                      ? `${formatFreq(band.freqHz)} · ${band.gainDb.toFixed(1)} dB`
                      : 'Pass filter — adjust in the graph view'
                  }
                  style={{
                    flex: 1,
                    position: 'relative',
                    minWidth: 0,
                    cursor: editable ? 'ns-resize' : 'default',
                    opacity: editable ? 1 : 0.4,
                    touchAction: 'none',
                  }}
                >
                  {/* Track: sharp-edged, same base gray as the knob ring */}
                  <div
                    style={{
                      position: 'absolute',
                      left: '50%',
                      transform: 'translateX(-50%)',
                      top: 0,
                      bottom: 0,
                      width: `${trackW}px`,
                      backgroundColor: 'rgba(50, 50, 50, 0.8)',
                    }}
                  />
                  {/* Value fill: bottom of the travel up to the cap, dark →
                      white toward the cap — exactly like the knob sweep. */}
                  {t > 0.001 && (
                    <div
                      style={{
                        position: 'absolute',
                        left: '50%',
                        transform: 'translateX(-50%)',
                        width: `${trackW}px`,
                        bottom: `${thumbPad}px`,
                        height: `calc((100% - ${2 * thumbPad}px) * ${t.toFixed(4)})`,
                        background: SLIDER_FILL_GRADIENT,
                      }}
                    />
                  )}
                  {/* Fader cap (knob-handle style: white with a black center line) */}
                  <div
                    style={{
                      position: 'absolute',
                      left: '50%',
                      top: `calc((100% - ${2 * thumbPad}px) * ${(1 - t).toFixed(4)} + ${thumbPad}px)`,
                      transform: 'translate(-50%, -50%)',
                      width: `${capW}px`,
                      height: `${capH}px`,
                      background: 'linear-gradient(180deg, #FFFFFF 0%, #D6D6DB 100%)',
                      border: '1px solid #000000',
                      boxSizing: 'border-box',
                      display: 'flex',
                      alignItems: 'center',
                    }}
                  >
                    <div style={{ width: '100%', height: '2px', backgroundColor: '#000000' }} />
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Frequency labels; the outer bands get their curve glyph so the
            shelf/pass role is visible without a dedicated row. */}
        <div style={{ height: `${SLIDER_FREQ_ROW_H}px`, display: 'flex', flexShrink: 0 }}>
          {bands.map((band, i) => {
            const showGlyph = i === 0 || i === EQ_NUM_BANDS - 1;
            return (
              <span key={i} style={{ ...rowCellStyle, gap: '4px', fontSize: '10px', color: SUBTLE }}>
                {showGlyph && (
                  <svg width={12} height={11} viewBox="0 0 16 14" style={{ flexShrink: 0 }}>
                    <path
                      d={TYPE_GLYPHS[band.type]}
                      fill="none"
                      stroke={SUBTLE}
                      strokeWidth={1.6}
                      strokeLinecap="round"
                    />
                  </svg>
                )}
                {formatFreq(band.freqHz)}
              </span>
            );
          })}
        </div>
      </div>
    </div>
  );
};

interface BlockEqViewProps {
  blockId: string;
  bands: EqBand[];
  /** EQ power state — a bypassed EQ renders its curve/dots dimmed. */
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
  // Optimistic local bands; native converges via chain-state polling. Skip
  // prop syncs mid-drag so a stale poll can't fight the pointer.
  const [bands, setBands] = useState<EqBand[]>(bandsProp);
  const [selected, setSelected] = useState(1);
  const draggingRef = useRef(false);
  const dragStateRef = useRef<{ index: number; startQ: number; startY: number } | null>(null);
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

  const handleDotPointerDown = useCallback(
    (index: number) => (e: React.PointerEvent<SVGCircleElement>) => {
      e.preventDefault();
      e.currentTarget.setPointerCapture(e.pointerId);
      setSelected(index);
      draggingRef.current = true;
      const { y } = graphPointFromEvent(e);
      dragStateRef.current = { index, startQ: bands[index].q, startY: y };
    },
    [bands, graphPointFromEvent]
  );

  const handleDotPointerMove = useCallback(
    (index: number) => (e: React.PointerEvent<SVGCircleElement>) => {
      if (!draggingRef.current || dragStateRef.current?.index !== index) return;
      const { x, y } = graphPointFromEvent(e);
      // Bands keep their left-to-right order: clamp between the neighbors'
      // frequencies (with a hair of margin so dots never sit exactly on top).
      const lo = index > 0 ? bands[index - 1].freqHz * 1.02 : EQ_MIN_FREQ_HZ;
      const hi = index < bands.length - 1 ? bands[index + 1].freqHz * 0.98 : EQ_MAX_FREQ_HZ;
      const freqHz = clamp(normToFreq(clamp(x / GRAPH_W, 0, 1)), lo, hi);
      if (hasGain(bands[index].type)) {
        const gainDb = clamp(yToGain(y), -EQ_MAX_ABS_GAIN_DB, EQ_MAX_ABS_GAIN_DB);
        updateBand(index, { freqHz, gainDb });
      } else {
        // Cuts have no gain — vertical drag tunes Q instead (up = tighter).
        const drag = dragStateRef.current;
        const q = clamp(drag.startQ * Math.exp((drag.startY - y) * 0.02), EQ_MIN_Q, EQ_MAX_Q);
        updateBand(index, { freqHz, q });
      }
    },
    [bands, graphPointFromEvent, updateBand]
  );

  const handleDotPointerUp = useCallback(() => {
    draggingRef.current = false;
    dragStateRef.current = null;
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
      const q = clamp(band.q * Math.exp(-e.deltaY * 0.003), EQ_MIN_Q, EQ_MAX_Q);
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

  const chipStyle: React.CSSProperties = {
    display: 'flex',
    alignItems: 'center',
    gap: '6px',
    height: '28px',
    padding: '0 10px',
    borderRadius: '8px',
    border: BORDER,
    backgroundColor: OVERLAY_BG,
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
      {/* Grid: bleeds to the card body edges, everything else floats on top */}
      {view === 'sliders' ? (
        <>
          {/* Spectrum bleeds edge-to-edge behind the sliders too */}
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
              <line x1={x} y1={0} x2={x} y2={GRAPH_H} stroke="rgba(235, 235, 245, 0.07)" strokeWidth={1} />
              {GRID_LABELS[f] && (
                <text x={x + 4} y={GRAPH_H - 5} fill="rgba(235, 235, 245, 0.35)" fontSize={9}>
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
        <path d={curve.area} fill={CURVE_COLOR} opacity={0.10} />
        <path d={curve.line} fill="none" stroke={CURVE_COLOR} strokeWidth={1.25} />

        {/* Band dots */}
        {bands.map((band, i) => {
          const cx = freqToNorm(band.freqHz) * GRAPH_W;
          const cy = hasGain(band.type) ? gainToY(band.gainDb) : gainToY(0);
          const isSelected = i === selected;
          return (
            <g key={i}>
              {isSelected && (
                <circle cx={cx} cy={cy} r={9} fill="none" stroke="#ffffff" strokeWidth={1.5} opacity={0.9} />
              )}
              <circle
                cx={cx}
                cy={cy}
                r={5.5}
                fill={isSelected ? '#FFFFFF' : '#B8B8BE'}
                stroke="#000000"
                strokeWidth={1.5}
                style={{ cursor: 'grab', touchAction: 'none' }}
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

      {/* Floating: band readout (top-left, graph view only) */}
      {view === 'graph' && (
        <div
          style={{
            position: 'absolute',
            top: '6px',
            left: '10px',
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

      {/* Floating: type selector + selected band readouts (bottom-left, graph view only) */}
      {view === 'graph' && (
      <div
        style={{
          position: 'absolute',
          bottom: '10px',
          left: '10px',
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
        }}
      >
        {/* Curve type: outer bands choose shelf vs pass; bells show their
            single (active) option so the selected shape is always visible. */}
        <div
          style={{
            display: 'flex',
            height: '28px',
            borderRadius: '8px',
            border: BORDER,
            overflow: 'hidden',
            backgroundColor: OVERLAY_BG,
            flexShrink: 0,
          }}
        >
          {typeOptions.map((type, i) => {
            const active = selectedBand?.type === type;
            return (
              <button
                key={type}
                onClick={() => handleTypeChange(type)}
                title={BAND_TYPE_LABELS[type]}
                style={{
                  width: '32px',
                  height: '100%',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  border: 'none',
                  borderLeft: i > 0 ? BORDER : 'none',
                  cursor: typeOptions.length > 1 ? 'pointer' : 'default',
                  backgroundColor: active ? 'rgba(235, 235, 245, 0.16)' : 'transparent',
                  padding: 0,
                }}
              >
                <svg width={16} height={14} viewBox="0 0 16 14">
                  <path
                    d={TYPE_GLYPHS[type]}
                    fill="none"
                    stroke={active ? '#FFFFFF' : MUTED}
                    strokeWidth={1.6}
                    strokeLinecap="round"
                  />
                </svg>
              </button>
            );
          })}
        </div>

        <div style={chipStyle}>
          <span style={{ fontSize: '11px', color: SUBTLE }}>Freq</span>
          <span style={{ fontSize: '12px', color: '#ffffff' }}>
            {formatFreq(selectedBand?.freqHz ?? 0)}
          </span>
        </div>
        <div style={{ ...chipStyle, opacity: hasGain(selectedBand?.type ?? 'bell') ? 1 : 0.4 }}>
          <span style={{ fontSize: '11px', color: SUBTLE }}>Gain</span>
          <span style={{ fontSize: '12px', color: '#ffffff' }}>
            {hasGain(selectedBand?.type ?? 'bell')
              ? `${(selectedBand?.gainDb ?? 0).toFixed(1)} dB`
              : '—'}
          </span>
        </div>
        <div style={chipStyle} title="Scroll over the graph to adjust Q">
          <span style={{ fontSize: '11px', color: SUBTLE }}>Q</span>
          <span style={{ fontSize: '12px', color: '#ffffff' }}>
            {(selectedBand?.q ?? 1).toFixed(2)}
          </span>
        </div>
      </div>
      )}
    </div>
  );
};
