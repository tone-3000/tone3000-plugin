import React, { useEffect, useRef, useState } from 'react';
import type { EqBand } from '../types/chain';
import { EQ_MAX_ABS_GAIN_DB, EQ_NUM_BANDS } from '../types/chain';
import { formatFreq } from './eqMath';
import { clamp, hasGain, TYPE_GLYPHS } from './eqShared';
import { BODY_PADDING } from './chainLayout';
import { HELP, helpProps, pinHelp, unpinHelp } from './helpText';

/**
 * Mesa-style graphic-EQ view: six gain faders mirroring the same bands as
 * the graph editor. Sliders move gain only; frequency, type (shelf/pass on
 * the outer bands) and Q are shown but only editable in the graph view.
 *
 * Interaction conventions (mirroring KnobControl):
 * - Shift+drag = 8x finer control (delta-based, so it can toggle mid-drag).
 * - Alt/Option-click or double-click a fader resets its gain to 0 (the
 *   mixer-fader convention).
 * - While dragging, the band's frequency label swaps to a live dB readout
 *   and lingers briefly after release.
 */

/** How long the dB readout lingers after the pointer releases. */
const READOUT_HOLD_MS = 250;

const SLIDER_DB_MARKS = [15, 10, 5, 0, -5, -10, -15];
const SLIDER_FREQ_ROW_H = 16;

/** Same chrome as parametric Hz marks (BlockEqView grid labels). */
const AXIS_LABEL_COLOR = 'rgba(235, 235, 245, 0.35)';
/** Matches the parametric Hz marks' offset from the graph edge. */
const AXIS_LABEL_INSET = 4;

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

