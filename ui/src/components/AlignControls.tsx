import React, { useCallback, useRef, useState } from 'react';
import { rem } from '../hooks/useUiScale';
import { Equal, Power } from './icons';
import { KnobControl } from './KnobControl';
import { offsetMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { useDismissable } from '../hooks/useDismissable';
import { useTouchHold } from '../hooks/useTouchHold';
import { useAutoMeasure, type AutoMeasureResult } from '../hooks/useAutoMeasure';
import { HELP, helpProps } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import { ImageDeckPanel, useImageDeckReset } from './ImageDeckPanel';
import {
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  KNOB_LABEL_GAP,
  faceplateChromeLift,
  pillButtonStyle,
} from './theme';
import { IMAGE_GROUP_WIDTH } from './SpreadControls';

/**
 * Align (stereo chain mode): a corrective alignment delay on one chain (see
 * native StereoOffset.h), e.g. two captures of one performance landing a
 * few ms apart, or NAM models / IRs with different baked-in latency.
 *
 * Faceplate face: an "ALIGN" advert pill while off; clicking it powers
 * align on and reveals the bipolar Offset knob, auto-align button, and
 * power (which collapses back to the advert). Both states share one
 * footprint so the toggle never shifts the plate. Offset center = 0 ms =
 * identity; the sign picks which chain is delayed.
 *
 * The auto (=) button measures the alignment for you: one click mutes the
 * output for under half a second while an internal sweep drives both chains
 * (see native AutoOffset.h), and the measured inter-chain lag is written
 * into the offset (powering align on when there's a real correction). The
 * measurement also catches chains that are polarity-inverted against each
 * other and toggles a chain Ø (the chips on the pan rail) to match.
 *
 * Right-click opens the shared deck panel (ImageDeckPanel: Wobble,
 * Crossover, Diffuse, mono-safety LED), the same sections as Spread's but
 * all default OFF: align stays purely corrective until the deck is asked
 * for, at which point the delayed chain reads as an ADT-style second take.
 *
 * Occupies the same fixed slot as the mono-mode Spread group
 * (IMAGE_GROUP_WIDTH).
 */

const CHROME_LIFT = faceplateChromeLift(KNOB_SIZE_SECONDARY);

const SECONDARY_CENTER_Y = KNOB_LABEL_GAP + 14 + KNOB_SIZE_SECONDARY / 2;
const ADVERT_HEIGHT = KNOB_SIZE_PRIMARY;

/** Same elongated outline triangle as Spread, but the advert flips the
    directions so the arrows point inward (→ ALIGN ←) instead of out. */
const AlignArrow: React.FC<{ direction: 'left' | 'right' }> = ({ direction }) => (
  <svg
    viewBox="0 0 24 10"
    fill="none"
    aria-hidden
    style={{
      width: rem(20),
      height: rem(7),
      flexShrink: 0,
      display: 'block',
      transform: direction === 'left' ? 'scaleX(-1)' : undefined,
    }}
  >
    <path
      d="M2 2 L20.5 5 L2 8 Z"
      stroke="currentColor"
      strokeWidth={1.15}
      strokeLinejoin="miter"
      strokeMiterlimit={10}
    />
  </svg>
);

/** What sits in the slot while align is off: a pill CTA that powers it on. */
const AdvertButton: React.FC<{ onClick: () => void }> = ({ onClick }) => (
  <button
    onClick={onClick}
    {...helpProps(HELP.alignAdvert)}
    style={{
      ...pillButtonStyle,
      height: `${ADVERT_HEIGHT}rem`,
      width: `${IMAGE_GROUP_WIDTH}rem`,
      marginBottom: `${SECONDARY_CENTER_Y - ADVERT_HEIGHT / 2}rem`,
      boxSizing: 'border-box',
      borderRadius: `${ADVERT_HEIGHT / 2}rem`,
      padding: 0,
      fontSize: '12rem',
      letterSpacing: 'normal',
      gap: '8rem',
    }}
  >
    <AlignArrow direction="right" />
    ALIGN
    <AlignArrow direction="left" />
  </button>
);

/**
 * Auto align: one-shot chain time alignment. Click runs an internal probe
 * on the native side (output muted for ~½ s) and the measured lag is
 * written into alignOffset (the Offset knob visibly moves). Yellow while
 * measuring; click again to cancel. An untrustworthy measurement (e.g. a
 * chain edit landing mid-probe) is discarded silently.
 */
/** matchedMs is the measured inter-chain lag (positive = the right chain
    gets delayed), mirroring the Offset knob move; below the native
    "already aligned" floor no delay correction was applied. polarityFlipped
    reports a chain Ø toggled alongside. Two decimals: the probe measures to
    sub-sample precision. */
const alignDoneMessage = ({
  matchedMs = 0,
  polarityFlipped = false,
}: AutoMeasureResult): string => {
  const delay =
    Math.abs(matchedMs) < 0.05
      ? null
      : `${matchedMs > 0 ? 'R' : 'L'} +${Math.abs(matchedMs).toFixed(2)} ms`;
  return ['Aligned', delay, polarityFlipped ? 'Ø flipped' : null]
    .filter((part) => part != null)
    .join(' · ');
};

const AutoAlignButton: React.FC = () => {
  const { listening, toggle } = useAutoMeasure(
    'startAutoOffset',
    'cancelAutoOffset',
    'pollAutoOffset',
    alignDoneMessage,
    'Measuring'
  );
  return (
    <ChromeIconButton
      tone="armed"
      on={listening}
      help={HELP.autoAlign}
      onClick={toggle}
      offsetY={CHROME_LIFT}
    >
      <Equal size={ICON_SIZE} />
    </ChromeIconButton>
  );
};

/** Offset knob + auto + power; right-click opens the advanced deck panel;
    collapses to the advert when powered off. */
export const AlignGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('alignEnabled', 'toggle');
  const [offset, setOffset, onOffsetDrag] = useParameter('alignOffset', 'slider');
  const resetDeck = useImageDeckReset('align');
  const [open, setOpen] = useState(false);
  const panelRef = useRef<HTMLDivElement | null>(null);
  const close = useCallback(() => setOpen(false), []);

  // Dismissal checks against the panel, not the group wrapper: the Offset
  // knob, auto, and power live in the same wrapper but a click on them
  // should close the panel. primaryOnly keeps the contextmenu toggle
  // working. (Same pattern as SpreadGroup.)
  useDismissable(open, panelRef, close, { primaryOnly: true });

  // Touch and hold on the Offset knob is the right-click of the platform:
  // WKWebView never fires contextmenu for a long press, so without this the
  // advanced deck has no route at all on iPad. On the knob, not the group,
  // so auto and power stay plain taps. Renders nothing off iOS.
  const toggle = useCallback(() => setOpen((prev) => !prev), []);
  const holdProps = useTouchHold(toggle);

  return (
    <div
      onContextMenu={(e) => {
        e.preventDefault();
        setOpen((prev) => !prev);
      }}
      style={{ position: 'relative', width: `${IMAGE_GROUP_WIDTH}rem`, boxSizing: 'border-box' }}
    >
      {enabled ? (
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'flex-end',
            justifyContent: 'center',
            gap: '10rem',
          }}
        >
          <AutoAlignButton />
          {/* Panel anchors to the Offset knob so its left edge tracks the
              knob, matching the spread group. The hold wrapper covers the
              knob alone: the panel hangs in the same box, and a hold on its
              own knobs must not toggle the deck under the finger. */}
          <div style={{ position: 'relative' }}>
            <div {...holdProps}>
              <KnobControl
                label="Offset"
                value={offset}
                onChange={setOffset}
                variant="bipolar"
                size={KNOB_SIZE_PRIMARY}
                scale={offsetMsScale}
                defaultValue={0.5}
                onReset={resetDeck}
                help={HELP.alignOffset}
                onDragStateChange={onOffsetDrag}
              />
            </div>
            {open && <ImageDeckPanel feature="align" ref={panelRef} fromKnob />}
          </div>
          <ChromeIconButton
            tone="power"
            on
            help={HELP.alignPower}
            onClick={() => setEnabled(false)}
            offsetY={CHROME_LIFT}
          >
            <Power size={ICON_SIZE} />
          </ChromeIconButton>
        </div>
      ) : (
        <AdvertButton onClick={() => setEnabled(true)} />
      )}
      {open && !enabled && <ImageDeckPanel feature="align" ref={panelRef} />}
    </div>
  );
};
