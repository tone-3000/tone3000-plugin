import React from 'react';
import { useMeter, useMeterClip } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB } from './meterColor';
import { HELP, helpProps } from './helpText';

interface BlockLedProps {
  /** Meter id from useMeters (e.g. meterId.blockOut(blockId)). */
  meterId: string;
  /** Outer ring diameter, px. */
  size?: number;
}

type Rgb = [number, number, number];

// Pure primaries matching the meter ramp (meterColor.ts) so the lens reads
// as the same family as the dot-strip meters.
const BLUE: Rgb = [0, 0, 255];
const YELLOW: Rgb = [255, 255, 0];
const RED: Rgb = [255, 0, 0];

/**
 * Brightness fade at the bottom of the scale. A hard silence cutoff near the
 * -60 dB meter floor doesn't work here: NAM captures emit their own noise
 * floor (hiss) even with no input, so the output meter idles around
 * -50…-40 dB and the lens would never turn off. Instead the lens brightness
 * ramps from fully dark at OFF_DB to fully lit at FULL_DB, so residual amp
 * noise reads as off/barely-glowing and real signal lights it up.
 */
const OFF_DB = -50;
const FULL_DB = -32;
const DARK_LENS: Rgb = [34, 34, 34];

const lerpRgb = (c0: Rgb, c1: Rgb, t: number): Rgb => [
  Math.round(c0[0] + (c1[0] - c0[0]) * t),
  Math.round(c0[1] + (c1[1] - c0[1]) * t),
  Math.round(c0[2] + (c1[2] - c0[2]) * t),
];

/**
 * Level → lens color: blue and yellow plateaus with short crossfades, red at
 * the top. Plateaus (rather than a continuous ramp) keep the single dot
 * readable at a glance: blue = healthy, yellow = hot, red = at 0 dBFS.
 */
const dbToUnit = (db: number): number =>
  Math.min(1, Math.max(0, (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB)));

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
 * Single-LED level indicator for gallery tiles: a lens in a dark ring. Dark
 * when silent; with signal present the lens tracks the level from blue
 * through yellow to red at 0 dBFS. Clipping latches the lens solid red
 * (click to clear). The detail card keeps the full dot-strip BlockMeter.
 *
 * Memoized: level updates arrive through the meter store subscription, so a
 * parent re-render with identical props never needs to re-run this.
 */
export const BlockLed: React.FC<BlockLedProps> = React.memo(function BlockLed({
  meterId,
  size = 20,
}) {
  const db = useMeter(meterId);
  const [clipped, clearClip] = useMeterClip(meterId);

  const unit = clipped ? 1 : dbToUnit(db);
  // 0 = off (dark lens), 1 = fully lit; see OFF_DB/FULL_DB above.
  const presence = clipped ? 1 : Math.min(1, Math.max(0, (db - OFF_DB) / (FULL_DB - OFF_DB)));
  const lens = lerpRgb(DARK_LENS, clipped ? RED : levelColor(unit), presence);

  // Soft halo around the ring in the lens color; blur and strength both
  // grow with signal energy so louder blocks visibly radiate. Gated by
  // presence so an idle (noise-floor) lens doesn't glow.
  const glow =
    presence > 0
      ? `0 0 ${(2 + unit * 6).toFixed(1)}px ${(unit * 1.5).toFixed(1)}px rgba(${lens[0]}, ${lens[1]}, ${lens[2]}, ${(presence * (0.25 + unit * 0.45)).toFixed(2)})`
      : 'none';

  return (
    <div
      onClick={clipped ? clearClip : undefined}
      {...(clipped ? helpProps(HELP.clipDot) : {})}
      style={{
        width: `${size}px`,
        height: `${size}px`,
        borderRadius: '50%',
        backgroundColor: '#0c0c0c',
        boxShadow: glow,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        cursor: clipped ? 'pointer' : undefined,
        flexShrink: 0,
        transition: 'box-shadow 60ms linear',
      }}
    >
      <div
        style={{
          width: `${Math.round(size * 0.65)}px`,
          height: `${Math.round(size * 0.65)}px`,
          borderRadius: '50%',
          backgroundColor: `rgb(${lens[0]}, ${lens[1]}, ${lens[2]})`,
          transition: 'background-color 60ms linear',
        }}
      />
    </div>
  );
});
