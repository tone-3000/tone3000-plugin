import { useId } from 'react';

type PanKnobThumbProps = {
  /** Handle angle in degrees from noon, -135..+135. */
  angleDeg: number;
  /** Where the fill arc anchors (deg from noon). Pan/bipolar knobs fill from
      noon toward the handle; a full-sweep knob would pass -135. */
  anchorDeg?: number;
};

/**
 * Third knob style, traced from the reference render: a solid black body
 * inside a thin gray ring track. The value paints a white arc along the ring
 * from the anchor to the handle. The lit arc uses the same stroke width as
 * the grey track; a soft outer white glow (Figma-style box-shadow) bleeds
 * past the ring without thickening the solid stroke. A plain white dot
 * marks the handle on the body.
 *
 * All coordinates come from sampling the 144px reference PNG (center 72,72):
 * body r=57, ring centerline r=62 / width 10 (#5f5f5f), dot r=8.5 at
 * distance 36 from center. The viewBox hugs the ring's outer edge (r=67) so
 * the track spans the full component size; overflow stays visible so the
 * glow can bleed past it like in the reference.
 */
const CX = 72;
const CY = 72;
const BODY_R = 57;
const RING_R = 62;
const RING_W = 10;
const RING_OUTER = RING_R + RING_W / 2; // 67
const DOT_DIST = 36;
const DOT_R = 8.5;
const TRACK_COLOR = '#5f5f5f';

/** Point on the ring centerline at `deg` from noon (clockwise positive). */
const polar = (deg: number, r: number): [number, number] => {
  const rad = (deg * Math.PI) / 180;
  return [CX + r * Math.sin(rad), CY - r * Math.cos(rad)];
};

const arcPath = (fromDeg: number, toDeg: number): string => {
  const [x0, y0] = polar(fromDeg, RING_R);
  const [x1, y1] = polar(toDeg, RING_R);
  const largeArc = Math.abs(toDeg - fromDeg) > 180 ? 1 : 0;
  const sweep = toDeg >= fromDeg ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${RING_R} ${RING_R} 0 ${largeArc} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
};

export function PanKnobThumb({ angleDeg, anchorDeg = 0 }: PanKnobThumbProps) {
  const uid = useId().replace(/[:]/g, '');
  const glowId = `panGlow-${uid}`;

  const hasFill = Math.abs(angleDeg - anchorDeg) > 0.5;
  const fill = hasFill ? arcPath(anchorDeg, angleDeg) : null;

  return (
    <svg
      viewBox={`${CX - RING_OUTER} ${CY - RING_OUTER} ${RING_OUTER * 2} ${RING_OUTER * 2}`}
      width="100%"
      height="100%"
      style={{ display: 'block', overflow: 'visible' }}
    >
      <defs>
        {/* Outer white glow only — SourceGraphic stays the sharp RING_W
            stroke so the lit arc matches the grey track exactly (Figma's
            soft box-shadow, not a thickened blur). */}
        <filter id={glowId} x="-120%" y="-120%" width="340%" height="340%" colorInterpolationFilters="sRGB">
          <feGaussianBlur in="SourceAlpha" stdDeviation="5.5" result="blur" />
          <feFlood floodColor="#ffffff" floodOpacity="0.42" result="glowColor" />
          <feComposite in="glowColor" in2="blur" operator="in" result="glow" />
          <feMerge>
            <feMergeNode in="glow" />
            <feMergeNode in="SourceGraphic" />
          </feMerge>
        </filter>
      </defs>

      {/* Static base: body + full-circle track */}
      <circle cx={CX} cy={CY} r={BODY_R} fill="#000000" />
      <circle cx={CX} cy={CY} r={RING_R} fill="none" stroke={TRACK_COLOR} strokeWidth={RING_W} />

      {/* Value fill: same stroke as the track, with a soft outer glow */}
      {fill && (
        <path
          d={fill}
          fill="none"
          stroke="#ffffff"
          strokeWidth={RING_W}
          strokeLinecap="round"
          filter={`url(#${glowId})`}
        />
      )}

      {/* Handle dot */}
      <g style={{ transform: `rotate(${angleDeg}deg)`, transformOrigin: `${CX}px ${CY}px` }}>
        <circle cx={CX} cy={CY - DOT_DIST} r={DOT_R} fill="#ffffff" />
      </g>
    </svg>
  );
}
