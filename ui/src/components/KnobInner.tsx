import React from 'react';
import { KnobFace } from './KnobFace';
import type { KnobTone } from './KnobFace';

/**
 * Knob geometry variants. The artwork is the same hardware-style knob in
 * every case; the variant only changes how a value maps to the indicator
 * angle, and where the value arc grows from:
 * - full: classic 270° sweep, value 0..1 = hard-left..hard-right. Zero is the
 *   start of travel, so the arc grows from bottom left.
 * - bipolar: same sweep, but value 0.5 = noon means "zero/off" (the center
 *   snap lives in KnobControl), so the arc grows out of noon either way.
 * - panLeft: half track, value 0..0.5 = hard left..center (noon).
 * - panRight: mirrored half track, value 0.5..1 = center..hard right.
 * Both pan halves read zero at noon like bipolar does, they just stay on
 * their own side of it.
 */
export type KnobVariant = 'full' | 'bipolar' | 'panLeft' | 'panRight';

/** Visual tone: primary = a section's headline knob, secondary = its darker,
    smaller companion trims. Purely cosmetic; either tone can be bipolar or
    plain, that's the variant's job. */
export type KnobThumb = KnobTone;

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

/** Angle the value arc grows from: every centered variant reads zero at noon,
    a plain knob reads it at the bottom-left start of travel. */
const arcFromFor = (variant: KnobVariant): number => (variant === 'full' ? -135 : 0);

// Memoized: pure function of scalar props, cheap to skip when a parent
// re-renders idle knobs.
export const KnobInner: React.FC<KnobInnerProps> = React.memo(function KnobInner({
  value,
  size,
  variant = 'full',
  thumb = 'primary',
}) {
  return (
    <div
      style={{
        width: size,
        height: size,
        position: 'relative',
        pointerEvents: 'none',
      }}
    >
      <KnobFace angleDeg={angleFor(variant, value)} arcFromDeg={arcFromFor(variant)} tone={thumb} />
    </div>
  );
});
