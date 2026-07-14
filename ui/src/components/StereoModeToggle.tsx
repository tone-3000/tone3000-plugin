import React from 'react';
import { Circle } from 'lucide-react';
import { GRAY, HIGHLIGHT, SURFACE_RAISED } from './theme';

/**
 * Mono/stereo pill switch that lives in the top bar. Both chains render at
 * once in stereo mode (see ChainView), so this is the only stereo-mode
 * control outside the chain gallery.
 */

// Two overlapping circles (no lucide equivalent) — drawn to match lucide's
// stroke style so it sits next to Circle seamlessly.
const StereoCirclesIcon: React.FC<{ size?: number }> = ({ size = 14 }) => (
  <svg
    width={(size * 18) / 12}
    height={size}
    viewBox="0 0 18 12"
    fill="none"
    stroke="currentColor"
    strokeWidth="1.75"
    strokeLinecap="round"
  >
    <circle cx="6" cy="6" r="5" />
    <circle cx="12" cy="6" r="5" />
  </svg>
);

export const StereoModeToggle: React.FC<{
  stereoEnabled: boolean;
  onToggle: (enabled: boolean) => void;
}> = ({ stereoEnabled, onToggle }) => {
  const segmentStyle = (active: boolean): React.CSSProperties => ({
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    width: '32px',
    height: '24px',
    border: 'none',
    borderRadius: '12px',
    cursor: 'pointer',
    padding: 0,
    color: active ? '#ffffff' : GRAY,
    backgroundColor: active ? HIGHLIGHT : 'transparent',
  });

  return (
    <div
      title={stereoEnabled ? 'Stereo mode (dual chains)' : 'Mono mode (single chain)'}
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        gap: '2px',
        padding: '2px',
        borderRadius: '14px',
        backgroundColor: SURFACE_RAISED,
        flexShrink: 0,
      }}
    >
      <button
        onClick={() => onToggle(false)}
        title="Mono (single chain)"
        style={segmentStyle(!stereoEnabled)}
      >
        <Circle size={12} strokeWidth={1.75} />
      </button>
      <button
        onClick={() => onToggle(true)}
        title="Stereo (independent L/R chains)"
        style={segmentStyle(stereoEnabled)}
      >
        <StereoCirclesIcon size={12} />
      </button>
    </div>
  );
};
