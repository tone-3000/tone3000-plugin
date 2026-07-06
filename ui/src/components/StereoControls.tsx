import React from 'react';
import { Headphones } from 'lucide-react';
import type { ChainSide } from '../types/tone';

interface StereoControlsProps {
  stereoEnabled: boolean;
  activeSide: ChainSide;
  onToggleStereo: (enabled: boolean) => void;
  onSelectSide: (side: ChainSide) => void;
}

const segmentBase: React.CSSProperties = {
  padding: '6px 18px',
  fontSize: '12px',
  fontWeight: 700,
  border: 'none',
  cursor: 'pointer',
  color: '#ffffff',
  background: 'transparent',
  letterSpacing: '0.04em',
};

/**
 * Control strip pinned above the chain. Enables stereo (dual-chain) mode and, when on,
 * toggles which chain (Left / Right) is being edited below.
 */
export const StereoControls: React.FC<StereoControlsProps> = ({
  stereoEnabled,
  activeSide,
  onToggleStereo,
  onSelectSide,
}) => {
  return (
    <div
      style={{
        width: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        gap: '16px',
        padding: '12px',
      }}
    >
      <button
        type="button"
        onClick={() => onToggleStereo(!stereoEnabled)}
        title={stereoEnabled ? 'Disable stereo mode' : 'Enable stereo mode'}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          padding: '6px 14px',
          fontSize: '12px',
          fontWeight: 700,
          letterSpacing: '0.04em',
          borderRadius: '8px',
          border: '1px solid rgba(84, 84, 88, 0.65)',
          cursor: 'pointer',
          color: '#ffffff',
          backgroundColor: stereoEnabled ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
        }}
      >
        <Headphones size={16} />
        STEREO {stereoEnabled ? 'ON' : 'OFF'}
      </button>

      {stereoEnabled && (
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            borderRadius: '8px',
            border: '1px solid rgba(84, 84, 88, 0.65)',
            overflow: 'hidden',
          }}
        >
          <button
            type="button"
            onClick={() => onSelectSide('left')}
            style={{
              ...segmentBase,
              backgroundColor: activeSide === 'left' ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
            }}
          >
            LEFT
          </button>
          <button
            type="button"
            onClick={() => onSelectSide('right')}
            style={{
              ...segmentBase,
              borderLeft: '1px solid rgba(84, 84, 88, 0.65)',
              backgroundColor: activeSide === 'right' ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
            }}
          >
            RIGHT
          </button>
        </div>
      )}
    </div>
  );
};
