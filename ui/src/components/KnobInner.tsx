import React from 'react';

/**
 * Knob visual variants, all sharing the same ring/handle/gradient language:
 * - full: classic 270° sweep, gradient builds from the bottom-left start up
 *   to the handle.
 * - bipolar: 270° track, handle rests at noon (value 0.5 = zero). The
 *   gradient anchors at noon and grows toward the handle in either
 *   direction (e.g. spread left/right).
 * - panLeft: half track from bottom-left to noon (value 0..0.5 = hard
 *   left..center); gradient anchors at noon, growing toward the handle.
 * - panRight: mirrored half track from noon to bottom-right (value
 *   0.5..1 = center..hard right).
 */
export type KnobVariant = 'full' | 'bipolar' | 'panLeft' | 'panRight';

interface KnobInnerProps {
  value: number; // 0 to 1 (panLeft uses 0..0.5, panRight 0.5..1)
  size: number;
  innerColor?: string;
  variant?: KnobVariant;
}

// Grayscale ramp shared by every variant: dark at the anchor, white at the
// handle. `reversed` flips it for sweeps that start at the handle (left side
// of noon), since conic-gradient can only paint clockwise.
const GRADIENT_STOPS = [
  { pos: 0, color: 'rgba(30, 30, 30, 0.3)' },
  { pos: 0.2, color: 'rgba(50, 50, 50, 0.6)' },
  { pos: 0.4, color: 'rgba(100, 100, 100, 0.75)' },
  { pos: 0.6, color: 'rgba(160, 160, 160, 0.85)' },
  { pos: 0.8, color: 'rgba(220, 220, 220, 0.9)' },
  { pos: 1, color: 'rgba(255, 255, 255, 1)' },
];

const buildGradient = (fromDeg: number, sweepDeg: number, reversed: boolean) => {
  if (sweepDeg <= 0) return 'transparent';
  const stops = reversed
    ? GRADIENT_STOPS.map((s) => `${s.color} ${((1 - s.pos) * sweepDeg).toFixed(2)}deg`).reverse()
    : GRADIENT_STOPS.map((s) => `${s.color} ${(s.pos * sweepDeg).toFixed(2)}deg`);
  return `conic-gradient(from ${fromDeg}deg, ${stops.join(', ')}, transparent ${sweepDeg}deg)`;
};

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

// Empty-track gradient for each variant. The track is the dim arc the user
// can still drag into; its appearance should mirror the active side so the
// knob reads as symmetric rather than having a transparent/black dead zone.
const trackGradient = (variant: KnobVariant): string => {
  // Shared dim stop colors: solid at the start of each arm, fading toward
  // the gap (the dead zone at the bottom of the circle).
  const SOLID = 'rgba(50, 50, 50, 0.8)';
  const FADE = 'rgba(50, 50, 50, 0.1)';
  const NONE = 'transparent';
  switch (variant) {
    case 'bipolar':
      // Two symmetric 135° arms — one clockwise from noon, one counter-clockwise.
      // Each arm is brightest nearest noon and fades toward the bottom gap.
      // Left arm: from noon (0°) going counter-clockwise  →  painted as a
      //   225° clockwise sector with the opacity reversed.
      // Right arm: from noon (0°) going 135° clockwise.
      // We stack two conic-gradient layers via a single 360° cone that covers
      // both arms and is transparent in the bottom 90° gap.
      return `conic-gradient(from -135deg, ${NONE} 0deg, ${FADE} 0deg, ${SOLID} 135deg, ${SOLID} 135deg, ${FADE} 270deg, ${NONE} 270deg)`;
    case 'panLeft':
      // Active arm: hard-left (225°, bottom-left) → noon (360°).
      // Mirror arm: noon (0°) → hard-right (135°), same shape, symmetric.
      return `conic-gradient(from 225deg, ${FADE} 0deg, ${SOLID} 135deg, ${SOLID} 135deg, ${FADE} 270deg, ${NONE} 270deg)`;
    case 'panRight':
      // Active arm: noon (0°) → hard-right (135°).
      // Mirror arm: hard-left (225°) → noon, same shape.
      return `conic-gradient(from 225deg, ${FADE} 0deg, ${SOLID} 135deg, ${SOLID} 135deg, ${FADE} 270deg, ${NONE} 270deg)`;
    default:
      // Classic full 270° sweep: uniform dim track from hard-left to hard-right.
      return `conic-gradient(from 225deg, ${SOLID} 0deg, ${FADE} 270deg, ${NONE} 270deg)`;
  }
};

