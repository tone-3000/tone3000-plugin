import React from 'react';
import { Power, Radio } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { jitterMsScale, offsetMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { HELP, helpProps } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import {
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  faceplateChromeLift,
  pillButtonStyle,
} from './theme';

/**
 * Spread: a short per-note delay on one channel for stereo width. One
 * parameter set serves both modes (they're mutually exclusive):
 * - Mono: doubles the chain and delays the chosen side (slap double).
 * - Stereo: delays the chosen side's chain in place.
 * The group lives on the faceplate in both modes.
 *
 * Presentation is the same either way: an advert button while off; clicking
 * it powers spread on and reveals Offset + Jit with a power button that
 * collapses back to the advert. The Offset knob is bipolar — center = 0 ms =
 * processing skipped; left of center delays the left channel, right of
 * center the right.
 */

/** Shared faceplate action-button height (secondary knob center). */
export const KNOB_CENTER_OFFSET = faceplateChromeLift(KNOB_SIZE_SECONDARY);
/** Footprint of the expanded group — Offset + Jit + chrome box with 10px
    gaps — so the advert can reserve the same width and toggling spread
    never shifts the plate. Both knobs are secondary here: Offset is bipolar,
    and bipolar controls take the secondary tone. */
const SPREAD_GROUP_WIDTH = 2 * KNOB_SIZE_SECONDARY + 20 + ICON_BOX_SIZE;
/** Shared secondary-knob centerline height above the plate baseline
    (label slot + gap + radius). Used to vertically center the advert. */
const SECONDARY_CENTER_Y = 10 + 14 + KNOB_SIZE_SECONDARY / 2;
/** Advert stands as tall as the plate's primary knobs. */
const SPREAD_ADVERT_HEIGHT = KNOB_SIZE_PRIMARY;

/** What sits on the plate while spread is off. */
const SpreadAdvertButton: React.FC<{ onClick: () => void }> = ({ onClick }) => (
  <button
    onClick={onClick}
    {...helpProps(HELP.spreadAdvert)}
    style={{
      ...pillButtonStyle(true),
      // Same width as the expanded group (no layout shift). Height matches
      // the primary knobs; margin centers it on the shared secondary-knob
      // centerline used by every faceplate action button.
      height: `${SPREAD_ADVERT_HEIGHT}px`,
      width: `${SPREAD_GROUP_WIDTH}px`,
      marginBottom: `${SECONDARY_CENTER_Y - SPREAD_ADVERT_HEIGHT / 2}px`,
      boxSizing: 'border-box',
      borderRadius: `${SPREAD_ADVERT_HEIGHT / 2}px`,
      padding: 0,
      fontSize: '12px',
      letterSpacing: '0.08em',
      gap: '8px',
    }}
  >
    <Radio size={ICON_BOX_SIZE} />
    SPREAD
  </button>
);

/** Offset + Jit knobs, or the advert button while spread is off. */
export const SpreadGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('spreadEnabled', 'toggle');
  const [offset, setOffset] = useParameter('spreadOffset', 'slider');
  const [jitter, setJitter] = useParameter('spreadJitter', 'slider');

  if (!enabled) return <SpreadAdvertButton onClick={() => setEnabled(true)} />;

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'flex-end',
        gap: '10px',
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-end', gap: '10px' }}>
        <KnobControl
          label="Offset"
          value={offset}
          onChange={setOffset}
          variant="bipolar"
          thumb="secondary"
          size={KNOB_SIZE_SECONDARY}
          labelSize={12}
          scale={offsetMsScale}
          defaultValue={0.5}
          help={HELP.spreadOffset}
        />
        <KnobControl
          label="Jit"
          value={jitter}
          onChange={setJitter}
          size={KNOB_SIZE_SECONDARY}
          labelSize={12}
          thumb="secondary"
          scale={jitterMsScale}
          defaultValue={0.5}
          help={HELP.jitter}
        />
      </div>
      <ChromeIconButton
        tone="power"
        on
        help={HELP.spreadPower}
        onClick={() => setEnabled(false)}
        offsetY={KNOB_CENTER_OFFSET}
      >
        <Power size={ICON_SIZE} />
      </ChromeIconButton>
    </div>
  );
};
