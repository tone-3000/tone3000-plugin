import React, { useEffect, useState } from 'react';
import { Equal, Power } from 'lucide-react';
import { KnobControl } from './KnobControl';
import { offsetMsScale } from './knobScale';
import { useParameter } from '../hooks/useParameter';
import { useNativeFunction } from '../hooks/useFunction';
import { HELP } from './helpText';
import { ChromeIconButton } from './ChromeIconButton';
import {
  ICON_SIZE,
  KNOB_SIZE_PRIMARY,
  KNOB_SIZE_SECONDARY,
  faceplateChromeLift,
} from './theme';
import { IMAGE_GROUP_WIDTH } from './SpreadControls';

/**
 * Offset (stereo chain mode): a purely corrective alignment delay on one
 * chain (see native StereoOffset.h) — e.g. two captures of one performance
 * landing a few ms apart, or NAM models / IRs with different baked-in
 * latency. Knob + power, always visible, dimmed while off — same pattern as
 * the Gate group.
 *
 * The knob is bipolar — center = 0 ms = identity; left of center delays the
 * left chain, right of center the right, up to ±24 ms.
 *
 * The auto (=) button on the opposite edge measures the alignment for you:
 * same one-shot listening flow as auto-balance (see native AutoOffset.h) —
 * click, play ~2 s, and the measured inter-chain lag is written into the
 * offset (powering it on when there's a real correction). It stays live
 * while the group is off, which is the natural starting point for "align
 * this for me".
 *
 * Occupies the same fixed slot as the mono-mode Spread group; the edge
 * buttons flank the knob symmetrically so it stays centered and the plate
 * never shifts (it's laid out with space-between).
 */
/** Plate-shared chrome baseline: the secondary-knob centerline, like every
    other faceplate action button (even those flanking primary knobs). */
const CHROME_LIFT = faceplateChromeLift(KNOB_SIZE_SECONDARY);

/**
 * Auto offset: one-shot chain time alignment. Click arms a listening
 * measurement on the native side — play for ~2 s and the measured lag is
 * written into stereoOffsetTime (the Offset knob visibly moves). Yellow
 * (listening) while armed; click again to cancel; times out after 15 s of
 * silence or an untrustworthy measurement.
 */
const AutoOffsetButton: React.FC = () => {
  const start = useNativeFunction<boolean>('startAutoOffset');
  const cancel = useNativeFunction<boolean>('cancelAutoOffset');
  const poll = useNativeFunction<{ state: string; matchedMs?: number }>('pollAutoOffset');
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
      help={HELP.autoOffset}
      onClick={handleClick}
      offsetY={CHROME_LIFT}
    >
      <Equal size={ICON_SIZE} />
    </ChromeIconButton>
  );
};

export const OffsetGroup: React.FC = () => {
  const [enabled, setEnabled] = useParameter('stereoOffsetEnabled', 'toggle');
  const [offset, setOffset] = useParameter('stereoOffsetTime', 'slider');

  // Only the knob dims while off: the power's tone already reads off, and
  // the auto button stays fully live (measuring powers the offset on).
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'flex-end',
        justifyContent: 'center',
        gap: '10px',
        width: `${IMAGE_GROUP_WIDTH}px`,
        boxSizing: 'border-box',
      }}
    >
      <AutoOffsetButton />
      <div style={{ opacity: enabled ? 1 : 0.55 }}>
        <KnobControl
          label="Offset"
          value={offset}
          onChange={setOffset}
          variant="bipolar"
          size={KNOB_SIZE_PRIMARY}
          labelSize={12}
          scale={offsetMsScale}
          defaultValue={0.5}
          help={HELP.offsetTime}
        />
      </div>
      <ChromeIconButton
        tone="power"
        on={enabled}
        help={HELP.offsetPower}
        onClick={() => setEnabled(!enabled)}
        offsetY={CHROME_LIFT}
      >
        <Power size={ICON_SIZE} />
      </ChromeIconButton>
    </div>
  );
};