/** Per-variant geometry: handle angle (deg from noon, -135..135), base track
    extent, and the value gradient (all in conic coords, 0 = noon). */
const geometry = (variant: KnobVariant, value: number) => {
  switch (variant) {
    case 'bipolar': {
      const angleDeg = clamp(value, 0, 1) * 270 - 135;
      return {
        angleDeg,
        trackFromDeg: 225,
        trackSweepDeg: 270,
        gradient:
          angleDeg >= 0
            ? { fromDeg: 0, sweepDeg: angleDeg, reversed: false }
            : { fromDeg: 360 + angleDeg, sweepDeg: -angleDeg, reversed: true },
      };
    }
    case 'panLeft': {
      const angleDeg = (clamp(value, 0, 0.5) / 0.5) * 135 - 135;
      return {
        angleDeg,
        trackFromDeg: 225,
        trackSweepDeg: 135,
        gradient: { fromDeg: 360 + angleDeg, sweepDeg: -angleDeg, reversed: true },
      };
    }
    case 'panRight': {
      const angleDeg = ((clamp(value, 0.5, 1) - 0.5) / 0.5) * 135;
      return {
        angleDeg,
        trackFromDeg: 0,
        trackSweepDeg: 135,
        gradient: { fromDeg: 0, sweepDeg: angleDeg, reversed: false },
      };
    }
    default: {
      const angleDeg = clamp(value, 0, 1) * 270 - 135;
      return {
        angleDeg,
        trackFromDeg: 225,
        trackSweepDeg: 270,
        gradient: { fromDeg: 225, sweepDeg: clamp(value, 0, 1) * 270, reversed: false },
      };
    }
  }
};

// Memoized: pure function of its scalar props, and it repaints two conic
// gradients per render — cheap to skip when a parent re-renders idle knobs.
export const KnobInner: React.FC<KnobInnerProps> = React.memo(function KnobInner({
  value,
  size,
  innerColor = '#000000',
  variant = 'full',
}) {
  const { angleDeg, gradient } = geometry(variant, value);
  // Convert to radians, offset by -90 so 0° is at top
  const angleRad = (angleDeg - 90) * (Math.PI / 180);

  const outerRadius = size / 2;
  const innerRadius = outerRadius * 0.5; // Thinner arc, larger inner circle
  const arcCenterRadius = (outerRadius + innerRadius) / 2;

  // Handle dimensions - positioned in the middle of the arc ring
  const handleWidth = Math.round(size * 0.12);
  const handleHeight = Math.round((outerRadius - innerRadius) * 1.2);

  // Calculate handle offset from center (use transform for smoother animation)
  const handleOffsetX = arcCenterRadius * Math.cos(angleRad);
  const handleOffsetY = arcCenterRadius * Math.sin(angleRad);

  // Round the sweep lightly to reduce paint jitter during drags.
  const sweepDeg = Math.round(gradient.sweepDeg * 10) / 10;

  return (
    <div
      style={{
        width: size,
        height: size,
        position: 'relative',
        borderRadius: '50%',
        pointerEvents: 'none',
      }}
    >
      {/* Base track - the variant's full arc extent (both arms for
          centered variants so neither side is transparent/black) */}
      <div
        style={{
          position: 'absolute',
          width: '100%',
          height: '100%',
          borderRadius: '50%',
          background: trackGradient(variant),
        }}
      />

      {/* Gradient overlay - from the variant's anchor to the handle */}
      <div
        style={{
          position: 'absolute',
          width: '100%',
          height: '100%',
          borderRadius: '50%',
          background: buildGradient(gradient.fromDeg, sweepDeg, gradient.reversed),
          willChange: 'background',
          backfaceVisibility: 'hidden',
        }}
      />

      {/* Inner dark circle - cuts out the center to create the ring */}
      <div
        style={{
          position: 'absolute',
          top: '50%',
          left: '50%',
          transform: 'translate(-50%, -50%)',
          width: innerRadius * 2,
          height: innerRadius * 2,
          borderRadius: '50%',
          backgroundColor: innerColor,
        }}
      />

      {/* Handle/indicator - uses transform for smoother animation */}
      <div
        style={{
          position: 'absolute',
          left: '50%',
          top: '50%',
          width: handleWidth,
          height: handleHeight,
          backgroundColor: '#ffffff',
          borderLeft: '2px solid #000000',
          transform: `translate(-50%, -50%) translate(${handleOffsetX}px, ${handleOffsetY}px) rotate(${angleDeg}deg)`,
          transformOrigin: 'center center',
          willChange: 'transform',
          backfaceVisibility: 'hidden',
        }}
      />
    </div>
  );
});
