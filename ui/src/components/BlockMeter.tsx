import React, { useMemo } from 'react';
import { useMeter, useMeterClip } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB, getGradientColor } from './meterColor';

interface BlockMeterProps {
  /** Meter id from useMeters (e.g. meterId.blockIn(blockId)). */
  meterId: string;
  height?: number;
}

const DOT_SIZE = 4;
const DOT_GAP = 5;

/**
 * Minimal per-block level meter: a vertical column of dots that always shows
 * the color scale (dimmed) and lights up to the current level. Dots span
 * METER_MIN_DB..0 dBFS; the top dot is a latching clip LED (click to clear).
 */
export const BlockMeter: React.FC<BlockMeterProps> = ({ meterId, height = 140 }) => {
  const db = useMeter(meterId);
  const [clipped, clearClip] = useMeterClip(meterId);

  const numDots = useMemo(
    () => Math.max(2, Math.floor((height + DOT_GAP) / (DOT_SIZE + DOT_GAP))),
    [height]
  );

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column-reverse',
        justifyContent: 'flex-start',
        gap: `${DOT_GAP}px`,
        flexShrink: 0,
        alignSelf: 'center',
      }}
    >
      {Array.from({ length: numDots }, (_, index) => {
        // Dot i sits at an exact dB threshold; the top dot is exactly 0 dBFS
        // and doubles as the latching clip LED.
        const position = index / (numDots - 1);
        const dotDb = METER_MIN_DB + position * (METER_MAX_DB - METER_MIN_DB);
        const isClipDot = index === numDots - 1;
        const isActive = isClipDot ? clipped : db >= dotDb;
        return (
          <div
            key={index}
            onClick={isClipDot && clipped ? clearClip : undefined}
            title={isClipDot && clipped ? 'Clipped — click to clear' : undefined}
            style={{
              width: `${DOT_SIZE}px`,
              height: `${DOT_SIZE}px`,
              borderRadius: '50%',
              backgroundColor: getGradientColor(position),
              opacity: isActive ? 1 : 0.22,
              cursor: isClipDot && clipped ? 'pointer' : undefined,
              flexShrink: 0,
            }}
          />
        );
      })}
    </div>
  );
};
