/** Shared meter scale + color ramp for DbMeter and BlockMeter. */

export const METER_MIN_DB = -60;
/** Top of the scale = 0 dBFS: the last dot lights exactly at clipping. */
export const METER_MAX_DB = 0;

/** Interpolate color along gradient: blue (bottom) → yellow (middle) → red (top). */
export const getGradientColor = (position: number): string => {
  // position: 0 = bottom (blue), 0.5 = middle (yellow), 1 = top (red)
  let r: number, g: number, b: number;

  if (position <= 0.5) {
    // Bottom half: blue #0000FF → yellow #FFFF00
    const t = position * 2;
    r = Math.round(255 * t);
    g = Math.round(255 * t);
    b = Math.round(255 * (1 - t));
  } else {
    // Top half: yellow #FFFF00 → red #FF0000
    const t = (position - 0.5) * 2;
    r = 255;
    g = Math.round(255 * (1 - t));
    b = 0;
  }

  return `rgb(${r}, ${g}, ${b})`;
};
