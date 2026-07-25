import { useId } from 'react';
import { BRAND_YELLOW } from './theme';

type BipolarKnobThumbProps = {
  /** Handle angle in degrees from noon, -135..+135. Noon = zero. */
  angleDeg: number;
};

const ARC_RADIUS = 91.56;
const CENTER = 100;

// Point on the arc's circle at `thetaDeg` degrees clockwise from 12 o'clock
// -- same angle convention as the rotation transform below, so the arc
// endpoint always lines up with wherever the pointer actually is.
function pointOnArc(thetaDeg: number) {
  const rad = (thetaDeg * Math.PI) / 180;
  return {
    x: CENTER + ARC_RADIUS * Math.sin(rad),
    y: CENTER - ARC_RADIUS * Math.cos(rad),
  };
}

// Bipolar value arc fills from the 12 o'clock "zero" reference out to
// wherever the pointer currently sits, on whichever side. Sweep-flag stays
// 1 (clockwise) in both directions because the two endpoints are always
// ordered start=min(0, angle), end=max(0, angle).
function valueArcPath(angleDeg: number): string {
  if (Math.abs(angleDeg) < 0.25) return ''; // no deviation from center, no arc
  const thetaStart = Math.min(0, angleDeg);
  const thetaEnd = Math.max(0, angleDeg);
  const p1 = pointOnArc(thetaStart);
  const p2 = pointOnArc(thetaEnd);
  const largeArc = thetaEnd - thetaStart > 180 ? 1 : 0;
  return `M ${p1.x.toFixed(2)} ${p1.y.toFixed(2)} A ${ARC_RADIUS} ${ARC_RADIUS} 0 ${largeArc} 1 ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`;
}

// This knob's bevel-style pointer (bkBevel) already carries real directional
// light baked in -- dark top, light bottom, same "chamfered edge catches
// light from below" convention as the outer bezel rings on the other two
// knobs. Since it's the one thing that rotates, its gradient gets the same
// counter-rotation treatment as before: rotate(-angleDeg) around its own
// center, so the bright edge always faces the same fixed direction (down)
// no matter which way the pointer has turned.
export function BipolarKnobThumb({ angleDeg }: BipolarKnobThumbProps) {
  const uid = useId().replace(/[:]/g, '');
  const faceId = `bkFace-${uid}`;
  const innerRingId = `bkInnerRing-${uid}`;
  const bevelId = `bkBevel-${uid}`;

  return (
    <svg viewBox="0 0 200 200" width="100%" height="100%" style={{ display: 'block' }}>
      <defs>
        <linearGradient id={faceId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#505050" />
          <stop offset="24.6%" stopColor="#4f4f4f" />
          <stop offset="31%" stopColor="#494949" />
          <stop offset="37.3%" stopColor="#414141" />
          <stop offset="43.7%" stopColor="#373737" />
          <stop offset="50%" stopColor="#2b2b2b" />
          <stop offset="56.3%" stopColor="#202020" />
          <stop offset="62.7%" stopColor="#161616" />
          <stop offset="69%" stopColor="#0b0b0b" />
          <stop offset="75.4%" stopColor="#040404" />
          <stop offset="81.7%" stopColor="#000000" />
          <stop offset="100%" stopColor="#000000" />
        </linearGradient>
        <linearGradient id={innerRingId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#a9a9a9" />
          <stop offset="25.9%" stopColor="#a9a9a9" />
          <stop offset="37.5%" stopColor="#7a7a7a" />
          <stop offset="50%" stopColor="#4c4c4c" />
          <stop offset="62.5%" stopColor="#161616" />
          <stop offset="74.1%" stopColor="#000000" />
          <stop offset="100%" stopColor="#000000" />
        </linearGradient>
        <linearGradient
          id={bevelId}
          x1="0"
          y1="0"
          x2="0"
          y2="1"
          gradientTransform={`rotate(${-angleDeg} 0.5 0.5)`}
        >
          <stop offset="0%" stopColor="#040404" />
          <stop offset="20%" stopColor="#1b1b1b" />
          <stop offset="50%" stopColor="#626262" />
          <stop offset="78%" stopColor="#a9a9a9" />
          <stop offset="100%" stopColor="#a9a9a9" />
        </linearGradient>
      </defs>

      {/* Static base -- never rotates, lighting stays put */}
      <circle cx="100" cy="100" r="100" fill="#1d1d1d" />
      <circle cx="100" cy="100" r="98.4" fill="#000000" />

      {/* Dynamic value arc -- redrawn each render from the fixed 12 o'clock
          reference out to the pointer's current position */}
      <path d={valueArcPath(angleDeg)} fill="none" stroke={BRAND_YELLOW} strokeWidth="10.12" />

      <circle cx="100" cy="100" r="86.4" fill="#1d1d1d" />
      <circle cx="100" cy="100" r="84.9" fill="#a3a3a3" />
      <circle cx="100" cy="100" r="82.75" fill={`url(#${innerRingId})`} />
      <circle cx="100" cy="100" r="79.05" fill={`url(#${faceId})`} />

      {/* Rotating pointer -- bevel gradient counter-rotated to stay put */}
      <g style={{ transform: `rotate(${angleDeg}deg)`, transformOrigin: '100px 100px' }}>
        <circle cx="100" cy="49" r="14.4" fill={`url(#${bevelId})`} />
        <circle cx="100" cy="49" r="10.25" fill="#000000" />
      </g>
    </svg>
  );
}
