import { useId } from 'react';
import { BRAND_YELLOW } from './theme';

/**
 * The knob artwork, shared by both tones. Source of truth for the geometry
 * is design/primary-knob.svg and design/secondary-knob.svg. The two exports
 * are the same hardware knob at different sizes, so every radius here is
 * their coordinates normalized to this 200x200 viewBox. Only the three face
 * layers differ between them; keep TONE_FACE_FILLS in sync with the exports.
 */

export type KnobTone = 'primary' | 'secondary';

type KnobFaceProps = {
  /** Pointer angle in degrees clockwise from noon, -135..+135. */
  angleDeg: number;
  /** Angle the value arc grows from: noon for centered knobs, -135
      (bottom left, start of travel) for the rest. */
  arcFromDeg: number;
  tone: KnobTone;
};

const CENTER = 100;
/** Outer rim, then the black channel the value arc runs in. */
const RIM_RADIUS = 100;
const CHANNEL_RADIUS = 98.4;
/** Value arc: centerline radius and stroke, sized to fill the channel. */
const ARC_RADIUS = 91.56;
const ARC_WIDTH = 10.12;
/** Drop shadow the face stack sits on. */
const SHADOW_RADIUS = 86.4;
/** Face stack, outermost first. Both tones share these radii. */
const FACE_RADII = [84.9, 82.75, 79.05];
/** Pointer: orbit radius of its center, then its two circles. */
const POINTER_ORBIT = 51;
const POINTER_RADIUS = 14.4;
const POINTER_HOLE_RADIUS = 10.25;

/** A face layer is either a flat color or a top-to-bottom two-stop ramp. */
type FaceFill = string | readonly [top: string, bottom: string];

const TONE_FACE_FILLS: Record<KnobTone, readonly FaceFill[]> = {
  primary: [
    ['#a8a8a8', '#3d3d3d'],
    ['#a9a9a9', '#242424'],
    ['#979797', '#232323'],
  ],
  secondary: ['#a3a3a3', ['#a9a9a9', '#000000'], ['#505050', '#000000']],
};

/** Point on the arc's circle at `thetaDeg` clockwise from noon, the same angle
    convention as the pointer's rotation, so the arc endpoint always lines up
    with wherever the pointer actually is. */
function pointOnArc(thetaDeg: number) {
  const rad = (thetaDeg * Math.PI) / 180;
  return {
    x: CENTER + ARC_RADIUS * Math.sin(rad),
    y: CENTER - ARC_RADIUS * Math.cos(rad),
  };
}

/**
 * Value arc from the knob's zero reference out to the pointer. Endpoints are
 * ordered ascending so the sweep flag can stay 1 (clockwise) whichever side
 * of zero the pointer is on, which is what lets one path serve both a
 * centered knob (zero at noon, fills either way) and a plain one (zero at
 * bottom left, fills one way).
 */
function valueArcPath(fromDeg: number, toDeg: number): string {
  const start = Math.min(fromDeg, toDeg);
  const end = Math.max(fromDeg, toDeg);
  if (end - start < 0.25) return ''; // sitting on zero, no arc to draw
  const p1 = pointOnArc(start);
  const p2 = pointOnArc(end);
  const largeArc = end - start > 180 ? 1 : 0;
  return `M ${p1.x.toFixed(2)} ${p1.y.toFixed(2)} A ${ARC_RADIUS} ${ARC_RADIUS} 0 ${largeArc} 1 ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`;
}

/**
 * The pointer's bevel gradient has directional light baked in: dark top,
 * light bottom, the "chamfered edge catches light from below" convention the
 * whole faceplate uses. Since the pointer is the one thing that rotates, its
 * gradient is counter-rotated by the same angle around its own center, so the
 * bright edge keeps facing down no matter where the pointer has turned to.
 * gradientTransform is in objectBoundingBox space, hence the (0.5, 0.5) pivot
 * rather than the viewBox's (100, 100).
 */
export function KnobFace({ angleDeg, arcFromDeg, tone }: KnobFaceProps) {
  // useId's raw output carries framework punctuation (React 19 hands back
  // «R0»), which has no business inside an id that a url(#...) has to resolve.
  const uid = useId().replace(/[^a-zA-Z0-9]/g, '');
  const bevelId = `knobBevel-${uid}`;
  const faceId = (layer: number) => `knobFace${layer}-${uid}`;
  const faceFills = TONE_FACE_FILLS[tone];

  return (
    <svg viewBox="0 0 200 200" width="100%" height="100%" style={{ display: 'block' }}>
      <defs>
        {faceFills.map((fill, layer) =>
          typeof fill === 'string' ? null : (
            <linearGradient key={layer} id={faceId(layer)} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor={fill[0]} />
              <stop offset="100%" stopColor={fill[1]} />
            </linearGradient>
          )
        )}
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
      <circle cx={CENTER} cy={CENTER} r={RIM_RADIUS} fill="#1d1d1d" />
      <circle cx={CENTER} cy={CENTER} r={CHANNEL_RADIUS} fill="#000000" />

      {/* Dynamic value arc -- redrawn each render from the zero reference out
          to the pointer's current position */}
      <path
        d={valueArcPath(arcFromDeg, angleDeg)}
        fill="none"
        stroke={BRAND_YELLOW}
        strokeWidth={ARC_WIDTH}
      />

      {/* Face stack: shadow ring, then the tone's three layers */}
      <circle cx={CENTER} cy={CENTER} r={SHADOW_RADIUS} fill="#1d1d1d" />
      {faceFills.map((fill, layer) => (
        <circle
          key={layer}
          cx={CENTER}
          cy={CENTER}
          r={FACE_RADII[layer]}
          fill={typeof fill === 'string' ? fill : `url(#${faceId(layer)})`}
        />
      ))}

      {/* Rotating pointer -- bevel gradient counter-rotated to stay put */}
      <g
        style={{ transform: `rotate(${angleDeg}deg)`, transformOrigin: `${CENTER}px ${CENTER}px` }}
      >
        <circle
          cx={CENTER}
          cy={CENTER - POINTER_ORBIT}
          r={POINTER_RADIUS}
          fill={`url(#${bevelId})`}
        />
        <circle cx={CENTER} cy={CENTER - POINTER_ORBIT} r={POINTER_HOLE_RADIUS} fill="#000000" />
      </g>
    </svg>
  );
}
