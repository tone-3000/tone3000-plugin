import React from 'react';

/**
 * Shared chain-column layout: tone card dimensions plus the fixed-width row
 * wrapper used by the toolbars pinned above the chain (spread / stereo
 * controls), so their edges line up with the cards. Lives in its own module
 * (no component imports) to keep the dependency graph acyclic.
 */

export const CARD_WIDTH = 800;
export const CARD_HEIGHT = 216;

/** Fixed-width row aligned with chain tone cards (toolbar controls, etc.). */
export const ChainContentRow: React.FC<{
  children: React.ReactNode;
  style?: React.CSSProperties;
}> = ({ children, style }) => (
  <div
    style={{
      width: `${CARD_WIDTH}px`,
      margin: '0 auto',
      boxSizing: 'border-box',
      ...style,
    }}
  >
    {children}
  </div>
);