export const EqSliders: React.FC<EqSlidersProps> = ({
  bands,
  onGainChange,
  onDragStateChange,
  size = 30,
}) => {
  const draggingIndexRef = useRef<number | null>(null);
  const lastYRef = useRef(0);
  // Which band's label shows the dB readout (dragging, or lingering).
  const [readoutIndex, setReadoutIndex] = useState<number | null>(null);
  const holdTimerRef = useRef<number | null>(null);
  useEffect(
    () => () => {
      if (holdTimerRef.current !== null) window.clearTimeout(holdTimerRef.current);
    },
    []
  );

  // All fader dimensions derive from `size` (like the knobs): the cap is
  // `size` wide, roughly half as tall, riding a track ~size/4 wide.
  const capW = size;
  const capH = Math.round(size * 0.47);
  const trackW = Math.max(4, Math.round(size * 0.27));
  const thumbPad = Math.ceil(capH / 2); // keeps the cap inside the travel

  const travelOf = (e: React.PointerEvent<HTMLDivElement>) =>
    Math.max(1, e.currentTarget.getBoundingClientRect().height - 2 * thumbPad);

  const gainFromEvent = (e: React.PointerEvent<HTMLDivElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const t = clamp((e.clientY - rect.top - thumbPad) / travelOf(e), 0, 1);
    return (1 - t) * 2 * EQ_MAX_ABS_GAIN_DB - EQ_MAX_ABS_GAIN_DB;
  };

  const handlePointerDown = (index: number) => (e: React.PointerEvent<HTMLDivElement>) => {
    if (!hasGain(bands[index].type)) return;
    e.preventDefault();
    if (e.altKey) {
      onGainChange(index, 0);
      return;
    }
    e.currentTarget.setPointerCapture(e.pointerId);
    draggingIndexRef.current = index;
    lastYRef.current = e.clientY;
    if (holdTimerRef.current !== null) window.clearTimeout(holdTimerRef.current);
    setReadoutIndex(index);
    // Keep the hint up while the (captured) drag runs off the fader column.
    pinHelp(HELP.eqFader);
    onDragStateChange(true);
    // Plain grab jumps the cap to the pointer; a Shift-grab holds position so
    // fine adjustment starts from the current value.
    if (!e.shiftKey) onGainChange(index, gainFromEvent(e));
  };

  const handlePointerMove = (index: number) => (e: React.PointerEvent<HTMLDivElement>) => {
    if (draggingIndexRef.current !== index) return;
    const dY = e.clientY - lastYRef.current;
    lastYRef.current = e.clientY;
    if (e.shiftKey) {
      // Fine mode: delta-based at 1/8 speed, from the current value.
      const dGain = (-dY / travelOf(e)) * 2 * EQ_MAX_ABS_GAIN_DB * (1 / 8);
      onGainChange(
        index,
        clamp(bands[index].gainDb + dGain, -EQ_MAX_ABS_GAIN_DB, EQ_MAX_ABS_GAIN_DB)
      );
    } else {
      onGainChange(index, gainFromEvent(e));
    }
  };

  const handlePointerUp = () => {
    if (draggingIndexRef.current === null) return;
    draggingIndexRef.current = null;
    unpinHelp(HELP.eqFader);
    onDragStateChange(false);
    if (holdTimerRef.current !== null) window.clearTimeout(holdTimerRef.current);
    holdTimerRef.current = window.setTimeout(() => setReadoutIndex(null), READOUT_HOLD_MS);
  };

  const dbTop = (db: number) =>
    `${(1 - (db + EQ_MAX_ABS_GAIN_DB) / (2 * EQ_MAX_ABS_GAIN_DB)) * 100}%`;

  return (
    <div
      style={{
        position: 'absolute',
        inset: 0,
        display: 'flex',
        flexDirection: 'column',
        // Spectrum/dB rules bleed edge-to-edge behind; faders + labels keep
        // the card body's BODY_PADDING gutters.
        padding: `${BODY_PADDING}px 0 ${AXIS_LABEL_INSET}px`,
        boxSizing: 'border-box',
      }}
    >
      {/* Fader travel region */}
      <div style={{ flex: 1, position: 'relative', minHeight: 0 }}>
        {/* dB grid + its numerals, spanning the travel edge to edge */}
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
                top: dbTop(db),
                height: '1px',
                backgroundColor: `rgba(235, 235, 245, ${db === 0 ? 0.18 : 0.06})`,
              }}
            />
          ))}
          {/* Numerals sit just under the lines in the left gutter so neither
              the rule nor a fader cap runs through the text. */}
          {SLIDER_DB_MARKS.map((db) => (
            <span
              key={`label-${db}`}
              style={{
                position: 'absolute',
                left: `${BODY_PADDING}px`,
                top: dbTop(db),
                transform: 'translateY(2px)',
                fontSize: '9px',
                lineHeight: 1,
                color: AXIS_LABEL_COLOR,
                whiteSpace: 'nowrap',
              }}
            >
              {db > 0 ? `+${db}` : db}
            </span>
          ))}
        </div>

        <div
          style={{
            position: 'absolute',
            inset: 0,
            display: 'flex',
            padding: `0 ${BODY_PADDING}px`,
          }}
        >
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
                onDoubleClick={() => editable && onGainChange(i, 0)}
                {...helpProps(editable ? HELP.eqFader : HELP.eqFaderPass)}
                style={{
                  flex: 1,
                  position: 'relative',
                  minWidth: 0,
                  cursor: editable ? 'ns-resize' : 'default',
                  opacity: editable ? 1 : 0.4,
                  touchAction: 'none',
                }}
              >
                {/* Track spans exactly -15..+15. At either extreme the cap's
                    center lands on the corresponding end of the track. */}
                <div
                  style={{
                    position: 'absolute',
                    left: '50%',
                    transform: 'translateX(-50%)',
                    top: `${thumbPad}px`,
                    bottom: `${thumbPad}px`,
                    width: `${trackW}px`,
                    backgroundColor: 'rgba(50, 50, 50, 0.8)',
                  }}
                />
                {/* Value fill: bottom of the travel up to the cap, dark →
                    white toward the cap, exactly like the knob sweep. */}
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

      {/* Frequency labels, centered under their fader and clear of the tracks.
          The outer bands carry their curve glyph so the shelf/pass role is
          visible without a dedicated row. */}
      <div
        style={{
          height: `${SLIDER_FREQ_ROW_H}px`,
          display: 'flex',
          flexShrink: 0,
          padding: `0 ${BODY_PADDING}px`,
          boxSizing: 'border-box',
        }}
      >
        {bands.map((band, i) => {
          const showGlyph = i === 0 || i === EQ_NUM_BANDS - 1;
          // Knob-style readout: the label swaps to the live gain while this
          // band is being dragged (and lingers briefly after).
          const showReadout = readoutIndex === i && hasGain(band.type);
          return (
            <span
              key={i}
              style={{
                flex: 1,
                minWidth: 0,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                gap: '4px',
                fontSize: '9px',
                color: showReadout ? '#ffffff' : AXIS_LABEL_COLOR,
                fontVariantNumeric: 'tabular-nums',
                whiteSpace: 'nowrap',
              }}
            >
              {showReadout ? (
                `${band.gainDb > 0 ? '+' : ''}${band.gainDb.toFixed(1)} dB`
              ) : (
                <>
                  {showGlyph && (
                    <svg width={12} height={11} viewBox="0 0 16 14" style={{ flexShrink: 0 }}>
                      <path
                        d={TYPE_GLYPHS[band.type]}
                        fill="none"
                        stroke={AXIS_LABEL_COLOR}
                        strokeWidth={1.6}
                        strokeLinecap="round"
                      />
                    </svg>
                  )}
                  {formatFreq(band.freqHz)}
                </>
              )}
            </span>
          );
        })}
      </div>
    </div>
  );
};
