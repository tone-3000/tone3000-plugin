import React, { useMemo } from 'react';
import { useMeter, useMeterClip } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB, getGradientColor } from './meterColor';
import { HELP, helpProps } from './helpText';

interface BlockMeterProps {
  /** Meter id from useMeters (e.g. meterId.blockIn(blockId)). */
  meterId: string;
  /** Extent along the meter axis, px (height when vertical, width when horizontal). */
  length?: number;
  /** Vertical rails in the detail card; horizontal strip on gallery tiles. */
  orientation?: 'vertical' | 'horizontal';
}

const DOT_SIZE = 4;
/** Matches the main meters' (DbMeter) gap so the rails read as one family. */
const DOT_GAP = 10;

/**
 * Minimal per-block level meter: a run of dots that always shows the color
 * scale (dimmed) and lights up to the current level. Dots span
 * METER_MIN_DB..0 dBFS; the last dot (top / right) is a latching clip LED
 * (click to clear).
 *
 * Memoized: level updates arrive through the meter store subscription, so a
 * parent re-render with identical props never needs to re-run this.
 */
export const BlockMeter: React.FC<BlockMeterProps> = React.memo(function BlockMeter({
  meterId,
  length = 140,
  orientation = 'vertical',
}) {
  const db = useMeter(meterId);
  const [clipped, clearClip] = useMeterClip(meterId);

  const numDots = useMemo(
    () => Math.max(2, Math.floor((length + DOT_GAP) / (DOT_SIZE + DOT_GAP))),
    [length]
  );

  return (
    <div
      style={{
        display: 'flex',
        // Level rises bottom→top when vertical, left→right when horizontal.
        flexDirection: orientation === 'vertical' ? 'column-reverse' : 'row',
        justifyContent: 'flex-start',
        gap: `${DOT_GAP}px`,
        flexShrink: 0,
        alignSelf: 'center',
      }}
    >
      {Array.from({ length: numDots }, (_, index) => {
        // Dot i sits at an exact dB threshold; the last dot is exactly 0 dBFS
        // and doubles as the latching clip LED.
        const position = index / (numDots - 1);
        const dotDb = METER_MIN_DB + position * (METER_MAX_DB - METER_MIN_DB);
        const isClipDot = index === numDots - 1;
        const isActive = isClipDot ? clipped : db >= dotDb;
        return (
          <div
            key={index}
            onClick={isClipDot && clipped ? clearClip : undefined}
            {...(isClipDot && clipped ? helpProps(HELP.clipDot) : {})}
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
});
