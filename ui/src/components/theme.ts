import type { CSSProperties } from 'react';

/**
 * Shared theme tokens. The palette is deliberately tiny: black surfaces,
 * white/gray chrome, and three brand accents (pure blue / yellow / red) for
 * audio visuals and UI "attention" states. Components import these instead of
 * re-declaring the same hex literals.
 *
 * Icon / chrome box system (Lucide glyphs; custom SVGs use `currentColor`,
 * round caps, and a stroke weight that matches Lucide at the rendered size):
 * - Glyph in a box: ICON_SIZE (14). Square box: ICON_BOX_SIZE (20) ×
 *   ICON_BOX_RADIUS (2). Text / non-square chrome (EQ, PRE, LITE/FULL,
 *   segmented strips): TEXT_BOX_HEIGHT (20), 12px monospace, 4px L/R padding.
 * - Interactive icons are white; GRAY only for off/disabled states.
 * - State patterns (see ChromeIconButton / ChromeTextButton):
 *   1. Power (on/off): on = white icon, no fill; off = GRAY icon + HIGHLIGHT.
 *   2. Open / panel showing (EQ editor open): WHITE fill + BLACK label.
 *   3. Armed / listening / shaping (auto-balance, active EQ while closed,
 *      PRE, normalize): BRAND_YELLOW fill + BLACK glyph/label.
 *   4. Link (pan link): on = white icon; off = GRAY icon — never a fill.
 */

/** Lucide / custom glyph size inside ICON_BOX_SIZE chrome boxes. */
export const ICON_SIZE = 14;
/** Square hit-target for icon buttons beside knobs and in card headers. */
export const ICON_BOX_SIZE = 20;
/** Corner radius for every icon/text chrome box. */
export const ICON_BOX_RADIUS = 2;
/** Height for text chrome (EQ, PRE, LITE/FULL segments). */
export const TEXT_BOX_HEIGHT = 20;

/** Brand accents — the only chromatic UI colors outside gray/white/black. */
export const BRAND_BLUE = '#0000FF';
export const BRAND_YELLOW = '#FFFF00';
export const BRAND_RED = '#FF0000';

export const WHITE = '#ffffff';
export const BLACK = '#000000';

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
  border: primary ? `1px solid ${WHITE}` : BORDER,
  backgroundColor: 'transparent',
  color: primary ? WHITE : MUTED,
  cursor: 'pointer',
  whiteSpace: 'nowrap',
  flexShrink: 0,
});

/** Base style for a square icon chrome box (faceplate / card / tile).
 *  Grid + placeItems centers glyphs reliably; flex+inline-SVG baseline
 *  quirks are what made Power look low in the expanded block header. */
export const iconButtonStyle = (size = ICON_BOX_SIZE): CSSProperties => ({
  background: 'transparent',
  border: '1px solid transparent',
  outline: 'none',
  color: MUTED,
  cursor: 'pointer',
  width: `${size}px`,
  height: `${size}px`,
  borderRadius: ICON_BOX_RADIUS,
  display: 'grid',
  placeItems: 'center',
  padding: 0,
  flexShrink: 0,
  boxSizing: 'border-box',
  lineHeight: 0,
  fontSize: 0,
});

/** Text chrome box (EQ, PRE, static LITE/FULL label): fixed height, mono. */
export const textBoxStyle = (): CSSProperties => ({
  height: `${TEXT_BOX_HEIGHT}px`,
  borderRadius: ICON_BOX_RADIUS,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  padding: '0 4px',
  fontSize: '12px',
  fontWeight: 400,
  fontFamily: 'monospace',
  lineHeight: 1,
  boxSizing: 'border-box',
  flexShrink: 0,
  cursor: 'pointer',
  background: 'transparent',
});

/** Shared fill behind LITE/FULL and the EQ view switcher. */
export const SEGMENTED_TRACK = 'rgba(120, 120, 128, 0.36)';

/**
 * Segmented control shell (LITE/FULL, EQ view). Borderless track fill —
 * selection is white vs MUTED text/icons, not a cell highlight.
 */
export const segmentedGroupStyle = (): CSSProperties => ({
  display: 'flex',
  flexDirection: 'row',
  alignItems: 'stretch',
  height: `${TEXT_BOX_HEIGHT}px`,
  borderRadius: ICON_BOX_RADIUS,
  border: 'none',
  backgroundColor: SEGMENTED_TRACK,
  overflow: 'hidden',
  flexShrink: 0,
  boxSizing: 'border-box',
});

/**
 * One cell inside a segmented group. Non-square chrome (text or icon
 * strips) always gets 4px left/right padding — never a tight ICON_BOX square.
 */
export const segmentedCellStyle = (icon = false): CSSProperties => ({
  height: '100%',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  border: 'none',
  cursor: 'pointer',
  backgroundColor: 'transparent',
  padding: '0 4px',
  fontSize: icon ? 0 : '12px',
  fontWeight: icon ? undefined : 400,
  fontFamily: icon ? undefined : 'monospace',
  lineHeight: icon ? 0 : 1,
  flexShrink: 0,
  boxSizing: 'border-box',
});

/**
 * Vertical lift for a chrome icon box sitting in a bottom-aligned faceplate
 * row: from the shared label baseline up to the center of a secondary knob.
 * (10px gap + 14px label slot + radius − half the box.)
 */
export const faceplateChromeLift = (secondaryKnobSize: number) =>
  -(10 + 14 + secondaryKnobSize / 2 - ICON_BOX_SIZE / 2);
