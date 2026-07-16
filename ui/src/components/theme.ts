import type { CSSProperties } from 'react';

/**
 * Shared theme tokens. The palette is deliberately tiny: black surfaces,
 * white/gray chrome, and the brand blue→yellow→red ramp reserved for audio
 * visuals (meters, spectrum, tuner). Components import these instead of
 * re-declaring the same rgba literals.
 *
 * Icon system (Lucide wherever the lib has the glyph; custom SVGs use
 * `currentColor`, round caps, and a stroke weight that visually matches
 * Lucide at the rendered size):
 * - Sizes by hierarchy: top bar 18, card header / tile quick actions 14,
 *   faceplate + inline pills 12.
 * - Interactive icons are white; GRAY only for off/disabled states. No
 *   accent colors for state — grayscale only.
 * - State patterns:
 *   1. Power (on/off): on = white icon, no fill; off = GRAY icon + HIGHLIGHT.
 *   2. Panel/view toggle (shown/hidden): shown = HIGHLIGHT fill + white;
 *      hidden = transparent + MUTED.
 *   3. Engaged/armed (PRE, auto-balance, active EQ): ACTIVE_OUTLINE +
 *      HIGHLIGHT + white; idle = BORDER + transparent + GRAY.
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
/** Bright outline marking an engaged/armed control (PRE, auto-balance…). */
export const ACTIVE_OUTLINE = '1px solid rgba(255, 255, 255, 0.85)';
/** Card body background. */
export const SURFACE = '#151517';
/** Raised chrome (card headers, faceplate, pills). */
export const SURFACE_RAISED = '#1C1C1E';

/**
 * Pill CTA — the house text-button style (preset Save, the tone browser's
 * Browse CTA, gear chips): rounded-full outline on a transparent fill.
 * Primary = white outline + white label; secondary (the "dismiss" next to a
 * primary) = hairline outline + muted label.
 */
export const pillButtonStyle = (primary = true): CSSProperties => ({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  gap: '8px',
  padding: '7px 16px',
  fontSize: '13px',
  fontWeight: 600,
  borderRadius: '9999px',
  border: primary ? '1px solid #ffffff' : BORDER,
  backgroundColor: 'transparent',
  color: primary ? '#ffffff' : MUTED,
  cursor: 'pointer',
  whiteSpace: 'nowrap',
  flexShrink: 0,
});

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
