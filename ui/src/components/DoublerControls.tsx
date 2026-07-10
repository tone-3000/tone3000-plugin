import React from 'react';
import { Power } from 'lucide-react';
import { ChainContentRow } from './chainLayout';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';

/**
 * Delay-based width controls, shared between the two mode-exclusive features:
 * - DoublerControls: mono-mode doubler, pinned above the chain (the same slot
 *   the stereo controls row uses in stereo mode).
 * - DoublerGroup: the reusable pill (label + two knobs + power switch), also
 *   used by the stereo OFFSET group in StereoControls.
 */

export const PILL_BORDER = '1px solid rgba(84, 84, 88, 0.65)';
const KNOB_SIZE = 30;
/** Vertically center inline elements on the knob column (knob + label). */
export const KNOB_CENTER_OFFSET = -11;

/** Small square icon toggle that sits inline with knobs in a control pill. */
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

interface DoublerGroupProps {
  /** Feature name, used only in the power button tooltip. */
  label: string;
  enabledParam: string;
  spreadParam: string;
  jitterParam: string;
  spreadLabel: string;
  jitterLabel: string;
}

export const DoublerGroup: React.FC<DoublerGroupProps> = ({
  label,
  enabledParam,
  spreadParam,
  jitterParam,
  spreadLabel,
  jitterLabel,
}) => {
  const [enabled, setEnabled] = useParameter(enabledParam, 'toggle');
  const [spread, setSpread] = useParameter(spreadParam, 'slider');
  const [jitter, setJitter] = useParameter(jitterParam, 'slider');

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
        label={spreadLabel}
        value={spread}
        onChange={setSpread}
        size={KNOB_SIZE}
        labelSize={10}
      />
      <KnobControl
        label={jitterLabel}
        value={jitter}
        onChange={setJitter}
        size={KNOB_SIZE}
        labelSize={10}
      />
      <PillIconButton
        on={enabled}
        title={enabled ? `Turn ${label.toLowerCase()} off` : `Turn ${label.toLowerCase()} on`}
        onClick={() => setEnabled(!enabled)}
      >
        <Power size={12} />
      </PillIconButton>
    </div>
  );
};

export const DoublerControls: React.FC = () => (
  <ChainContentRow
    style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'flex-end',
      padding: '12px 0',
    }}
  >
    <DoublerGroup
      label="DOUBLER"
      enabledParam="doublerEnabled"
      spreadParam="doublerSpread"
      jitterParam="doublerJitter"
      spreadLabel="Spread"
      jitterLabel="Jitter"
    />
  </ChainContentRow>
);
