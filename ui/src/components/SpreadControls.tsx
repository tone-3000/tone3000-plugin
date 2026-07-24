import React from 'react';
import { Power, Radio, X } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { jitterMsScale, offsetMsScale } from './knobScale';
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
 * The knobs are Offset (the delay) and Jit; "Spread" is the feature name,
 * not a knob. Presentation differs by mode:
 * - Stereo: Offset/Jit are always on the plate with a power switch —
 *   width is a first-class control between two real chains.
 * - Mono: the plate shows an advert button instead of parked dimmed knobs;
 *   clicking it powers spread on and reveals the controls, and an X in the
 *   power switch's spot powers it off and collapses back to the advert.
 *
 * The Offset knob is bipolar — center = 0 ms = processing skipped; left of
 * center delays the left channel, right of center the right.
 */

/** Matches the main faceplate knobs (Input/Gate/tone stack/Output). */
const KNOB_SIZE = 48;
/** Companion trim next to Offset — same size language as Bal next to Output. */
const JITTER_KNOB_SIZE = 32;
/** Lifts a 22px button from the group's bottom-aligned baseline to the
    secondary knobs' center — the shared height of every faceplate action
    button (10px gap + 14px label slot below the knob, then up to its
    center). */
export const KNOB_CENTER_OFFSET = -(10 + 14 + JITTER_KNOB_SIZE / 2 - 11);
/** Footprint of the expanded group — Offset + Jit + 22px switch with 10px
    gaps — so the mono-mode advert can reserve the same width and toggling
    spread never shifts the plate. */
const SPREAD_GROUP_WIDTH = KNOB_SIZE + 10 + JITTER_KNOB_SIZE + 10 + 22;
/** Shared secondary-knob centerline height above the plate baseline
    (label slot + gap + radius). Used to vertically center the advert. */
const SECONDARY_CENTER_Y = 10 + 14 + JITTER_KNOB_SIZE / 2;
const SPREAD_ADVERT_HEIGHT = 48;

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

/** Mono-mode advert: what sits on the plate while spread is off. */
const SpreadAdvertButton: React.FC<{ onClick: () => void }> = ({ onClick }) => (
  <button
    onClick={onClick}
    {...helpProps(HELP.spreadAdvert)}
    style={{
      // Same width as the expanded group (no layout shift). Height matches
      // the primary knobs; margin centers it on the shared secondary-knob
      // centerline used by every faceplate action button.
      height: `${SPREAD_ADVERT_HEIGHT}px`,
      width: `${SPREAD_GROUP_WIDTH}px`,
      marginBottom: `${SECONDARY_CENTER_Y - SPREAD_ADVERT_HEIGHT / 2}px`,
      boxSizing: 'border-box',
      borderRadius: `${SPREAD_ADVERT_HEIGHT / 2}px`,
      border: '1px solid #ffffff',
      background: 'transparent',
      color: '#ffffff',
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      justifyContent: 'center',
      gap: '8px',
      padding: 0,
      fontSize: '12px',
      fontWeight: 600,
      letterSpacing: '0.08em',
      cursor: 'pointer',
      flexShrink: 0,
    }}
  >
    <Radio size={14} />
    SPREAD
  </button>
);

/** Offset + Jit knobs with the mode-appropriate switch (see file comment). */
export const SpreadGroup: React.FC<{
  /** Stereo mode shows the knobs permanently with a power switch; mono mode
      collapses to the advert button while spread is off. */
  stereoMode: boolean;
}> = ({ stereoMode }) => {
  const [enabled, setEnabled] = useParameter('spreadEnabled', 'toggle');
  const [offset, setOffset] = useParameter('spreadOffset', 'slider');
  const [jitter, setJitter] = useParameter('spreadJitter', 'slider');

  if (!stereoMode && !enabled) return <SpreadAdvertButton onClick={() => setEnabled(true)} />;

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'flex-end',
        gap: '10px',
        opacity: enabled ? 1 : 0.55,
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
        <KnobControl
          label="Offset"
          value={offset}
          onChange={setOffset}
          variant="bipolar"
          size={KNOB_SIZE}
          labelSize={12}
          scale={offsetMsScale}
          defaultValue={0.75}
          help={HELP.spreadOffset}
        />
        <KnobControl
          label="Jit"
          value={jitter}
          onChange={setJitter}
          size={JITTER_KNOB_SIZE}
          labelSize={12}
          thumb="secondary"
          scale={jitterMsScale}
          defaultValue={0.5}
          help={HELP.jitter}
        />
      </div>
      {stereoMode ? (
        <PillIconButton on={enabled} help={HELP.spreadPower} onClick={() => setEnabled(!enabled)}>
          <Power size={12} />
        </PillIconButton>
      ) : (
        <PillIconButton on help={HELP.spreadClose} onClick={() => setEnabled(false)}>
          <X size={12} />
        </PillIconButton>
      )}
    </div>
  );
};
