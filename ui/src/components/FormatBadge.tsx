import React from 'react';

/** Format tag (NAM / IR / …) matching the web ToneCard badge:
    zinc-400 background, black mono text, 2px corners. */
export const FormatBadge: React.FC<{ label: string }> = ({ label }) => (
  <span
    style={{
      fontFamily: 'monospace',
      fontSize: '12px',
      fontWeight: 400,
      color: '#000000',
      backgroundColor: '#a1a1aa',
      padding: '1px 6px',
      borderRadius: '2px',
      whiteSpace: 'nowrap',
      flexShrink: 0,
      letterSpacing: 'normal',
    }}
  >
    {label}
  </span>
);
