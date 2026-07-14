import React, { useEffect, useState } from 'react';
import { Equal, Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { SpreadGroup } from './SpreadControls';
import { useParameter } from '../hooks/useParameter';
import { useFunction } from '../hooks/useFunction';

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

const PowerButton: React.FC<{
  on: boolean;
  title: string;
  onClick: () => void;
  /** Vertical nudge; defaults to centering on a lone knob. Pass 0 when the
      button sits inside a pre-positioned stack. */
  offsetY?: number;
}> = ({ on, title, onClick, offsetY = KNOB_CENTER_OFFSET }) => (
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
      transform: `translateY(${offsetY}px)`,
    }}
  >
    <Power size={12} />
  </button>
);

/** PRE/POST position toggle for the tone stack: lit pill = EQ before the
    chain, dimmed = after (default). Same grayscale language as PowerButton. */
const PreButton: React.FC<{ on: boolean; onClick: () => void }> = ({ on, onClick }) => (
  <button
    onClick={onClick}
    title={on ? 'EQ runs before the chain — click for post' : 'EQ runs after the chain — click for pre'}
    style={{
      height: '15px',
      padding: '0 5px',
      borderRadius: '4px',
      border: on ? '1px solid rgba(255, 255, 255, 0.85)' : BORDER,
      background: on ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
      color: on ? '#ffffff' : '#8D8D93',
      fontSize: '8px',
      fontWeight: 700,
      letterSpacing: '0.8px',
      lineHeight: 1,
      cursor: 'pointer',
      flexShrink: 0,
    }}
  >
    PRE
  </button>
);

/**
 * Auto balance: one-shot L/R energy match. Click arms a listening
 * measurement on the native side — play for ~2 s and the measured dB
 * difference is written into the outputBalance parameter (the Bal knob
 * visibly moves). Yellow while listening; click again to cancel; times out
 * after 15 s of silence.
 */
const AutoBalanceButton: React.FC = () => {
  const start = useFunction<boolean>('startAutoBalance');
  const cancel = useFunction<boolean>('cancelAutoBalance');
  const poll = useFunction<{ state: string; matchedDb?: number }>('pollAutoBalance');
  const [listening, setListening] = useState(false);

  useEffect(() => {
    if (!listening) return;
    const id = setInterval(async () => {
      const res = await poll.invoke();
      if (res && res.state !== 'listening') setListening(false);
    }, 200);
    return () => clearInterval(id);
  }, [listening, poll.invoke]);

  const handleClick = async () => {
    if (listening) {
      await cancel.invoke();
      setListening(false);
    } else {
      await start.invoke();
      setListening(true);
    }
  };

  return (
    <button
      onClick={handleClick}
      title={
        listening
          ? 'Listening — play for a couple of seconds (click to cancel)'
          : 'Auto balance: match L/R output level'
      }
      style={{
        width: '18px',
        height: '18px',
        borderRadius: '5px',
        border: 'none',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        cursor: 'pointer',
        padding: 0,
        flexShrink: 0,
        color: listening ? '#FFFF00' : '#8D8D93',
        backgroundColor: listening ? 'rgba(255, 255, 0, 0.12)' : 'rgba(235, 235, 245, 0.18)',
        transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
      }}
    >
      <Equal size={11} />
    </button>
  );
};

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
  /** Show the auto-balance (=) button next to the balance knob. */
  autoBalance?: boolean;
}> = ({ label, type, stereo, balanceSide, autoBalance = false }) => {
  const [level, setLevel] = useParameter(`${type}Level`, 'slider');
  const [balance, setBalance] = useParameter(`${type}Balance`, 'slider');

  const knob = (
    <KnobControl
      label={label}
      value={level}
      onChange={setLevel}
      size={KNOB_SIZE}
      labelSize={12}
      innerColor="#1C1C1E"
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
      innerColor="#1C1C1E"
    />
  );
  // The (=) button always sits on the outer edge, keeping Bal next to its
  // main knob: [=][Bal][knob] on the left side, [knob][Bal][=] on the right.
  const autoBtn = autoBalance ? <AutoBalanceButton /> : null;
  return (
    <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
      {balanceSide === 'left' && autoBtn}
      {balanceSide === 'left' && balanceKnob}
      {knob}
      {balanceSide === 'right' && balanceKnob}
      {balanceSide === 'right' && autoBtn}
    </div>
  );
};

interface FaceplateProps {
  /** Output stage runs stereo (stereo mode or mono-mode spread) — shows the
      output balance knob. */
  stereoOutput: boolean;
  /** Plugin is fed a real stereo source — shows the input balance knob. */
  stereoInput: boolean;
  /** Two independent chains are running (stereo mode) — shows the auto
      balance button. Mono-mode spread doesn't need it: both channels carry
      the same chain, so their energy already matches. */
  stereoChains: boolean;
}

export const Faceplate: React.FC<FaceplateProps> = ({
  stereoOutput,
  stereoInput,
  stereoChains,
}) => {
  const [toneBass, setToneBass] = useParameter('toneBass', 'slider');
  const [toneMid, setToneMid] = useParameter('toneMid', 'slider');
  const [toneTreble, setToneTreble] = useParameter('toneTreble', 'slider');
  const [noiseGate, setNoiseGate] = useParameter('gateThreshold', 'slider');
  const [gateEnabled, setGateEnabled] = useParameter('gateEnabled', 'toggle');
  const [toneEqEnabled, setToneEqEnabled] = useParameter('toneEqEnabled', 'toggle');
  const [toneEqPre, setToneEqPre] = useParameter('toneEqPre', 'toggle');

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
          innerColor="#1C1C1E"
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
          innerColor="#1C1C1E"
        />
        <KnobControl
          label="Middle"
          value={toneMid}
          onChange={setToneMid}
          size={KNOB_SIZE}
          labelSize={12}
          innerColor="#1C1C1E"
        />
        <KnobControl
          label="Treble"
          value={toneTreble}
          onChange={setToneTreble}
          size={KNOB_SIZE}
          labelSize={12}
          innerColor="#1C1C1E"
        />
        {/* Power sits exactly where the other power buttons do; PRE hangs
            below it out-of-flow so it never shifts the power position. */}
        <div
          style={{
            position: 'relative',
            width: '22px',
            height: '22px',
            flexShrink: 0,
            transform: `translateY(${KNOB_CENTER_OFFSET}px)`,
          }}
        >
          <PowerButton
            on={toneEqEnabled}
            title={toneEqEnabled ? 'Turn EQ off' : 'Turn EQ on'}
            onClick={() => setToneEqEnabled(!toneEqEnabled)}
            offsetY={0}
          />
          <div
            style={{
              position: 'absolute',
              top: 'calc(100% + 5px)',
              left: '50%',
              transform: 'translateX(-50%)',
            }}
          >
            <PreButton on={toneEqPre} onClick={() => setToneEqPre(!toneEqPre)} />
          </div>
        </div>
      </div>

      {/* Spread/jitter (mono doubler & stereo offset) lives on the plate now,
          just before the output stage it feeds. */}
      <SpreadGroup innerColor="#1C1C1E" />

      <MainGainKnob
        label="Output"
        type="output"
        stereo={stereoOutput}
        balanceSide="left"
        autoBalance={stereoChains}
      />
    </div>
  );
};
