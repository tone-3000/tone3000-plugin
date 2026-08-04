import React from 'react';
import { helpProps } from './helpText';
import {
  BLACK,
  BORDER,
  BRAND_YELLOW,
  GRAY,
  HIGHLIGHT,
  ICON_BOX_SIZE,
  ICON_SIZE,
  iconButtonStyle,
  MUTED,
  textBoxStyle,
  WHITE,
} from './theme';

/**
 * Shared chrome icon button for the faceplate, card headers, tiles, and
 * pan rail. Box is always ICON_BOX_SIZE × ICON_BOX_RADIUS; glyphs are
 * forced to ICON_SIZE and grid-centered in the box (no nested flex/span
 * that WebKit can baseline-shift).
 */
export type ChromeTone = 'plain' | 'power' | 'armed' | 'link' | 'outline';

interface ChromeIconButtonProps {
  onClick: (e: React.MouseEvent) => void;
  /** One-line hint for the faceplate help readout (see helpText.ts). */
  help: string;
  children: React.ReactNode;
  /**
   * plain:   white icon (optional HIGHLIGHT via `filled`).
   * power:   on = white/clear; off = GRAY + HIGHLIGHT.
   * armed:   on = BRAND_YELLOW + BLACK (listening / engaged); off = BORDER + GRAY.
   * link:    on = white; off = GRAY, never a fill (pan link).
   * outline: on = WHITE border + icon; off = BORDER + GRAY (block normalize).
   */
  tone?: ChromeTone;
  /** For power/armed/link: feature on / listening / linked. */
  on?: boolean;
  /** plain only: HIGHLIGHT fill while true (e.g. non-default input mode). */
  filled?: boolean;
  /** Vertical nudge from a bottom-aligned row (faceplate side buttons). */
  offsetY?: number;
  /** e.g. preventFocus on gallery tiles so the webview doesn't scroll. */
  onMouseDown?: (e: React.MouseEvent) => void;
  disabled?: boolean;
  style?: React.CSSProperties;
}

/** Always 1px so the content box never changes between tones. */
const toneChrome = (
  tone: ChromeTone,
  on: boolean,
  filled: boolean
): Pick<React.CSSProperties, 'color' | 'backgroundColor' | 'border'> => {
  switch (tone) {
    case 'power':
      return {
        color: on ? WHITE : GRAY,
        backgroundColor: on ? 'transparent' : HIGHLIGHT,
        border: '1px solid transparent',
      };
    case 'armed':
      return {
        color: on ? BLACK : GRAY,
        backgroundColor: on ? BRAND_YELLOW : 'transparent',
        border: on ? `1px solid ${BRAND_YELLOW}` : BORDER,
      };
    case 'link':
      return {
        color: on ? WHITE : GRAY,
        backgroundColor: 'transparent',
        border: '1px solid transparent',
      };
    case 'outline':
      return {
        color: on ? WHITE : GRAY,
        backgroundColor: 'transparent',
        border: on ? `1px solid ${WHITE}` : BORDER,
      };
    default:
      return {
        color: WHITE,
        backgroundColor: filled ? HIGHLIGHT : 'transparent',
        border: '1px solid transparent',
      };
  }
};

/**
 * Normalize Lucide / SVG icons for chrome boxes. Stroke scales with size;
 * do NOT use absoluteStrokeWidth (at 14px that locks a 2px stroke and looks
 * bloated). `display:block` kills the inline-SVG baseline gap, and grid
 * centering on the box keeps every glyph optically centered without per-icon
 * nudges.
 *
 * Pass `size` to force a glyph size (ChromeIconButton). Omit it to keep the
 * child's own size (IconButton in the top bar uses 18px in a 28px box).
 */
export function chromeIcon(node: React.ReactNode, size?: number): React.ReactNode {
  if (!React.isValidElement(node)) return node;
  const el = node as React.ReactElement<{
    size?: number | string;
    absoluteStrokeWidth?: boolean;
    style?: React.CSSProperties;
  }>;
  return React.cloneElement(el, {
    ...(size !== undefined ? { size } : null),
    absoluteStrokeWidth: false,
    style: {
      display: 'block',
      flexShrink: 0,
      ...(el.props.style ?? {}),
    },
  });
}

export const ChromeIconButton: React.FC<ChromeIconButtonProps> = ({
  onClick,
  help,
  children,
  tone = 'plain',
  on = true,
  filled = false,
  offsetY,
  onMouseDown,
  disabled = false,
  style,
}) => (
  <button
    type="button"
    onClick={onClick}
    onMouseDown={onMouseDown}
    disabled={disabled}
    {...helpProps(help)}
    style={{
      ...iconButtonStyle(ICON_BOX_SIZE),
      // Grid on the button itself: one centering context, no nested span.
      display: 'grid',
      placeItems: 'center',
      ...toneChrome(tone, on, filled),
      opacity: disabled ? 0.3 : 1,
      cursor: disabled ? 'default' : 'pointer',
      transform: offsetY !== undefined ? `translateY(${offsetY}px)` : undefined,
      ...style,
    }}
  >
    {chromeIcon(children, ICON_SIZE)}
  </button>
);

/**
 * Text chrome button (EQ, PRE). TEXT_BOX_HEIGHT / ICON_BOX_RADIUS.
 * Priority: open (panel showing) > armed (feature shaping) > idle.
 * - open: WHITE fill + BLACK label.
 * - armed: BRAND_YELLOW fill + BLACK label.
 * - idle: BORDER + MUTED.
 */
interface ChromeTextButtonProps {
  onClick: () => void;
  help: string;
  children: React.ReactNode;
  armed?: boolean;
  open?: boolean;
  style?: React.CSSProperties;
}

const textChrome = (
  armed: boolean,
  open: boolean
): Pick<React.CSSProperties, 'color' | 'backgroundColor' | 'border'> => {
  if (open) {
    return {
      color: BLACK,
      backgroundColor: WHITE,
      border: `1px solid ${WHITE}`,
    };
  }
  if (armed) {
    return {
      color: BLACK,
      backgroundColor: BRAND_YELLOW,
      border: `1px solid ${BRAND_YELLOW}`,
    };
  }
  return {
    color: MUTED,
    backgroundColor: 'transparent',
    border: BORDER,
  };
};

export const ChromeTextButton: React.FC<ChromeTextButtonProps> = ({
  onClick,
  help,
  children,
  armed = false,
  open = false,
  style,
}) => (
  <button
    type="button"
    onClick={onClick}
    {...helpProps(help)}
    style={{
      ...textBoxStyle(),
      ...textChrome(armed, open),
      lineHeight: 1,
      ...style,
    }}
  >
    {children}
  </button>
);
