import React from 'react';
import { Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { jitterMsScale, spreadMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { GRAY, HIGHLIGHT } from './theme';

/**
 * Spread: a short per-note delay on one channel for stereo width. One
 * parameter set serves both modes (they're mutually exclusive):
 * - Mono: doubles the chain and delays the chosen side (slap double).
 * - Stereo: delays the chosen side's chain in place.
 * The group lives on the faceplate in both modes.
 *
 * The spread knob is bipolar — center = 0 ms = processing skipped; left of
 * center delays the left channel, right of center the right.
 */

const KNOB_SIZE = 30;
/** Vertically center inline elements on the knob column (knob + label). */
export const KNOB_CENTER_OFFSET = -11;

/** Small square icon toggle that sits inline with knobs in a control row. */
export const PillIconButton: React.FC<{
  on: boolean;
  title: string;
  onClick: () => void;
  /** Vertical nudge; defaults to centering on an adjacent knob. Pass 0 when
      the button is positioned by its own layout. */
  offsetY?: number;
  children: React.ReactNode;
}> = ({ on, title, onClick, offsetY = KNOB_CENTER_OFFSET, children }) => (
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
      color: on ? '#ffffff' : GRAY,
      backgroundColor: on ? 'transparent' : HIGHLIGHT,
      transform: `translateY(${offsetY}px)`,
    }}
  >
    {children}
  </button>
);

/** Spread + jitter knobs with power switch. */
export const SpreadGroup: React.FC<{
  /** Knob center fill — match the surface the group sits on. */
  innerColor?: string;
}> = ({ innerColor = '#000000' }) => {
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
        innerColor={innerColor}
        scale={spreadMsScale}
        defaultValue={0.5}
      />
      <KnobControl
        label="Jitter"
        value={jitter}
        onChange={setJitter}
        size={KNOB_SIZE}
        labelSize={10}
        innerColor={innerColor}
        scale={jitterMsScale}
        defaultValue={0}
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
