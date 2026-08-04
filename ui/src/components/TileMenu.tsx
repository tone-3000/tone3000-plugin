import React, { useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { helpProps } from './helpText';
import { useDismissable } from '../hooks/useDismissable';
import { DESIGN_WIDTH } from '../hooks/useUiScale';
import { BORDER, HIGHLIGHT, MUTED, WHITE } from './theme';

/**
 * Right-click action sheet for gallery tiles, in the house floating-panel
 * style (see the faceplate's input-mode menu): #141416 panel, hairline
 * border, 14px radius, icon + label rows with the shared hover highlight.
 *
 * Portaled to document.body with position:fixed at the click's viewport
 * coords. CSS zoom on the plugin root would otherwise desync layout-space
 * offsets from the cursor as the window scales. Size is matched via
 * transform:scale (same factor as useUiScale); applying CSS zoom here would
 * also scale left/top and shove the panel away from the cursor. Dismissed
 * on outside press, Escape, or picking a row.
 */

export interface TileMenuItem {
  label: string;
  icon: React.ReactNode;
  /** One-line hint for the faceplate help readout. */
  help: string;
  disabled?: boolean;
  onSelect: () => void;
}

/** Click point in viewport (client) coordinates. */
export interface TileMenuAnchor {
  clientX: number;
  clientY: number;
}

const MENU_WIDTH = 148;
const PANEL_PADDING = 6;
/** Visual-px nudge so the panel's top-left sits clearly past the cursor tip. */
const CURSOR_OFFSET = 6;

export const TileMenu: React.FC<{
  anchor: TileMenuAnchor;
  items: TileMenuItem[];
  onClose: () => void;
}> = ({ anchor, items, onClose }) => {
  const rootRef = useRef<HTMLDivElement>(null);
  const scale = Math.max(1, document.documentElement.clientWidth / DESIGN_WIDTH);

  useDismissable(true, rootRef, onClose);

  // Zoom can change under an open menu (window resize): just dismiss;
  // reopening at the old client point would look wrong anyway.
  useEffect(() => {
    window.addEventListener('resize', onClose);
    return () => window.removeEventListener('resize', onClose);
  }, [onClose]);

  return createPortal(
    <div
      ref={rootRef}
      // Keep every gesture inside the panel: clicks must not open the tile's
      // detail view, presses must not arm a drag under the menu.
      onClick={(e) => e.stopPropagation()}
      onPointerDown={(e) => e.stopPropagation()}
      onContextMenu={(e) => {
        e.preventDefault();
        e.stopPropagation();
      }}
      style={{
        // Fixed to the viewport at the click point. Match UI size with
        // transform:scale, since CSS zoom on this node would also scale left/top,
        // shoving the panel right/down as the window grows.
        position: 'fixed',
        left: `${anchor.clientX + CURSOR_OFFSET}px`,
        top: `${anchor.clientY + CURSOR_OFFSET}px`,
        width: `${MENU_WIDTH}px`,
        transform: `scale(${scale})`,
        transformOrigin: 'top left',
        backgroundColor: '#141416',
        border: BORDER,
        borderRadius: '14px',
        padding: `${PANEL_PADDING}px`,
        zIndex: 1000,
        boxSizing: 'border-box',
      }}
    >
      <style>{`.tile-menu-item:hover:not(:disabled) { background-color: ${HIGHLIGHT}; }`}</style>
      {items.map((item) => (
        <button
          key={item.label}
          type="button"
          className="tile-menu-item"
          disabled={item.disabled}
          {...helpProps(item.help)}
          onClick={() => {
            onClose();
            item.onSelect();
          }}
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: '12px',
            width: '100%',
            padding: '9px 12px',
            background: 'transparent',
            border: 'none',
            borderRadius: '8px',
            color: item.disabled ? MUTED : WHITE,
            opacity: item.disabled ? 0.4 : 1,
            fontSize: '13px',
            fontWeight: 400,
            textAlign: 'left',
            cursor: item.disabled ? 'default' : 'pointer',
            whiteSpace: 'nowrap',
            boxSizing: 'border-box',
          }}
        >
          {item.icon}
          {item.label}
        </button>
      ))}
    </div>,
    document.body
  );
};
