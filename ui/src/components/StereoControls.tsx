import React from 'react';
import { Circle, Link } from 'lucide-react';
import type { ChainSide } from '../types/chain';
import { ChainContentRow } from './chainLayout';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';
import { DoublerGroup, PillIconButton, PILL_BORDER } from './DoublerControls';

/**
 * Stereo (dual-chain) mode controls:
 * - StereoModeToggle: mono/stereo pill switch that lives in the top bar.
 * - StereoChainControls: the row pinned above the chain while stereo mode is
 *   on — chain pans (linked by default), LEFT/RIGHT chain picker, and the
 *   OFFSET group (delay-based width, same engine as the mono doubler).
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
    strokeWidth="1.5"
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
    color: active ? '#ffffff' : '#8D8D93',
    backgroundColor: active ? 'rgba(235, 235, 245, 0.24)' : 'transparent',
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
        backgroundColor: '#1C1C1E',
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

const segmentBase: React.CSSProperties = {
  padding: '6px 18px',
  fontSize: '12px',
  border: 'none',
  cursor: 'pointer',
  color: '#ffffff',
  background: 'transparent',
  letterSpacing: '0.04em',
};

const ChainSideSelector: React.FC<{
  activeSide: ChainSide;
  onSelectSide: (side: ChainSide) => void;
}> = ({ activeSide, onSelectSide }) => (
  <div
    style={{
      display: 'flex',
      flexDirection: 'row',
      borderRadius: '8px',
      border: PILL_BORDER,
      overflow: 'hidden',
      flexShrink: 0,
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
        borderLeft: PILL_BORDER,
        backgroundColor: activeSide === 'right' ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
      }}
    >
      RIGHT
    </button>
  </div>
);

/**
 * Chain pan pair. Constant-power positions (0 = hard left, 1 = hard right);
 * defaults keep the classic hard-panned dual-chain image. Linked (default)
 * mirrors the knobs around center so width changes stay symmetric; unlink for
 * uneven images. Mirroring is a UI gesture — the DSP just reads two pans.
 */
const ChainPanControls: React.FC = () => {
  const [panLeft, setPanLeft] = useParameter('chainPanLeft', 'slider');
  const [panRight, setPanRight] = useParameter('chainPanRight', 'slider');
  const [linked, setLinked] = useParameter('chainPanLinked', 'toggle');

  const handlePanLeft = (value: number) => {
    setPanLeft(value);
    if (linked) setPanRight(1 - value);
  };
  const handlePanRight = (value: number) => {
    setPanRight(value);
    if (linked) setPanLeft(1 - value);
  };
  const handleToggleLink = () => {
    const next = !linked;
    setLinked(next);
    // Re-linking snaps back to a symmetric image, anchored on the left pan.
    if (next) setPanRight(1 - panLeft);
  };

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        gap: '12px',
      }}
    >
      <KnobControl label="Pan L" value={panLeft} onChange={handlePanLeft} size={30} labelSize={10} />
      <PillIconButton
        on={linked}
        title={linked ? 'Unlink pans (uneven image)' : 'Link pans (mirrored)'}
        onClick={handleToggleLink}
      >
        <Link size={12} />
      </PillIconButton>
      <KnobControl
        label="Pan R"
        value={panRight}
        onChange={handlePanRight}
        size={30}
        labelSize={10}
      />
    </div>
  );
};

export const StereoChainControls: React.FC<{
  activeSide: ChainSide;
  onSelectSide: (side: ChainSide) => void;
}> = ({ activeSide, onSelectSide }) => (
  <ChainContentRow
    style={{
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      gap: '20px',
      padding: '12px 0',
    }}
  >
    <div style={{ flex: 1, display: 'flex', justifyContent: 'flex-start' }}>
      <ChainPanControls />
    </div>
    <ChainSideSelector activeSide={activeSide} onSelectSide={onSelectSide} />
    <div style={{ flex: 1, display: 'flex', justifyContent: 'flex-end' }}>
      <DoublerGroup
        label="OFFSET"
        enabledParam="stereoOffsetEnabled"
        spreadParam="stereoOffsetSpread"
        jitterParam="stereoOffsetJitter"
        spreadLabel="Offset"
        jitterLabel="Jitter"
      />
    </div>
  </ChainContentRow>
);
