import React from 'react';
import type { OAuthPhase } from '../hooks/useT3kSelect';
import { LoadingDots } from './LoadingDots';
import { pillButtonStyle } from './theme';

interface OAuthOverlayProps {
  phase: OAuthPhase;
  error: string | null;
  onRetry: () => void;
  onDismiss: () => void;
}

/**
 * Busy scrim over the whole plugin while an OAuth redirect is in flight —
 * leaving for tone3000.com or resolving the callback after landing back.
 * The normal UI keeps rendering underneath (dimmed + blurred, web-style)
 * instead of a blank takeover, so the user comes straight back to the view
 * they'll interact with. Errors surface on the same scrim with a retry.
 *
 * zIndex sits above every takeover (tone browser / settings are 2000) so a
 * redirect kicked off from inside one still dims it.
 */
export const OAuthOverlay: React.FC<OAuthOverlayProps> = ({
  phase,
  error,
  onRetry,
  onDismiss,
}) => {
  if (phase === 'idle') return null;

  return (
    <div
      role="status"
      aria-live="polite"
      style={{
        position: 'absolute',
        inset: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.5)',
        backdropFilter: 'blur(4px)',
        WebkitBackdropFilter: 'blur(4px)',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 16,
        padding: 24,
        textAlign: 'center',
        color: '#fff',
        zIndex: 3000,
      }}
    >
      {(phase === 'leaving' || phase === 'returning') && <LoadingDots />}
      {phase === 'error' && (
        <>
          <div style={{ fontSize: 14, opacity: 0.95, maxWidth: 360 }}>
            {error ?? 'Something went wrong completing TONE3000 sign-in.'}
          </div>
          <div style={{ display: 'flex', gap: 12 }}>
            <button type="button" onClick={onRetry} style={pillButtonStyle()}>
              Try again
            </button>
            <button type="button" onClick={onDismiss} style={pillButtonStyle(false)}>
              Dismiss
            </button>
          </div>
        </>
      )}
    </div>
  );
};
