import React from 'react';
import { PrimaryKnobThumb } from './PrimaryKnobThumb';
import { SecondaryKnobThumb } from './SecondaryKnobThumb';
import { PanKnobThumb } from './PanKnobThumb';

/**
 * Knob geometry variants. The visual is the same hardware-style knob; the
 * variant only changes how a value maps to the indicator angle:
 * - full: classic 270° sweep, value 0..1 = hard-left..hard-right.
 * - bipolar: same sweep, but value 0.5 = noon means "zero/off" (the center
 *   snap lives in KnobControl).
 * - panLeft: half track, value 0..0.5 = hard left..center (noon).
 * - panRight: mirrored half track, value 0.5..1 = center..hard right.
 */
export type KnobVariant = 'full' | 'bipolar' | 'panLeft' | 'panRight';

/** Visual style: primary = large dark knob, secondary = small light knob,
    pan = black body in a ring track that fills from noon toward the handle. */
export type KnobThumb = 'primary' | 'secondary' | 'pan';

interface KnobInnerProps {
  value: number; // 0 to 1 (panLeft uses 0..0.5, panRight 0.5..1)
  size: number;
  variant?: KnobVariant;
  thumb?: KnobThumb;
}

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

/** Indicator angle in degrees from noon (-135..+135) for a given variant. */
const angleFor = (variant: KnobVariant, value: number): number => {
  switch (variant) {
    case 'panLeft':
      return (clamp(value, 0, 0.5) / 0.5) * 135 - 135;
    case 'panRight':
      return ((clamp(value, 0.5, 1) - 0.5) / 0.5) * 135;
    default:
      // full + bipolar share the mapping; bipolar just rests at noon (0.5).
      return clamp(value, 0, 1) * 270 - 135;
  }
};

/** Where the pan thumb's fill arc anchors: centered variants fill outward
    from noon, a full sweep fills up from hard-left. */
const fillAnchorFor = (variant: KnobVariant): number => (variant === 'full' ? -135 : 0);

// Memoized: pure function of scalar props — cheap to skip when a parent
// re-renders idle knobs.
export const KnobInner: React.FC<KnobInnerProps> = React.memo(function KnobInner({
  value,
  size,
  variant = 'full',
  thumb = 'primary',
}) {
  const angleDeg = angleFor(variant, value);
  return (
    <div
      style={{
        width: size,
        height: size,
        position: 'relative',
        pointerEvents: 'none',
      }}
    >
      {thumb === 'primary' ? (
        <PrimaryKnobThumb angleDeg={angleDeg} />
      ) : thumb === 'secondary' ? (
        <SecondaryKnobThumb angleDeg={angleDeg} />
      ) : (
        <PanKnobThumb angleDeg={angleDeg} anchorDeg={fillAnchorFor(variant)} />
      )}
    </div>
  );
});
