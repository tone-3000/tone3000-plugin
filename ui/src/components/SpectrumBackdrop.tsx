import React, { useMemo } from 'react';
import { useBlockSpectrum, SPECTRUM_MIN_DB } from '../hooks/useBlockSpectrum';
import { GRAPH_W, GRAPH_H, clamp } from './eqShared';

/**
 * Spectrum backdrop for the EQ editor — isolated so its ~30 fps polling
 * re-renders only this leaf, never the editor around it. Filled with the
 * brand meter ramp (blue at the bottom → yellow → red at the top), so louder
 * content climbs into the red just like the meters. Render inside an SVG
 * sized to the shared EQ graph coordinate space.
 */
export const SpectrumBackdrop: React.FC<{ blockId: string }> = ({ blockId }) => {
  const bins = useBlockSpectrum(blockId);
  const areaPath = useMemo(() => {
    if (bins.length < 2) return '';
    // Display window: -80..0 dB across the graph height.
    const topDb = 0;
    const bottomDb = Math.max(SPECTRUM_MIN_DB, -80);
    const points = bins.map((db, i) => {
      const x = (i / (bins.length - 1)) * GRAPH_W;
      const t = clamp((db - bottomDb) / (topDb - bottomDb), 0, 1);
      return `${x.toFixed(1)} ${(GRAPH_H * (1 - t)).toFixed(1)}`;
    });
    // Filled area only (no outline stroke): at idle every bin sits on the
    // floor, so a stroked curve would draw a hairline across the bottom.
    return `M0 ${GRAPH_H} L${points.join(' L')} L${GRAPH_W} ${GRAPH_H} Z`;
  }, [bins]);

  if (!areaPath) return null;
  const gradientId = `eq-spectrum-${blockId}`;
  return (
    <>
      <defs>
        <linearGradient
          id={gradientId}
          gradientUnits="userSpaceOnUse"
          x1="0"
          y1={GRAPH_H}
          x2="0"
          y2="0"
        >
          {/* Yellow/red pulled down so hot content reads red well before 0 dBFS */}
          <stop offset="0%" stopColor="#0000FF" />
          <stop offset="40%" stopColor="#FFFF00" />
          <stop offset="70%" stopColor="#FF0000" />
          <stop offset="100%" stopColor="#FF0000" />
        </linearGradient>
      </defs>
      <path d={areaPath} fill={`url(#${gradientId})`} fillOpacity={0.3} />
    </>
  );
};
