import { useId } from 'react';

type SecondaryKnobThumbProps = {
  angleDeg: number;
};

// This one is trickier than the primary knob: the indicator itself (sRing)
// carries its own top-lit gradient, not a flat fill. If we just rotated the
// whole indicator group, that highlight would swing around with it and read
// as "wrong" -- like a marble catching light from a moving sun.
//
// Fix: apply an equal and opposite gradientTransform to the ring's own
// gradient, `rotate(-angleDeg)` around its own center. The ring rotates
// into position, but its internal gradient rotates back by the same amount,
// so the highlight stays pinned to true "up" no matter where the pointer
// ends up. gradientTransform lives in objectBoundingBox space by default, so
// the rotation pivot is (0.5, 0.5), not the SVG's (100, 100).
export function SecondaryKnobThumb({ angleDeg }: SecondaryKnobThumbProps) {
  const uid = useId().replace(/[:]/g, '');
  const bandId = `sBand-${uid}`;
  const bodyId = `sBody-${uid}`;
  const ringId = `sRing-${uid}`;

  return (
    // viewBox cropped tight to the outer circle (center 100,100, r 79.07) so
    // the rendered knob fills the full component size instead of ~79% of it.
    <svg viewBox="20.93 20.93 158.14 158.14" width="100%" height="100%" style={{ display: 'block' }}>
      <defs>
        <linearGradient id={bandId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#161616" />
          <stop offset="26%" stopColor="#161616" />
          <stop offset="37.4%" stopColor="#4f4f4f" />
          <stop offset="50%" stopColor="#8d8d8d" />
          <stop offset="62.6%" stopColor="#dcdcdc" />
          <stop offset="74.4%" stopColor="#fafafa" />
          <stop offset="84.5%" stopColor="#ffffff" />
          <stop offset="100%" stopColor="#ffffff" />
        </linearGradient>
        <linearGradient id={bodyId} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#878787" />
          <stop offset="24%" stopColor="#878787" />
          <stop offset="37%" stopColor="#9a9a9a" />
          <stop offset="50%" stopColor="#b9b9b9" />
          <stop offset="65%" stopColor="#e1e1e1" />
          <stop offset="81%" stopColor="#ffffff" />
          <stop offset="100%" stopColor="#ffffff" />
        </linearGradient>
        <linearGradient
          id={ringId}
          x1="0"
          y1="0"
          x2="0"
          y2="1"
          gradientTransform={`rotate(${-angleDeg} 0.5 0.5)`}
        >
          <stop offset="0%" stopColor="#f7f7f7" />
          <stop offset="7%" stopColor="#f7f7f7" />
          <stop offset="12.6%" stopColor="#e5e5e5" />
          <stop offset="19.4%" stopColor="#cccccc" />
          <stop offset="28.2%" stopColor="#adadad" />
          <stop offset="38.7%" stopColor="#898989" />
          <stop offset="50%" stopColor="#676767" />
          <stop offset="61.2%" stopColor="#464646" />
          <stop offset="71.5%" stopColor="#242424" />
          <stop offset="80.5%" stopColor="#161616" />
          <stop offset="100%" stopColor="#161616" />
        </linearGradient>
      </defs>

      {/* Static base */}
      <circle cx="100" cy="100" r="79.07" fill={`url(#${bandId})`} />
      <circle cx="100" cy="100" r="75.40" fill={`url(#${bodyId})`} />

      {/* Rotating indicator group -- the original r=13.63 footprint scaled up
          25%, nudged inward so its outer edge keeps the original clearance
          from the knob rim. The ring keeps its lit look via the
          counter-rotated gradient above; the center dot is flat, so it
          just rotates along for free. */}
      <g style={{ transform: `rotate(${angleDeg}deg)`, transformOrigin: '100px 100px' }}>
        <circle cx="100" cy="50.08" r="17.04" fill={`url(#${ringId})`} />
        <circle cx="100" cy="50.08" r="12.5" fill="#1c1c1e" />
      </g>
    </svg>
  );
}
