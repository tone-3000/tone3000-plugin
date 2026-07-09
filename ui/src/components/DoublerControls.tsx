import React from 'react';
import { Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';

/**
 * Mono-mode stereo doubler, pinned above the chain (the same slot the
 * LEFT/RIGHT selector uses in stereo mode). Label + spread/jitter knobs +
 * power switch, always visible — matches the faceplate gate/EQ pattern.
 */

const BORDER = '1px solid rgba(84, 84, 88, 0.65)';
const MUTED = 'rgba(235, 235, 245, 0.60)';
const KNOB_SIZE = 30;
/** Vertically center the power btn on the knob column (knob + label). */
const KNOB_CENTER_OFFSET = -11;

const PowerButton: React.FC<{ on: boolean; title: string; onClick: () => void }> = ({
  on,
  title,
  onClick,
}) => (
  <button
    onClick={onClick}
    title={title}
    style={{
      width: '22px',
      height: '22px',
      borderRadius: '6px',
      border: 'none',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      cursor: 'pointer',
      padding: 0,
      flexShrink: 0,
      color: on ? '#ffffff' : '#8D8D93',
      backgroundColor: on ? 'transparent' : 'rgba(235, 235, 245, 0.18)',
      transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
    }}
  >
    <Power size={12} />
  </button>
);

export const DoublerControls: React.FC = () => {
  const [enabled, setEnabled] = useParameter('doublerEnabled', 'toggle');
  const [spread, setSpread] = useParameter('doublerSpread', 'slider');
  const [jitter, setJitter] = useParameter('doublerJitter', 'slider');

  return (
    <div
      style={{
        width: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: '12px',
      }}
    >
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '16px',
          padding: '8px 16px',
          borderRadius: '8px',
          border: BORDER,
          opacity: enabled ? 1 : 0.55,
        }}
      >
        <span
          style={{
            fontSize: '12px',
            letterSpacing: '0.04em',
            color: enabled ? '#ffffff' : MUTED,
            flexShrink: 0,
            transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
          }}
        >
          DOUBLER
        </span>
        <KnobControl
          label="Spread"
          value={spread}
          onChange={setSpread}
          size={KNOB_SIZE}
          labelSize={10}
        />
        <KnobControl
          label="Jitter"
          value={jitter}
          onChange={setJitter}
          size={KNOB_SIZE}
          labelSize={10}
        />
        <PowerButton
          on={enabled}
          title={enabled ? 'Turn doubler off' : 'Turn doubler on'}
          onClick={() => setEnabled(!enabled)}
        />
      </div>
    </div>
  );
};
