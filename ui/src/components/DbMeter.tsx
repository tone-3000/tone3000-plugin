import React, { useMemo } from 'react';
import { useMeter } from '../hooks/useMeters';
import { METER_MAX_DB, METER_MIN_DB, getGradientColor } from './meterColor';

interface DbMeterProps {
  type: 'input' | 'output';
  height?: number;
  labelsPosition?: 'left' | 'right';
}

const BLOCK_SIZE = 6;
const BLOCK_GAP = 10;
const INACTIVE_COLOR = '#8D8D93';
const LABEL_COLOR = '#8D8D93';

export const DbMeter: React.FC<DbMeterProps> = ({
  type,
  height = 200,
  labelsPosition = 'left',
}) => {
  // Fed by the shared meter store: one native call per frame for the whole UI.
  const dbLevel = useMeter(type);

  // Calculate number of blocks that fit in the height
  const numBlocks = useMemo(() => {
    return Math.floor(height / (BLOCK_SIZE + BLOCK_GAP));
  }, [height]);

  // Calculate actual height occupied by blocks (for label alignment)
  const actualMeterHeight = useMemo(() => {
    return numBlocks * BLOCK_SIZE + (numBlocks - 1) * BLOCK_GAP;
  }, [numBlocks]);

  const minDb = METER_MIN_DB;
  const maxDb = METER_MAX_DB;

  // Convert dB to block index (0 = bottom, numBlocks-1 = top)
  const dbToBlockIndex = (db: number): number => {
    const normalized = (db - minDb) / (maxDb - minDb);
    return Math.floor(normalized * numBlocks);
  };

  // Convert dB to pixel position from bottom
  const dbToPixelPosition = (db: number): number => {
    const normalized = (db - minDb) / (maxDb - minDb);
    return normalized * actualMeterHeight;
  };

  const activeBlockIndex = dbToBlockIndex(dbLevel);

  // All scale marks
  const scaleMarks = [-60, -48, -36, -24, -18, -12, -6, -3, 0, +3, +6, +12];

  // Generate blocks from bottom to top
  const blocks = useMemo(() => {
    return Array.from({ length: numBlocks }, (_, i) => i);
  }, [numBlocks]);

  const renderLabels = (side: 'left' | 'right') => (
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
      {scaleMarks.map((db) => {
        const pixelPos = dbToPixelPosition(db);
        return (
          <div
            key={db}
            style={{
              position: 'absolute',
              bottom: `${pixelPos}px`,
              [side === 'left' ? 'right' : 'right']: 0,
              transform: 'translateY(50%)',
              textAlign: side === 'left' ? 'right' : 'right',
              width: '24px',
              lineHeight: 1,
              fontFamily: 'monospace',
            }}
          >
            {db > 0 ? `+${db}` : db}
          </div>
        );
      })}
    </div>
  );

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'flex-end',
        gap: '12px',
      }}
    >
      {/* Labels on left side */}
      {labelsPosition === 'left' && renderLabels('left')}

      {/* Meter blocks */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'column-reverse',
          gap: `${BLOCK_GAP}px`,
        }}
      >
        {blocks.map((index) => {
          const isActive = index <= activeBlockIndex;
          const position = numBlocks > 1 ? index / (numBlocks - 1) : 0;
          const color = isActive ? getGradientColor(position) : INACTIVE_COLOR;

          return (
            <div
              key={index}
              style={{
                width: `${BLOCK_SIZE}px`,
                height: `${BLOCK_SIZE}px`,
                backgroundColor: color,
                // borderRadius: '2px',
                flexShrink: 0,
              }}
            />
          );
        })}
      </div>

      {/* Labels on right side */}
      {labelsPosition === 'right' && renderLabels('right')}
    </div>
  );
};
