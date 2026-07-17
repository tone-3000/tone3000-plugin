import React from 'react';
import { Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { jitterMsScale, spreadMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { HELP, helpProps } from './helpText';
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

/** Matches the main faceplate knobs (Input/Gate/tone stack/Output). */
const KNOB_SIZE = 36;
/** Companion trim next to Spread — same size language as Bal next to Output. */
const JITTER_KNOB_SIZE = 24;
/** Vertically center inline elements on the knob column (knob + label). */
export const KNOB_CENTER_OFFSET = -11;

/** Small square icon toggle that sits inline with knobs in a control row. */
export const PillIconButton: React.FC<{
  on: boolean;
  /** One-line hint for the faceplate help readout (see helpText.ts). */
  help: string;
  onClick: () => void;
  /** Vertical nudge; defaults to centering on an adjacent knob. Pass 0 when
      the button is positioned by its own layout. */
  offsetY?: number;
  children: React.ReactNode;
}> = ({ on, help, onClick, offsetY = KNOB_CENTER_OFFSET, children }) => (
  <button
    onClick={onClick}
    {...helpProps(help)}
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

/** Spread + jitter knobs with power switch. Jitter is a small companion
 *  knob grouped with Spread the same way Bal sits next to Output. */
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
        gap: '10px',
        opacity: enabled ? 1 : 0.55,
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
        <KnobControl
          label="Spread"
          value={amount}
          onChange={setAmount}
          variant="bipolar"
          size={KNOB_SIZE}
          labelSize={12}
          innerColor={innerColor}
          scale={spreadMsScale}
          defaultValue={0.5}
          help={HELP.spread}
        />
        <KnobControl
          label="Jit"
          value={jitter}
          onChange={setJitter}
          size={JITTER_KNOB_SIZE}
          labelSize={10}
          innerColor={innerColor}
          scale={jitterMsScale}
          defaultValue={0}
          help={HELP.jitter}
        />
      </div>
      <PillIconButton on={enabled} help={HELP.spreadPower} onClick={() => setEnabled(!enabled)}>
        <Power size={12} />
      </PillIconButton>
    </div>
  );
};
