import React, { useEffect, useState } from 'react';
import { Equal, Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { balanceDbScale, gainDbScale, gateDbScale, toneScale } from './knobScale';
import { SpreadGroup } from './SpreadControls';
import { useParameter } from '../hooks/useParameter';
import type { InputMode } from '../types/chain';
import { useNativeFunction } from '../hooks/useFunction';
import { HELP } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import {
  BORDER,
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  faceplateChromeLift,
} from './theme';

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

const PLATE_HEIGHT = 100;
/** Shared faceplate action-button height: center of the secondary knobs. */
const CHROME_LIFT = faceplateChromeLift(KNOB_SIZE_SECONDARY);

const PowerButton: React.FC<{
  on: boolean;
  help: string;
  onClick: () => void;
}> = ({ on, help, onClick }) => (
  <ChromeIconButton tone="power" on={on} help={help} onClick={onClick} offsetY={CHROME_LIFT}>
    <Power size={ICON_SIZE} />
  </ChromeIconButton>
);

/** Two overlapping circles — the classic stereo glyph (lucide has none). */
const StereoIcon: React.FC<{ style?: React.CSSProperties }> = ({ style }) => (
  <svg
    width={17}
    height={10}
    viewBox="0 0 17 10"
    fill="none"
    xmlns="http://www.w3.org/2000/svg"
    style={{ display: 'block', flexShrink: 0, ...style }}
  >
    <circle cx="5" cy="5" r="4.375" stroke="currentColor" strokeWidth="1.25" />
    <circle cx="11.6665" cy="5" r="4.375" stroke="currentColor" strokeWidth="1.25" />
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
  <ChromeIconButton
    help={HELP.inputMode}
    onClick={() => onChange(INPUT_MODE_CYCLE[mode])}
    filled={mode !== 'stereo'}
    offsetY={CHROME_LIFT}
  >
    {mode === 'stereo' ? (
      <StereoIcon />
    ) : (
      <span style={{ fontFamily: 'monospace', fontSize: '11px', fontWeight: 700, lineHeight: 1 }}>
        {mode === 'left' ? 'L' : 'R'}
      </span>
    )}
  </ChromeIconButton>
);

/**
 * Auto balance: one-shot L/R energy match. Click arms a listening
 * measurement on the native side — play for ~2 s and the measured dB
 * difference is written into the outputBalance parameter (the Bal knob
 * visibly moves). Yellow (listening) while armed; click again to cancel;
 * times out after 15 s of silence.
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
    <ChromeIconButton
      tone="armed"
      on={listening}
      help={HELP.autoBalance}
      onClick={handleClick}
      offsetY={CHROME_LIFT}
    >
      <Equal size={ICON_SIZE} />
    </ChromeIconButton>
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
          size={KNOB_SIZE_SECONDARY}
          labelSize={12}
          variant="bipolar"
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
        size={KNOB_SIZE_PRIMARY}
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
          size={KNOB_SIZE_PRIMARY}
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
          size={KNOB_SIZE_SECONDARY}
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
          gap: '10px',
          opacity: toneEqEnabled ? 1 : 0.55,
        }}
      >
        <div style={{ display: 'flex', alignItems: 'flex-end', gap: '24px' }}>
          <KnobControl
            label="Bass"
            value={toneBass}
            onChange={setToneBass}
            size={KNOB_SIZE_PRIMARY}
            labelSize={12}
            scale={toneScale}
            defaultValue={toneScale.fromDisplay(5)}
            help={HELP.toneBass}
          />
          <KnobControl
            label="Middle"
            value={toneMid}
            onChange={setToneMid}
            size={KNOB_SIZE_PRIMARY}
            labelSize={12}
            scale={toneScale}
            defaultValue={toneScale.fromDisplay(5)}
            help={HELP.toneMiddle}
          />
          <KnobControl
            label="Treble"
            value={toneTreble}
            onChange={setToneTreble}
            size={KNOB_SIZE_PRIMARY}
            labelSize={12}
            scale={toneScale}
            defaultValue={toneScale.fromDisplay(5)}
            help={HELP.toneTreble}
          />
        </div>
        <PowerButton
          on={toneEqEnabled}
          help={HELP.tonePower}
          onClick={() => setToneEqEnabled(!toneEqEnabled)}
        />
      </div>

      {/* Spread (offset/jit knobs) lives on the plate, just before the
          output stage it feeds. Collapsed to an advert button while off;
          expands with Offset/Jit + power to collapse (same in mono and stereo). */}
      <SpreadGroup />

      <OutputGainKnob stereo={stereoOutput} autoBalance={stereoChains} />
    </div>
  );
};
