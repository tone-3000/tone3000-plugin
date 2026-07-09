import React, { useMemo } from 'react';
import { useMeter, meterId } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB, getGradientColor } from './meterColor';

interface DbMeterProps {
  type: 'input' | 'output';
  /** Dual L/R columns sharing one dB scale (stereo mode / stereo input). */
  stereo?: boolean;
  height?: number;
  labelsPosition?: 'left' | 'right';
}

const DOT_SIZE = 6;
const DOT_GAP = 10;
const LABEL_COLOR = '#8D8D93';

/**
 * Main input/output meter: vertical dot column(s) in the block-meter style —
 * the full color scale is always visible (dimmed) and lights to the level.
 * Each column subscribes to its own meter id, so a channel only re-renders
 * for its own (quantized) changes.
 */
const DotColumn: React.FC<{ id: string; numDots: number }> = ({ id, numDots }) => {
  const db = useMeter(id);
  const normalized = (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
  const activeIndex = Math.floor(normalized * numDots);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column-reverse',
        gap: `${DOT_GAP}px`,
        flexShrink: 0,
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

export const DbMeter: React.FC<DbMeterProps> = ({
  type,
  stereo = false,
  height = 200,
  labelsPosition = 'left',
}) => {
  const numDots = useMemo(() => Math.floor(height / (DOT_SIZE + DOT_GAP)), [height]);
  const actualMeterHeight = numDots * DOT_SIZE + (numDots - 1) * DOT_GAP;

  // Convert dB to pixel position from bottom
  const dbToPixelPosition = (db: number): number => {
    const normalized = (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
    return normalized * actualMeterHeight;
  };

  const scaleMarks = [-60, -48, -36, -24, -18, -12, -6, -3, 0, +3, +6, +12];

  const labels = (
    <div
      style={{
        position: 'relative',
        height: `${actualMeterHeight}px`,
        fontSize: '10px',
        fontWeight: '500',
        color: LABEL_COLOR,
        flexShrink: 0,
        width: '20px',
      }}
    >
      {scaleMarks.map((db) => (
        <div
          key={db}
          style={{
            position: 'absolute',
            bottom: `${dbToPixelPosition(db)}px`,
            right: 0,
            transform: 'translateY(50%)',
            textAlign: 'right',
            width: '24px',
            lineHeight: 1,
            fontFamily: 'monospace',
          }}
        >
          {db > 0 ? `+${db}` : db}
        </div>
      ))}
    </div>
  );

  // One column subscribed to the combined level, or L/R columns per channel.
  const columns = stereo
    ? [meterId.main(type, 'l'), meterId.main(type, 'r')]
    : [type];

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'flex-end',
        gap: '12px',
      }}
    >
      {labelsPosition === 'left' && labels}
      {columns.map((id) => (
        <DotColumn key={id} id={id} numDots={numDots} />
      ))}
      {labelsPosition === 'right' && labels}
    </div>
  );
};
