import React from 'react';
import { iconButtonStyle } from './theme';

interface IconButtonProps {
  onClick: () => void;
  title: string;
  /** Lit (white icon, optional active fill) vs muted. Defaults lit. */
  active?: boolean;
  /** Grayed out and non-interactive. */
  disabled?: boolean;
  /** Show the active fill behind the icon while `active`. */
  fillWhenActive?: boolean;
  size?: number;
  children: React.ReactNode;
}

/** Small square icon button used across the top bar and card chrome. */
export const IconButton: React.FC<IconButtonProps> = ({
  onClick,
  title,
  active = true,
  disabled = false,
  fillWhenActive = false,
  size = 28,
  children,
}) => (
  <button
    onClick={onClick}
    disabled={disabled}
    title={title}
    style={{
      ...iconButtonStyle(size),
      color: '#ffffff',
      opacity: disabled ? 0.3 : 1,
      cursor: disabled ? 'default' : 'pointer',
      background: active && fillWhenActive ? 'rgba(235, 235, 245, 0.18)' : 'transparent',
    }}
  >
    {children}
  </button>
);
