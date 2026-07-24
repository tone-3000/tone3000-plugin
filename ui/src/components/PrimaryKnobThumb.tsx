import { useId } from 'react';

type PrimaryKnobThumbProps = {
  angleDeg: number;
};

// Lerp between two "#rrggbb" colors, t in [0,1].
function lerpColor(a: string, b: string, t: number): string {
  const pa = [1, 3, 5].map((i) => parseInt(a.slice(i, i + 2), 16));
  const pb = [1, 3, 5].map((i) => parseInt(b.slice(i, i + 2), 16));
  const rgb = pa.map((v, i) => Math.round(v + (pb[i] - v) * t));
  return `#${rgb.map((v) => v.toString(16).padStart(2, '0')).join('')}`;
}

// At rest the ring must be indistinguishable from the dot -- same color --
// so the whole indicator reads as one solid circle, the way the original
// artwork did. RING_FLAT matches the dot fill exactly for that reason.
//
// The lit stops stay dark for most of the ring and only brighten sharply
// in the last ~15% -- a tight crescent right at the bottom edge, not a
// broad glow across the lower half. That's what makes it read as a thin
// highlight catching the light rather than a lit ring.
const RING_FLAT = '#1c1c1e';
const RING_LIT_STOPS: Array<[number, string]> = [
  [0, '#1c1c1e'],
  [40, '#1c1c1e'],
  [60, '#242424'],
  [72, '#4a4a4a'],
  [82, '#7a7a7a'],
  [90, '#c0c0c0'],
  [100, '#ffffff'],
];

export function PrimaryKnobThumb({ angleDeg }: PrimaryKnobThumbProps) {
  const uid = useId().replace(/[:]/g, '');
  const outerBandId = `pOuterBand-${uid}`;
  const innerBandId = `pInnerBand-${uid}`;
  const bodyId = `pBody-${uid}`;
  const ringId = `pRing-${uid}`;

  // 0 at top-of-travel (angleDeg = 0), rising toward either end (+/-135deg).
  const travel = Math.min(1, Math.abs(angleDeg) / 135);

  return (
    // viewBox cropped tight to the outer circle (center 100,100, r 84.27) so
    // the rendered knob fills the full component size instead of ~84% of it.
    <svg viewBox="15.73 15.73 168.54 168.54" width="100%" height="100%" style={{ display: 'block' }}>
      <defs>
        <linearGradient id={outerBandId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#050505" />
          <stop offset="26%" stopColor="#060606" />
          <stop offset="37.5%" stopColor="#0e0e0e" />
          <stop offset="50%" stopColor="#161616" />
          <stop offset="62.5%" stopColor="#212121" />
          <stop offset="67.3%" stopColor="#3a3a3a" />
          <stop offset="71.9%" stopColor="#565656" />
          <stop offset="74.1%" stopColor="#676767" />
          <stop offset="78.4%" stopColor="#7c7c7c" />
          <stop offset="84.1%" stopColor="#9f9f9f" />
          <stop offset="87.6%" stopColor="#b8b8b8" />
          <stop offset="90.3%" stopColor="#cdcdcd" />
          <stop offset="92.3%" stopColor="#dedede" />
          <stop offset="93.9%" stopColor="#e9e9e9" />
          <stop offset="95.5%" stopColor="#ebebeb" />
          <stop offset="96.4%" stopColor="#f0f0f0" />
          <stop offset="100%" stopColor="#f0f0f0" />
        </linearGradient>
        <linearGradient id={innerBandId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#292929" />
          <stop offset="8%" stopColor="#262626" />
          <stop offset="16%" stopColor="#232323" />
          <stop offset="26%" stopColor="#2a2a2a" />
          <stop offset="37.5%" stopColor="#474747" />
          <stop offset="50%" stopColor="#515151" />
          <stop offset="62.5%" stopColor="#4b4b4b" />
          <stop offset="67.3%" stopColor="#4d4d4d" />
          <stop offset="70%" stopColor="#5c5c5c" />
          <stop offset="74.1%" stopColor="#6f6f6f" />
          <stop offset="78.3%" stopColor="#848484" />
          <stop offset="84.1%" stopColor="#999999" />
          <stop offset="92%" stopColor="#c2c2c2" />
          <stop offset="95.5%" stopColor="#e0e0e0" />
          <stop offset="97%" stopColor="#eeeeee" />
          <stop offset="100%" stopColor="#eeeeee" />
        </linearGradient>
        <linearGradient id={bodyId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#fafafa" />
          <stop offset="25%" stopColor="#bbbbbb" />
          <stop offset="50%" stopColor="#6a6a6a" />
          <stop offset="75%" stopColor="#262626" />
          <stop offset="88%" stopColor="#0a0a0a" />
          <stop offset="100%" stopColor="#000000" />
        </linearGradient>
        <linearGradient
          id={ringId}
          x1="0"
          y1="0"
          x2="0"
          y2="1"
          gradientTransform={`rotate(${-angleDeg} 0.5 0.5)`}
        >
          {RING_LIT_STOPS.map(([offset, litColor]) => (
            <stop key={offset} offset={`${offset}%`} stopColor={lerpColor(RING_FLAT, litColor, travel)} />
          ))}
        </linearGradient>
      </defs>

      {/* Static base -- never rotates, lighting stays put */}
      <circle cx="100" cy="100" r="84.27" fill={`url(#${outerBandId})`} />
      <circle cx="100" cy="100" r="78.20" fill={`url(#${innerBandId})`} />
      <circle cx="100" cy="100" r="72.27" fill={`url(#${bodyId})`} />

      {/* Rotating indicator -- the original r=11 dot footprint scaled up 10%
          for legibility at plugin knob sizes, nudged inward so its outer edge
          keeps the original clearance from the knob rim. Ring carries the
          lighting (flat at rest, separating into a bottom-edge highlight with
          travel), dot stays flat on top. */}
      <g style={{ transform: `rotate(${angleDeg}deg)`, transformOrigin: '100px 100px' }}>
        <circle cx="100" cy="50.4" r="12.1" fill={`url(#${ringId})`} />
        <circle cx="100" cy="50.4" r="8.8" fill="#1c1c1e" />
      </g>
    </svg>
  );
}
