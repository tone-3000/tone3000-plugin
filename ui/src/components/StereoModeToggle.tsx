import React from 'react';
import { Circle } from 'lucide-react';
import { HELP, helpProps } from './helpText';
import { GRAY, HIGHLIGHT, SURFACE_RAISED } from './theme';

/**
 * Mono/stereo pill switch that lives in the top bar. Both chains render at
 * once in stereo mode (see ChainView), so this is the only stereo-mode
 * control outside the chain gallery.
 */

// Two overlapping 12px circles (no lucide equivalent), drawn to match
// lucide's stroke style so it sits next to Circle seamlessly.
const StereoCirclesIcon: React.FC<{ size?: number }> = ({ size = 12 }) => {
  // Two size×size circles overlapping by half a radius.
  const r = size / 2;
  const overlap = r * 0.9;
  const width = size + overlap;
  return (
    <svg
      width={width}
      height={size}
      viewBox={`0 0 ${width} ${size}`}
      fill="none"
      stroke="currentColor"
      strokeWidth={1.5}
      strokeLinecap="round"
    >
      <circle cx={r} cy={r} r={r - 1} />
      <circle cx={overlap + r} cy={r} r={r - 1} />
    </svg>
  );
};

/** Overall selector width. The 36px highlight snaps to the selected side,
    inset 4px from the outer edge (matching its 4px top/bottom inset). Each
    button is highlight-width so its icon centers under the highlight; a 4px
    container gutter keeps the whole thing 80px wide (4 + 36 + 36 + 4). */
const SELECTOR_WIDTH = 80;
const OUTER_INSET = 4;
const HIGHLIGHT_WIDTH = 36;
const SEGMENT_WIDTH = HIGHLIGHT_WIDTH; // 36: button matches highlight so the icon aligns
const HIGHLIGHT_TRAVEL = HIGHLIGHT_WIDTH; // 36

export const StereoModeToggle: React.FC<{
  stereoEnabled: boolean;
  onToggle: (enabled: boolean) => void;
}> = ({ stereoEnabled, onToggle }) => {
  const segmentStyle = (active: boolean): React.CSSProperties => ({
    position: 'relative',
    zIndex: 1,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    width: `${SEGMENT_WIDTH}px`,
    height: '36px',
    border: 'none',
    background: 'transparent',
    cursor: 'pointer',
    padding: 0,
    color: active ? '#ffffff' : GRAY,
  });

  return (
    <div
      style={{
        position: 'relative',
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        width: `${SELECTOR_WIDTH}px`,
        height: '36px',
        padding: `0 ${OUTER_INSET}px`,
        boxSizing: 'border-box',
        borderRadius: '18px',
        backgroundColor: SURFACE_RAISED,
        flexShrink: 0,
      }}
    >
      {/* Selection highlight, snapping to the selected side (no animation). */}
      <span
        aria-hidden
        style={{
          position: 'absolute',
          top: '4px',
          left: `${OUTER_INSET}px`,
          width: `${HIGHLIGHT_WIDTH}px`,
          height: '28px',
          borderRadius: '14px',
          backgroundColor: HIGHLIGHT,
          transform: stereoEnabled ? `translateX(${HIGHLIGHT_TRAVEL}px)` : 'translateX(0)',
        }}
      />
      <button
        onClick={() => onToggle(false)}
        {...helpProps(HELP.monoMode)}
        style={segmentStyle(!stereoEnabled)}
      >
        <Circle size={12} strokeWidth={3} />
      </button>
      <button
        onClick={() => onToggle(true)}
        {...helpProps(HELP.stereoMode)}
        style={segmentStyle(stereoEnabled)}
      >
        <StereoCirclesIcon size={12} />
      </button>
    </div>
  );
};
