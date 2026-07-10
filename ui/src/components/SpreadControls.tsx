import React from 'react';
import { Power } from 'lucide-react';
import { ChainContentRow } from './chainLayout';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';

/**
 * Spread: a short per-note delay on one channel for stereo width. One
 * parameter set serves both modes (they're mutually exclusive):
 * - Mono: doubles the chain and delays the chosen side (slap double). The
 *   group renders in the row above the chain (SpreadControls).
 * - Stereo: delays the chosen side's chain in place; the same group renders
 *   in the stereo controls row (see StereoControls).
 *
 * The spread knob is bipolar — center = 0 ms = processing skipped; left of
 * center delays the left channel, right of center the right.
 */

export const PILL_BORDER = '1px solid rgba(84, 84, 88, 0.65)';
const KNOB_SIZE = 30;
/** Vertically center inline elements on the knob column (knob + label). */
export const KNOB_CENTER_OFFSET = -11;

/** Small square icon toggle that sits inline with knobs in a control row. */
export const PillIconButton: React.FC<{
  on: boolean;
  title: string;
  onClick: () => void;
  children: React.ReactNode;
}> = ({ on, title, onClick, children }) => (
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
    {children}
  </button>
);

/** Spread + jitter knobs with power switch — shared by both mode rows. */
export const SpreadGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('spreadEnabled', 'toggle');
  const [amount, setAmount] = useParameter('spreadAmount', 'slider');
  const [jitter, setJitter] = useParameter('spreadJitter', 'slider');

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        gap: '16px',
        opacity: enabled ? 1 : 0.55,
      }}
    >
      <KnobControl
        label="Spread"
        value={amount}
        onChange={setAmount}
        variant="bipolar"
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
      <PillIconButton
        on={enabled}
        title={enabled ? 'Turn spread off' : 'Turn spread on'}
        onClick={() => setEnabled(!enabled)}
      >
        <Power size={12} />
      </PillIconButton>
    </div>
  );
};

/** Mono-mode row: spread group pinned to the right, aligned with the cards. */
export const SpreadControls: React.FC = () => (
  <ChainContentRow
    style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'flex-end',
      padding: '12px 0',
    }}
  >
    <SpreadGroup />
  </ChainContentRow>
);
