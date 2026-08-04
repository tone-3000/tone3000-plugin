import React from 'react';
import { helpProps } from './helpText';
import { chromeIcon } from './ChromeIconButton';
import { HIGHLIGHT, ICON_BOX_SIZE, ICON_SIZE, iconButtonStyle } from './theme';

interface IconButtonProps {
  onClick: () => void;
  /** One-line hint for the faceplate help readout (see helpText.ts). */
  help: string;
  /** Lit (white icon, optional active fill) vs muted. Defaults lit. */
  active?: boolean;
  /** Grayed out and non-interactive. */
  disabled?: boolean;
  /** Show the active fill behind the icon while `active`. */
  fillWhenActive?: boolean;
  /** Box size; defaults to ICON_BOX_SIZE. Top bar passes 28. */
  size?: number;
  children: React.ReactNode;
}

/** Glyph size inside the box: chrome ratio at ICON_BOX_SIZE, 18px in the
    top-bar's 28px boxes. */
const glyphSizeFor = (box: number) =>
  box <= ICON_BOX_SIZE ? ICON_SIZE : Math.round((box * 18) / 28);

/** Small square icon button used across the top bar and miscellaneous chrome.
    Faceplate / card / tile controls prefer ChromeIconButton (tones + ICON_SIZE).
    Children still go through chromeIcon so Lucide sizing/centering matches. */
export const IconButton: React.FC<IconButtonProps> = ({
  onClick,
  help,
  active = true,
  disabled = false,
  fillWhenActive = false,
  size = ICON_BOX_SIZE,
  children,
}) => (
  <button
    type="button"
    onClick={onClick}
    disabled={disabled}
    {...helpProps(help)}
    style={{
      ...iconButtonStyle(size),
      color: '#ffffff',
      opacity: disabled ? 0.3 : 1,
      cursor: disabled ? 'default' : 'pointer',
      background: active && fillWhenActive ? HIGHLIGHT : 'transparent',
    }}
  >
    {chromeIcon(children, glyphSizeFor(size))}
  </button>
);
