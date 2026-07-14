import type { CSSProperties } from 'react';

/**
 * Shared theme tokens. The palette is deliberately tiny: black surfaces,
 * white/gray chrome, and the brand blue→yellow→red ramp reserved for audio
 * visuals (meters, spectrum, tuner). Components import these instead of
 * re-declaring the same rgba literals.
 */

/** Primary muted text/icon color. */
export const MUTED = 'rgba(235, 235, 245, 0.60)';
/** Secondary labels (axis marks, fine print). */
export const SUBTLE = 'rgba(235, 235, 245, 0.40)';
/** Disabled/idle icon gray. */
export const GRAY = '#8D8D93';
/** Pressed/active fill behind white icons and segmented buttons. */
export const HIGHLIGHT = 'rgba(235, 235, 245, 0.18)';
/** Hairline used by every card/section/segment border. */
export const BORDER = '1px solid rgba(84, 84, 88, 0.65)';
/** Card body background. */
export const SURFACE = '#151517';
/** Raised chrome (card headers, faceplate, pills). */
export const SURFACE_RAISED = '#1C1C1E';

/** Base style for a small square icon button (header/tile quick actions). */
export const iconButtonStyle = (size = 24): CSSProperties => ({
  background: 'transparent',
  border: 'none',
  outline: 'none',
  color: MUTED,
  cursor: 'pointer',
  width: `${size}px`,
  height: `${size}px`,
  borderRadius: '6px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  padding: 0,
  flexShrink: 0,
});
