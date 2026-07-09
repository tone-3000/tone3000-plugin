import React from 'react';
import { Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';

/**
 * Bottom faceplate: main input/output gain, gate and the global 3-band tone
 * stack. Gate + tone stack carry power switches (APVTS bool params, so they
 * automate and persist like everything else on the plate).
 *
 * Stereo gains: one main level knob per stage plus a small balance knob that
 * trims L/R against each other (±12 dB opposing, center = off). The balance
 * knob only appears when the stage actually runs stereo; the parameter is
 * inert on mono buffers either way. All values are host parameters —
 * presets/undo get them for free.
 */

const KNOB_SIZE = 36;
const BALANCE_KNOB_SIZE = 24;
const PLATE_HEIGHT = 100;

const BORDER = '1px solid rgba(84, 84, 88, 0.65)';

/** Offset that vertically centers side-controls on the knob itself (the
    knob column is knob + gap + label; the label pulls its center down). */
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

/**
 * Main gain knob for a stage (input/output). When the stage runs stereo, a
 * smaller balance knob joins it: center = no effect, off-center trims L/R
 * against each other by up to ±12 dB on top of the main level.
 */
const MainGainKnob: React.FC<{
  label: string;
  type: 'input' | 'output';
  stereo: boolean;
  /** Which side of the main knob the balance knob sits on. */
  balanceSide: 'left' | 'right';
}> = ({ label, type, stereo, balanceSide }) => {
  const [level, setLevel] = useParameter(`${type}Level`, 'slider');
  const [balance, setBalance] = useParameter(`${type}Balance`, 'slider');

  const knob = (
    <KnobControl
      label={label}
      value={level}
      onChange={setLevel}
      size={KNOB_SIZE}
      labelSize={12}
    />
  );

  if (!stereo) return knob;

  const balanceKnob = (
    <KnobControl
      label="Bal"
      value={balance}
      onChange={setBalance}
      size={BALANCE_KNOB_SIZE}
      labelSize={10}
    />
  );
  return (
    <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
      {balanceSide === 'left' && balanceKnob}
      {knob}
      {balanceSide === 'right' && balanceKnob}
    </div>
  );
};

interface FaceplateProps {
  /** Stereo (dual-chain) mode — enables per-channel output gain. */
  stereoEnabled: boolean;
  /** Plugin is fed a real stereo source — enables per-channel input gain. */
  stereoInput: boolean;
}

export const Faceplate: React.FC<FaceplateProps> = ({ stereoEnabled, stereoInput }) => {
  const [toneBass, setToneBass] = useParameter('toneBass', 'slider');
  const [toneMid, setToneMid] = useParameter('toneMid', 'slider');
  const [toneTreble, setToneTreble] = useParameter('toneTreble', 'slider');
  const [noiseGate, setNoiseGate] = useParameter('gateThreshold', 'slider');
  const [gateEnabled, setGateEnabled] = useParameter('gateEnabled', 'toggle');
  const [toneEqEnabled, setToneEqEnabled] = useParameter('toneEqEnabled', 'toggle');

  return (
    <div
      style={{
        width: '100%',
        height: `${PLATE_HEIGHT}px`,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        flexShrink: 0,
        borderTop: BORDER,
        background: '#1C1C1E',
        padding: '0 24px',
        boxSizing: 'border-box',
      }}
    >
      <MainGainKnob label="Input" type="input" stereo={stereoInput} balanceSide="right" />

      {/* Gate + power */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '10px',
          opacity: gateEnabled ? 1 : 0.55,
        }}
      >
        <KnobControl
          label="Gate"
          value={noiseGate}
          onChange={setNoiseGate}
          size={KNOB_SIZE}
          labelSize={12}
        />
        <PowerButton
          on={gateEnabled}
          title={gateEnabled ? 'Turn gate off' : 'Turn gate on'}
          onClick={() => setGateEnabled(!gateEnabled)}
        />
      </div>

      {/* 3-band tone stack + power */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '24px',
          opacity: toneEqEnabled ? 1 : 0.55,
        }}
      >
        <KnobControl
          label="Bass"
          value={toneBass}
          onChange={setToneBass}
          size={KNOB_SIZE}
          labelSize={12}
        />
        <KnobControl
          label="Middle"
          value={toneMid}
          onChange={setToneMid}
          size={KNOB_SIZE}
          labelSize={12}
        />
        <KnobControl
          label="Treble"
          value={toneTreble}
          onChange={setToneTreble}
          size={KNOB_SIZE}
          labelSize={12}
        />
        <PowerButton
          on={toneEqEnabled}
          title={toneEqEnabled ? 'Turn EQ off' : 'Turn EQ on'}
          onClick={() => setToneEqEnabled(!toneEqEnabled)}
        />
      </div>

      <MainGainKnob label="Output" type="output" stereo={stereoEnabled} balanceSide="left" />
    </div>
  );
};
