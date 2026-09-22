import React, { useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { helpProps } from './helpText';
import { useDismissable } from '../hooks/useDismissable';
import { BORDER, DISABLED_OPACITY, HIGHLIGHT, MUTED, WHITE } from './theme';

/**
 * Right-click action sheet for gallery tiles, in the house floating-panel
 * style (see the faceplate's input-mode menu): #141416 panel, hairline
 * border, 14px radius, icon + label rows with the shared hover highlight.
 *
 * Portaled to document.body with position:fixed at the click's viewport
 * coords (numeric left/top = real px; the rem-denominated sizes scale with
 * the UI like everything else). Dismissed on outside press, Escape, or
 * picking a row.
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

/** Floor for the panel; it grows to fit its longest row (see `width`
    below), so a label never squeezes anything out. */
const MENU_MIN_WIDTH = 148;
const PANEL_PADDING = 6;
/** Visual-px nudge so the panel's top-left sits clearly past the cursor tip. */
const CURSOR_OFFSET = 6;

export const TileMenu: React.FC<{
  anchor: TileMenuAnchor;
  items: TileMenuItem[];
  onClose: () => void;
}> = ({ anchor, items, onClose }) => {
  const rootRef = useRef<HTMLDivElement>(null);

  useDismissable(true, rootRef, onClose);

  // A resize reflows the content under the fixed menu: just dismiss;
  // keeping it at the old client point would look wrong anyway.
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
        // Fixed to the viewport at the click point: pointer coords are real
        // px, so left/top stay numeric (px), never rem.
        position: 'fixed',
        left: anchor.clientX + CURSOR_OFFSET,
        top: anchor.clientY + CURSOR_OFFSET,
        // max-content, not a fixed width: rows are nowrap, and in a fixed
        // panel a label longer than the box shrinks the row's icon away
        // instead of overflowing (flex items shrink, text nodes don't).
        width: 'max-content',
        minWidth: `${MENU_MIN_WIDTH}rem`,
        maxWidth: '260rem',
        backgroundColor: '#141416',
        border: BORDER,
        borderRadius: '14rem',
        padding: `${PANEL_PADDING}rem`,
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
            gap: '12rem',
            width: '100%',
            padding: '9rem 12rem',
            background: 'transparent',
            border: 'none',
            borderRadius: '8rem',
            color: item.disabled ? MUTED : WHITE,
            opacity: item.disabled ? DISABLED_OPACITY : 1,
            fontSize: '13rem',
            fontWeight: 400,
            textAlign: 'left',
            cursor: item.disabled ? 'not-allowed' : 'pointer',
            whiteSpace: 'nowrap',
            boxSizing: 'border-box',
          }}
        >
          <span style={{ display: 'flex', flexShrink: 0 }}>{item.icon}</span>
          {item.label}
        </button>
      ))}
    </div>,
    document.body
  );
};
