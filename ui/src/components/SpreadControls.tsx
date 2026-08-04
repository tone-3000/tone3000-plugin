import React, { useCallback, useRef, useState } from 'react';
import { Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { offsetMsScale, percentScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { useDismissable } from '../hooks/useDismissable';
import { useCorrelation } from '../hooks/useMeters';
import { HELP, helpProps } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import {
  BORDER,
  BRAND_RED,
  BRAND_YELLOW,
  ICON_BOX_SIZE,
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  SUBTLE,
  faceplateChromeLift,
  pillButtonStyle,
} from './theme';

/**
 * Spread (mono chain mode): an ADT-style mono-to-stereo double where one channel
 * gets a wobbling short lag (see native Spread.h and plugin/docs/spread.md).
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
 * taught by the hover hint) opens a small floating panel with the Wobble
 * knob (humanizing delay drift, absolute: up to ±1.2 ms around the offset,
 * not a fraction of it) and the mono-safety LED (live L/R output
 * correlation: dim = safe, yellow = caution, red = cancellation on a mono
 * sum). The crossover frequency and allpass cascade stay fixed by design;
 * they're what keep the low end mono-safe and the double decorrelated.
 */

/** Default +15 ms R on the bipolar ±24 ms offset (matches the APVTS default). */
const SPREAD_OFFSET_DEFAULT = 0.8125;
const SPREAD_WOBBLE_DEFAULT = 0.25;

const CHROME_LIFT = faceplateChromeLift(KNOB_SIZE_SECONDARY);

/** Fixed slot width shared by the advert pill, the expanded knob + power
    row, and the stereo-mode Offset group: every state of the stereo-image
    slot occupies the same footprint so toggles never shift the plate (it's
    laid out with space-between). Sized for the advert, the widest face. */
export const IMAGE_GROUP_WIDTH = 148;
/** Secondary-knob centerline above the plate baseline (label + gap + radius);
    vertically centers the advert on the same line as the plate's buttons. */
const SECONDARY_CENTER_Y = 10 + 14 + KNOB_SIZE_SECONDARY / 2;
/** The advert stands as tall as the plate's primary knobs. */
const ADVERT_HEIGHT = KNOB_SIZE_PRIMARY;

/** Elongated outline triangle flanking "SPREAD": ~3.5:1 length:height,
    hollow stroke matching the advert pill border. Points right; flip for left.
    ViewBox pads the acute tip so the miter isn't clipped. */
const SpreadArrow: React.FC<{ direction: 'left' | 'right' }> = ({ direction }) => (
  <svg
    width={20}
    height={7}
    viewBox="0 0 24 10"
    fill="none"
    aria-hidden
    style={{
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
      ...pillButtonStyle(true),
      height: `${ADVERT_HEIGHT}px`,
      width: `${IMAGE_GROUP_WIDTH}px`,
      marginBottom: `${SECONDARY_CENTER_Y - ADVERT_HEIGHT / 2}px`,
      boxSizing: 'border-box',
      borderRadius: `${ADVERT_HEIGHT / 2}px`,
      padding: 0,
      fontSize: '12px',
      letterSpacing: '0.08em',
      gap: '8px',
    }}
  >
    <SpreadArrow direction="left" />
    SPREAD
    <SpreadArrow direction="right" />
  </button>
);

/** Mono-safety LED. Below 0.5 correlation a mono fold-down audibly thins;
    below 0 it actively cancels. */
const CorrelationLed: React.FC = () => {
  const correlation = useCorrelation();
  const color = correlation < 0 ? BRAND_RED : correlation < 0.5 ? BRAND_YELLOW : SUBTLE;
  return (
    <div
      {...helpProps(HELP.spreadCorrelation)}
      style={{
        position: 'absolute',
        top: '8px',
        right: '8px',
        width: '6px',
        height: '6px',
        borderRadius: '50%',
        background: color,
      }}
    />
  );
};

/** The advanced panel: the Wobble knob + mono-safety LED, floating above
    the plate. When anchored to the Offset knob, its left edge starts at the
    knob's left side; otherwise (advert state) it flush-rights to the group. */
const AdvancedPanel = React.forwardRef<
  HTMLDivElement,
  { fromKnob?: boolean }
>(function AdvancedPanel({ fromKnob = false }, ref) {
  const [wobble, setWobble] = useParameter('spreadWobble', 'slider');
  return (
    <div
      ref={ref}
      style={{
        position: 'absolute',
        // Splits the difference down toward the knob's top edge instead of
        // floating a full gap above the group.
        bottom: 'calc(100% + 6px)',
        ...(fromKnob ? { left: 0 } : { right: 0 }),
        backgroundColor: '#141416',
        border: BORDER,
        borderRadius: '14px',
        padding: '14px 22px 8px',
        zIndex: 200,
        boxSizing: 'border-box',
      }}
    >
      <KnobControl
        label="Wobble"
        value={wobble}
        onChange={setWobble}
        size={KNOB_SIZE_SECONDARY}
        labelSize={12}
        thumb="secondary"
        scale={percentScale}
        defaultValue={SPREAD_WOBBLE_DEFAULT}
        help={HELP.spreadWobble}
      />
      <CorrelationLed />
    </div>
  );
});

/** Offset knob + power; right-click opens the advanced panel. */
export const SpreadGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('spreadEnabled', 'toggle');
  const [offset, setOffset] = useParameter('spreadOffset', 'slider');
  const [open, setOpen] = useState(false);
  const panelRef = useRef<HTMLDivElement | null>(null);
  const close = useCallback(() => setOpen(false), []);

  // Dismissal checks against the panel, not the group wrapper: the Offset
  // knob and power live in the same wrapper but a click on them should
  // close the panel. primaryOnly keeps the contextmenu toggle working.
  useDismissable(open, panelRef, close, { primaryOnly: true });

  return (
    <div
      onContextMenu={(e) => {
        e.preventDefault();
        setOpen((prev) => !prev);
      }}
      style={{ position: 'relative', width: `${IMAGE_GROUP_WIDTH}px`, boxSizing: 'border-box' }}
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
            gap: '10px',
          }}
        >
          <div style={{ width: `${ICON_BOX_SIZE}px`, flexShrink: 0 }} />
          {/* Panel anchors to the Offset knob so left:100% is the knob's
              right edge, not the group's. */}
          <div style={{ position: 'relative' }}>
            <KnobControl
              label="Offset"
              value={offset}
              onChange={setOffset}
              variant="bipolar"
              size={KNOB_SIZE_PRIMARY}
              labelSize={12}
              scale={offsetMsScale}
              defaultValue={SPREAD_OFFSET_DEFAULT}
              help={HELP.spreadOffset}
            />
            {open && <AdvancedPanel ref={panelRef} fromKnob />}
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
      {open && !enabled && <AdvancedPanel ref={panelRef} />}
    </div>
  );
};
