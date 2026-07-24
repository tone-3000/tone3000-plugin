import React, { useEffect, useState } from 'react';
import { Equal, Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { balanceDbScale, gainDbScale, gateDbScale, toneScale } from './knobScale';
import { SpreadGroup } from './SpreadControls';
import { useParameter } from '../hooks/useParameter';
import type { InputMode } from '../types/chain';
import { useNativeFunction } from '../hooks/useFunction';
import { HELP, helpProps } from './helpText';
import { ACTIVE_OUTLINE, BORDER, GRAY, HIGHLIGHT } from './theme';

/**
 * Bottom faceplate: main input/output gain, gate and the global 3-band tone
 * stack. Gate + tone stack carry power switches (APVTS bool params, so they
 * automate and persist like everything else on the plate).
 *
 * Input: a single level knob (per-channel trims live on the chain blocks).
 * When a real stereo source feeds the plugin, an input-mode button joins it
 * to pick what feeds the chain: both channels, or just L/R mirrored onto
 * both. The mode is chain state (session-persisted, not a preset value —
 * it's I/O routing, not tone).
 *
 * Output gain: the main level knob plus a small balance knob that trims L/R
 * against each other (±12 dB opposing, center = off). The balance knob only
 * appears when the output actually runs stereo (stereo mode, or mono+spread);
 * DSP forces center when inactive so a leftover setting can't skew a mono
 * bus. All values are host parameters — presets/undo get them for free.
 */

const KNOB_SIZE = 48;
/** Small companion knobs (Bal, Gate) — secondary style. */
const SECONDARY_KNOB_SIZE = 32;
const PLATE_HEIGHT = 100;

/** Every action button on the plate (power, input mode, auto-balance) sits
    at one shared height: centered on the secondary knobs. All knob groups
    bottom-align on the plate's baseline with a 10px gap + 14px label slot
    under each knob, so a button of height h starts at the baseline and
    lifts by 24 + secondary radius − h/2. */
const buttonLift = (buttonHeight: number) =>
  -(10 + 14 + SECONDARY_KNOB_SIZE / 2 - buttonHeight / 2);

/** Shared shell for the small square buttons that sit beside knobs
    (power switches, input mode). */
const SIDE_BUTTON_STYLE: React.CSSProperties = {
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
  transform: `translateY(${buttonLift(22)}px)`,
};

const PowerButton: React.FC<{
  on: boolean;
  help: string;
  onClick: () => void;
}> = ({ on, help, onClick }) => (
  <button
    onClick={onClick}
    {...helpProps(help)}
    style={{
      ...SIDE_BUTTON_STYLE,
      color: on ? '#ffffff' : GRAY,
      backgroundColor: on ? 'transparent' : HIGHLIGHT,
    }}
  >
    <Power size={12} />
  </button>
);

/** Two overlapping circles — the classic stereo glyph (lucide has none).
    Sized to sit next to the 12px Power icon without overpowering it. */
const StereoIcon: React.FC = () => (
  <svg
    width={15}
    height={12}
    viewBox="0 0 15 12"
    fill="none"
    stroke="currentColor"
    strokeWidth={1.4}
  >
    <circle cx="5.25" cy="6" r="4.4" />
    <circle cx="9.75" cy="6" r="4.4" />
  </svg>
);

const INPUT_MODE_CYCLE: Record<InputMode, InputMode> = {
  stereo: 'left',
  left: 'right',
  right: 'stereo',
};

/**
 * Input mode: which channels of a stereo source feed the plugin. Click
 * cycles stereo → L → R. Stereo (the default) shows the two-circle glyph;
 * L/R take only that channel (mirrored onto both) and get the filled
 * "engaged" look so a non-default routing is obvious at a glance.
 */
const InputModeButton: React.FC<{
  mode: InputMode;
  onChange: (mode: InputMode) => void;
}> = ({ mode, onChange }) => (
  <button
    onClick={() => onChange(INPUT_MODE_CYCLE[mode])}
    {...helpProps(HELP.inputMode)}
    style={{
      ...SIDE_BUTTON_STYLE,
      color: '#ffffff',
      backgroundColor: mode === 'stereo' ? 'transparent' : HIGHLIGHT,
    }}
  >
    {mode === 'stereo' ? (
      <StereoIcon />
    ) : (
      <span style={{ fontFamily: 'monospace', fontSize: '11px', fontWeight: 700, lineHeight: 1 }}>
        {mode === 'left' ? 'L' : 'R'}
      </span>
    )}
  </button>
);

/**
 * Auto balance: one-shot L/R energy match. Click arms a listening
 * measurement on the native side — play for ~2 s and the measured dB
 * difference is written into the outputBalance parameter (the Bal knob
 * visibly moves). Engaged style (white outline + fill, like PRE) while
 * listening; click again to cancel; times out after 15 s of silence.
 */
const AutoBalanceButton: React.FC = () => {
  const start = useNativeFunction<boolean>('startAutoBalance');
  const cancel = useNativeFunction<boolean>('cancelAutoBalance');
  const poll = useNativeFunction<{ state: string; matchedDb?: number }>('pollAutoBalance');
  const [listening, setListening] = useState(false);

  useEffect(() => {
    if (!listening) return;
    const id = setInterval(async () => {
      const res = await poll();
      if (res && res.state !== 'listening') setListening(false);
    }, 200);
    return () => clearInterval(id);
  }, [listening, poll]);

  const handleClick = async () => {
    if (listening) {
      await cancel();
      setListening(false);
    } else {
      await start();
      setListening(true);
    }
  };

  return (
    <button
      onClick={handleClick}
      {...helpProps(HELP.autoBalance)}
      style={{
        width: '18px',
        height: '18px',
        borderRadius: '5px',
        border: listening ? ACTIVE_OUTLINE : BORDER,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        cursor: 'pointer',
        padding: 0,
        flexShrink: 0,
        boxSizing: 'border-box',
        color: listening ? '#ffffff' : GRAY,
        backgroundColor: listening ? HIGHLIGHT : 'transparent',
        transform: `translateY(${buttonLift(18)}px)`,
      }}
    >
      {/* Size 12 keeps the glyph's inset even (16px content box − 12 = 2px
          per side); 11 forced a half-pixel offset that read as off-center. */}
      <Equal size={12} />
    </button>
  );
};

/**
 * Output gain knob. When the output runs stereo, a smaller balance knob
 * joins it (center = no effect, off-center trims L/R against each other by
 * up to ±12 dB on top of the main level), plus the auto-balance (=) button
 * when two independent chains are running.
 */
const OutputGainKnob: React.FC<{
  stereo: boolean;
  /** Show the auto-balance (=) button next to the balance knob. */
  autoBalance: boolean;
}> = ({ stereo, autoBalance }) => {
  const [level, setLevel] = useParameter('outputLevel', 'slider');
  const [balance, setBalance] = useParameter('outputBalance', 'slider');

  // The (=) button sits on the outer edge, keeping Bal next to the main
  // knob: [=][Bal][knob]. Inactive companions stay mounted but invisible so
  // the group's footprint is constant — toggling stereo/spread must not
  // shift the plate (it's laid out with space-between).
  return (
    <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
      <div style={{ visibility: autoBalance ? 'visible' : 'hidden' }}>
        <AutoBalanceButton />
      </div>
      <div style={{ visibility: stereo ? 'visible' : 'hidden' }}>
        <KnobControl
          label="Bal"
          value={balance}
          onChange={setBalance}
          size={SECONDARY_KNOB_SIZE}
          labelSize={12}
          thumb="secondary"
          scale={balanceDbScale}
          defaultValue={0.5}
          help={HELP.outputBalance}
        />
      </div>
      <KnobControl
        label="Output"
        value={level}
        onChange={setLevel}
        size={KNOB_SIZE}
        labelSize={12}
        scale={gainDbScale}
        defaultValue={0.5}
        help={HELP.outputLevel}
      />
    </div>
  );
};

interface FaceplateProps {
  /** Output stage runs stereo (stereo mode or mono-mode spread) — shows the
      output balance knob. */
  stereoOutput: boolean;
  /** Two independent chains are running (stereo mode) — shows the auto
      balance button. Mono-mode spread doesn't need it: both channels carry
      the same chain, so their energy already matches. */
  stereoChains: boolean;
  /** Plugin is fed a real stereo source — shows the input-mode button. */
  stereoInput: boolean;
  inputMode: InputMode;
  onInputModeChange: (mode: InputMode) => void;
}

export const Faceplate: React.FC<FaceplateProps> = ({
  stereoOutput,
  stereoChains,
  stereoInput,
  inputMode,
  onInputModeChange,
}) => {
  const [inputLevel, setInputLevel] = useParameter('inputLevel', 'slider');
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
        // Bottom-align every group so labels and knob bottoms share one
        // baseline regardless of knob size (Gate/Bal are shorter columns
        // than the 48px knobs). The padding re-centers the tallest columns
        // in the plate: (100 - 72) / 2.
        alignItems: 'flex-end',
        justifyContent: 'space-between',
        flexShrink: 0,
        borderTop: BORDER,
        background: '#1C1C1E',
        // Sides: wider than the meter section's 24px gutters so the
        // Input/Output knobs (48px) sit centered under the main meters' dot
        // columns (24px gutter + 18px labels + 10px gap puts the dots ~55px
        // in). Bottom: re-centers the tallest knob columns, (100 - 72) / 2.
        padding: '0 32px 14px',
        boxSizing: 'border-box',
      }}
    >
      {/* Input level + channel mode (mode only when the source is stereo).
          Rows bottom-align so the buttons' baseline lift lands them all at
          the same plate-wide height. */}
      <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
        <KnobControl
          label="Input"
          value={inputLevel}
          onChange={setInputLevel}
          size={KNOB_SIZE}
          labelSize={12}
          scale={gainDbScale}
          defaultValue={0.5}
          help={HELP.inputLevel}
        />
        {stereoInput && <InputModeButton mode={inputMode} onChange={onInputModeChange} />}
      </div>

      {/* Gate + power */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'flex-end',
          gap: '10px',
          opacity: gateEnabled ? 1 : 0.55,
        }}
      >
        <KnobControl
          label="Gate"
          value={noiseGate}
          onChange={setNoiseGate}
          size={SECONDARY_KNOB_SIZE}
          labelSize={12}
          thumb="secondary"
          scale={gateDbScale}
          defaultValue={gateDbScale.fromDisplay(-80)}
          help={HELP.gate}
        />
        <PowerButton
          on={gateEnabled}
          help={HELP.gatePower}
          onClick={() => setGateEnabled(!gateEnabled)}
        />
      </div>

      {/* 3-band tone stack + power */}
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'flex-end',
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
          scale={toneScale}
          defaultValue={toneScale.fromDisplay(5)}
          help={HELP.toneBass}
        />
        <KnobControl
          label="Middle"
          value={toneMid}
          onChange={setToneMid}
          size={KNOB_SIZE}
          labelSize={12}
          scale={toneScale}
          defaultValue={toneScale.fromDisplay(5)}
          help={HELP.toneMiddle}
        />
        <KnobControl
          label="Treble"
          value={toneTreble}
          onChange={setToneTreble}
          size={KNOB_SIZE}
          labelSize={12}
          scale={toneScale}
          defaultValue={toneScale.fromDisplay(5)}
          help={HELP.toneTreble}
        />
        <PowerButton
          on={toneEqEnabled}
          help={HELP.tonePower}
          onClick={() => setToneEqEnabled(!toneEqEnabled)}
        />
      </div>

      {/* Spread (offset/jit knobs) lives on the plate, just before the
          output stage it feeds. Stereo mode shows the knobs with a power
          switch; mono mode advertises the feature with a button until it's
          turned on (see SpreadControls). */}
      <SpreadGroup stereoMode={stereoChains} />

      <OutputGainKnob stereo={stereoOutput} autoBalance={stereoChains} />
    </div>
  );
};
