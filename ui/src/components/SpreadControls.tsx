import React, { useCallback, useRef, useState } from 'react';
import { rem } from '../hooks/useUiScale';
import { Power } from './icons';
import { KnobControl } from './KnobControl';
import { offsetMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { useDismissable } from '../hooks/useDismissable';
import { useTouchHold } from '../hooks/useTouchHold';
import { HELP, helpProps } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import { ImageDeckPanel, useImageDeckReset } from './ImageDeckPanel';
import {
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  KNOB_LABEL_GAP,
  faceplateChromeLift,
  pillButtonStyle,
} from './theme';

/**
 * Spread (mono chain mode): an ADT-style mono-to-stereo double where one channel
 * gets a wobbling short lag (see native Spread.h, plugin/docs/stereo-image.md).
 *
 * Faceplate face: a "SPREAD" advert pill while off; clicking it powers
 * spread on and reveals the bipolar Offset knob + power button (which
 * collapses back to the advert). Both states share one footprint so the
 * toggle never shifts the plate. Offset center = 0 ms = identity; the sign
 * picks which channel lags ("knob points at the fake one": precedence pulls
 * the image toward the dry side).
 *
 * Advanced controls are deliberately invisible: right-click anywhere on the
 * group (the standard plugin gesture for a control's extended options,
 * taught by the hover hint) opens the shared deck panel (ImageDeckPanel:
 * Wobble, Crossover, Diffuse, mono-safety LED). Spread's sections all
 * default on: the deck is its core sound.
 */

/** Default +15 ms R on the bipolar ±24 ms offset (matches the APVTS default). */
const SPREAD_OFFSET_DEFAULT = 0.8125;

const CHROME_LIFT = faceplateChromeLift(KNOB_SIZE_SECONDARY);

/** Fixed slot width shared by the advert pill, the expanded knob + power
    row, and the stereo-mode Align group: every state of the stereo-image
    slot occupies the same footprint so toggles never shift the plate (it's
    laid out with a fixed footprint). Sized for the advert, the widest face. */
export const IMAGE_GROUP_WIDTH = 148;
/** Secondary-knob centerline above the plate baseline (label + gap + radius);
    vertically centers the advert on the same line as the plate's buttons. */
const SECONDARY_CENTER_Y = KNOB_LABEL_GAP + 14 + KNOB_SIZE_SECONDARY / 2;
/** The advert stands as tall as the plate's primary knobs. */
const ADVERT_HEIGHT = KNOB_SIZE_PRIMARY;

/** Elongated outline triangle flanking "SPREAD": ~3.5:1 length:height,
    hollow stroke matching the advert pill border. Points right; flip for left.
    ViewBox pads the acute tip so the miter isn't clipped. */
const SpreadArrow: React.FC<{ direction: 'left' | 'right' }> = ({ direction }) => (
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

/** What sits in the slot while spread is off: a pill CTA that powers it on. */
const AdvertButton: React.FC<{ onClick: () => void }> = ({ onClick }) => (
  <button
    onClick={onClick}
    {...helpProps(HELP.spreadAdvert)}
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
    <SpreadArrow direction="left" />
    SPREAD
    <SpreadArrow direction="right" />
  </button>
);

/** Offset knob + power; right-click opens the advanced deck panel. */
export const SpreadGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('spreadEnabled', 'toggle');
  const [offset, setOffset, onOffsetDrag] = useParameter('spreadOffset', 'slider');
  const resetDeck = useImageDeckReset('spread');
  const [open, setOpen] = useState(false);
  const panelRef = useRef<HTMLDivElement | null>(null);
  const close = useCallback(() => setOpen(false), []);

  // Dismissal checks against the panel, not the group wrapper: the Offset
  // knob and power live in the same wrapper but a click on them should
  // close the panel. primaryOnly keeps the contextmenu toggle working.
  useDismissable(open, panelRef, close, { primaryOnly: true });

  // Touch and hold on the Offset knob is the right-click of the platform:
  // WKWebView never fires contextmenu for a long press, so without this the
  // advanced deck has no route at all on iPad. On the knob, not the group,
  // so the power button stays a plain tap. Renders nothing off iOS.
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
        // Centered spacer/knob/power row: the spacer mirrors the power
        // button's box so the knob lands dead center in the slot while the
        // power keeps the plate-standard 10px gap to its knob.
        <div
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'flex-end',
            justifyContent: 'center',
            gap: '10rem',
          }}
        >
          <div style={{ width: `${ICON_BOX_SIZE}rem`, flexShrink: 0 }} />
          {/* Panel anchors to the Offset knob so left:100% is the knob's
              right edge, not the group's. The hold wrapper covers the knob
              alone: the panel hangs in the same box, and a hold on its own
              knobs must not toggle the deck under the finger. */}
          <div style={{ position: 'relative' }}>
            <div {...holdProps}>
              <KnobControl
                label="Offset"
                value={offset}
                onChange={setOffset}
                variant="bipolar"
                size={KNOB_SIZE_PRIMARY}
                scale={offsetMsScale}
                defaultValue={SPREAD_OFFSET_DEFAULT}
                onReset={resetDeck}
                help={HELP.spreadOffset}
                onDragStateChange={onOffsetDrag}
              />
            </div>
            {open && <ImageDeckPanel feature="spread" ref={panelRef} fromKnob />}
          </div>
          <ChromeIconButton
            tone="power"
            on
            help={HELP.spreadPower}
            onClick={() => setEnabled(false)}
            offsetY={CHROME_LIFT}
          >
            <Power size={ICON_SIZE} />
          </ChromeIconButton>
        </div>
      ) : (
        <AdvertButton onClick={() => setEnabled(true)} />
      )}
      {open && !enabled && <ImageDeckPanel feature="spread" ref={panelRef} />}
    </div>
  );
};
