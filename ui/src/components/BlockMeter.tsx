import React, { useMemo } from 'react';
import { useMeter } from '../hooks/useMeters';
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
 * the color scale (dimmed) and lights up to the current level.
 */
export const BlockMeter: React.FC<BlockMeterProps> = ({ meterId, height = 140 }) => {
  const db = useMeter(meterId);

  const numDots = useMemo(
    () => Math.max(1, Math.floor((height + DOT_GAP) / (DOT_SIZE + DOT_GAP))),
    [height]
  );

  const normalized = (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
  const activeIndex = Math.floor(normalized * numDots);

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
        const position = numDots > 1 ? index / (numDots - 1) : 0;
        const isActive = index <= activeIndex;
        return (
          <div
            key={index}
            style={{
              width: `${DOT_SIZE}px`,
              height: `${DOT_SIZE}px`,
              borderRadius: '50%',
              backgroundColor: getGradientColor(position),
              opacity: isActive ? 1 : 0.22,
              flexShrink: 0,
            }}
          />
        );
      })}
    </div>
  );
};
