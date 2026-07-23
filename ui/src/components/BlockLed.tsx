import React from 'react';
import { useMeter, useMeterClip } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB } from './meterColor';
import { HELP, helpProps } from './helpText';

interface BlockLedProps {
  /** Meter id from useMeters (e.g. meterId.blockOut(blockId)). */
  meterId: string;
  /** Dot diameter, px. */
  size?: number;
}

type Rgb = [number, number, number];

// Pure primaries matching the meter ramp (meterColor.ts).
const BLUE: Rgb = [0, 0, 255];
const YELLOW: Rgb = [255, 255, 0];
const RED: Rgb = [255, 0, 0];

/**
 * Noise-floor gate: NAM captures idle around -50…-40 dB, so we fade the glow
 * in across that band. Intensity itself tracks the full meter scale (-60→0)
 * so blur/opacity keep growing through -20 dB up to 0 dBFS (the old curve
 * topped out at -32 dB and felt stuck for the whole hot range).
 */
const GATE_OFF_DB = -50;
const GATE_ON_DB = -40;
const DARK: Rgb = [34, 34, 34];

const lerpRgb = (c0: Rgb, c1: Rgb, t: number): Rgb => [
  Math.round(c0[0] + (c1[0] - c0[0]) * t),
  Math.round(c0[1] + (c1[1] - c0[1]) * t),
  Math.round(c0[2] + (c1[2] - c0[2]) * t),
];

const dbToUnit = (db: number): number =>
  Math.min(1, Math.max(0, (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB)));

/** Level → color: blue/yellow plateaus with short crossfades, red at the top. */
const levelColor = (unit: number): Rgb => {
  const fade = 0.06;
  const toYellow = 0.5;
  const toRed = 0.85;
  if (unit < toYellow - fade) return BLUE;
  if (unit < toYellow + fade) return lerpRgb(BLUE, YELLOW, (unit - (toYellow - fade)) / (fade * 2));
  if (unit < toRed - fade) return YELLOW;
  if (unit < toRed + fade) return lerpRgb(YELLOW, RED, (unit - (toRed - fade)) / (fade * 2));
  return RED;
};

/**
 * Realtime level → color/presence for the gallery inset glow. Does not latch
 * on clip — that lives only on the corner clip dot.
 *
 * `presence` is gated against the noise floor, then scaled by the full-range
 * meter unit so intensity keeps moving from quiet through 0 dBFS.
 */
export function useBlockEnergy(meterId: string) {
  const db = useMeter(meterId);

  const unit = dbToUnit(db);
  const gate = Math.min(1, Math.max(0, (db - GATE_OFF_DB) / (GATE_ON_DB - GATE_OFF_DB)));
  // Gate kills hiss; unit carries intensity travel all the way to 0 dBFS.
  const presence = gate * unit;
  const color = lerpRgb(DARK, levelColor(unit), gate);

  return { color, unit, presence };
}

/**
 * Clip latch indicator for gallery tiles: a red dot that appears only while
 * clip is latched. Click to clear. No bezel / no level metering — the inset
 * glow handles realtime viz.
 */
export const BlockLed: React.FC<BlockLedProps> = React.memo(function BlockLed({
  meterId,
  size = 10,
}) {
  const [clipped, clearClip] = useMeterClip(meterId);
  if (!clipped) return null;

  return (
    <div
      onClick={(e) => {
        e.stopPropagation();
        clearClip();
      }}
      {...helpProps(HELP.clipDot)}
      style={{
        width: `${size}px`,
        height: `${size}px`,
        borderRadius: '50%',
        backgroundColor: 'rgb(255, 0, 0)',
        cursor: 'pointer',
        flexShrink: 0,
      }}
    />
  );
});

interface BlockEnergyBorderProps {
  /** Meter id from useMeters (e.g. meterId.blockOut(blockId)). */
  meterId: string;
  /** Match the tile's corner radius. */
  borderRadius?: number;
}

/** Inset blur into the artwork: presence 0 → 2px, presence 1 → 24px. */
const INSET_BLUR_MIN_PX = 2;
const INSET_BLUR_MAX_PX = 18;
/** Peak shadow alpha at full presence. */
const INSET_OPACITY = 0.75;

/**
 * Realtime inset energy glow on gallery tiles: blue→yellow→red / presence
 * from the live meter only (no clip latch). Blur 2px→24px with presence.
 * `mix-blend-mode: screen` keeps the tint additive.
 */
export const BlockEnergyBorder: React.FC<BlockEnergyBorderProps> = React.memo(
  function BlockEnergyBorder({ meterId, borderRadius = 12 }) {
    const { color, presence } = useBlockEnergy(meterId);

    const blur = INSET_BLUR_MIN_PX + presence * (INSET_BLUR_MAX_PX - INSET_BLUR_MIN_PX);
    const shadow =
      presence > 0
        ? `inset 0 0 ${blur.toFixed(1)}px 0 rgba(${color[0]}, ${color[1]}, ${color[2]}, ${(INSET_OPACITY * presence).toFixed(2)})`
        : 'none';

    return (
      <div
        style={{
          position: 'absolute',
          inset: 0,
          borderRadius: `${borderRadius}px`,
          boxShadow: shadow,
          mixBlendMode: 'screen',
          pointerEvents: 'none',
          transition: 'box-shadow 60ms linear',
          zIndex: 3,
        }}
      />
    );
  }
);
