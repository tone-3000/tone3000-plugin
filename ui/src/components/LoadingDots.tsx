import React from 'react';

/**
 * Ports of the tone3000.com loading patterns:
 *   - LoadingDots: the three blinking dots (web `LoadingDots` component).
 *   - BusyOverlay: dim + blur scrim over in-place content while it reloads
 *     (web `Tones.tsx` loading overlay). The content stays mounted and
 *     visibly "disabled" underneath; the dots sit on top.
 */

export const LoadingDots: React.FC = () => (
  <span style={{ display: 'inline-flex', alignItems: 'center' }} aria-label="Loading">
    <style>
      {`@keyframes t3kDotsBlink { 0% { opacity: 0.2; } 20% { opacity: 1; } 100% { opacity: 0.2; } }`}
    </style>
    {[0, 1, 2].map((i) => (
      <span
        key={i}
        style={{
          width: '8px',
          height: '8px',
          borderRadius: '50%',
          backgroundColor: '#e4e4e7',
          margin: '0 2px',
          animation: 't3kDotsBlink 1.4s infinite both',
          animationDelay: `${i * 0.2}s`,
        }}
      />
    ))}
  </span>
);

interface BusyOverlayProps {
  /** Dots pinned near the top (list reloads, web-style) or centered. */
  align?: 'top' | 'center';
  zIndex?: number;
}

export const BusyOverlay: React.FC<BusyOverlayProps> = ({ align = 'top', zIndex = 20 }) => (
  <div
    style={{
      position: 'absolute',
      inset: 0,
      display: 'flex',
      alignItems: align === 'top' ? 'flex-start' : 'center',
      justifyContent: 'center',
      paddingTop: align === 'top' ? '64px' : 0,
      backgroundColor: 'rgba(0, 0, 0, 0.5)',
      backdropFilter: 'blur(4px)',
      WebkitBackdropFilter: 'blur(4px)',
      zIndex,
    }}
  >
    <LoadingDots />
  </div>
);
