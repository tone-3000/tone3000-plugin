import React, { useState } from 'react';
import { Link, Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { useParameter } from '../hooks/useParameter';

/**
 * Bottom faceplate: main input/output gain, gate and the global 3-band tone
 * stack. Gate + tone stack carry power switches (APVTS bool params, so they
 * automate and persist like everything else on the plate).
 *
 * Stereo gains: the L knob param ("inputLevel"/"outputLevel") is the main
 * value; when a stage is unlinked the R param takes over channel R and the
 * L/link/R toggle picks which one the knob edits. All four values + both
 * link flags are host parameters — presets/undo get them for free.
 */

const KNOB_SIZE = 36;
const PLATE_HEIGHT = 100;

const MUTED = 'rgba(235, 235, 245, 0.60)';
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

/** Vertical L / link / R selector for a stereo gain stage. */
const ChannelToggle: React.FC<{
  linked: boolean;
  side: 'l' | 'r';
  onSelect: (mode: 'l' | 'linked' | 'r') => void;
}> = ({ linked, side, onSelect }) => {
  const itemStyle = (active: boolean): React.CSSProperties => ({
    width: '22px',
    height: '20px',
    border: 'none',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    cursor: 'pointer',
    padding: 0,
    fontSize: '10px',
    letterSpacing: '0.5px',
    color: active ? '#ffffff' : MUTED,
    backgroundColor: active ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
  });

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        borderRadius: '6px',
        border: BORDER,
        overflow: 'hidden',
        flexShrink: 0,
        transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
      }}
    >
      <button
        onClick={() => onSelect('l')}
        title="Left channel gain"
        style={itemStyle(!linked && side === 'l')}
      >
        L
      </button>
      <button
        onClick={() => onSelect('linked')}
        title="Linked (both channels)"
        style={{ ...itemStyle(linked), borderTop: BORDER, borderBottom: BORDER }}
      >
        <Link size={10} />
      </button>
      <button
        onClick={() => onSelect('r')}
        title="Right channel gain"
        style={itemStyle(!linked && side === 'r')}
      >
        R
      </button>
    </div>
  );
};

/**
 * Main gain knob. In stereo it grows the L/link/R toggle: linked edits the
 * main (L) value and the DSP mirrors it to both channels; unlinked edits the
 * selected channel's own parameter.
 */
const MainGainKnob: React.FC<{
  label: string;
  type: 'input' | 'output';
  stereo: boolean;
  /** Which side of the knob the channel toggle sits on. */
  toggleSide: 'left' | 'right';
}> = ({ label, type, stereo, toggleSide }) => {
  const [level, setLevel] = useParameter(`${type}Level`, 'slider');
  const [levelR, setLevelR] = useParameter(`${type}LevelR`, 'slider');
  const [linked, setLinked] = useParameter(`${type}Linked`, 'toggle');
  const [side, setSide] = useState<'l' | 'r'>('l');

  const editingRight = stereo && !linked && side === 'r';

  const handleSelect = (mode: 'l' | 'linked' | 'r') => {
    if (mode === 'linked') {
      setLinked(true);
    } else {
      setLinked(false);
      setSide(mode);
    }
  };

  const knob = (
    <KnobControl
      label={stereo && !linked ? `${label} ${side.toUpperCase()}` : label}
      value={editingRight ? levelR : level}
      onChange={editingRight ? setLevelR : setLevel}
      size={KNOB_SIZE}
      labelSize={12}
    />
  );

  if (!stereo) return knob;

  const toggle = <ChannelToggle linked={linked} side={side} onSelect={handleSelect} />;
  return (
    <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '8px' }}>
      {toggleSide === 'left' && toggle}
      {knob}
      {toggleSide === 'right' && toggle}
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
      <MainGainKnob label="Input" type="input" stereo={stereoInput} toggleSide="right" />

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

      <MainGainKnob label="Output" type="output" stereo={stereoEnabled} toggleSide="left" />
    </div>
  );
};
