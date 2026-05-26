import React from 'react';
import type { OAuthPhase } from '../hooks/useT3kSelect';

interface OAuthOverlayProps {
  phase: OAuthPhase;
  error: string | null;
  onRetry: () => void;
  onDismiss: () => void;
}

/**
 * Covers the main plugin UI while the OAuth Select flow is resolving after a
 * redirect back from tone3000.com. Without this, the user would see the chain
 * UI flash empty (chain state hydrates from native after first render) before
 * the freshly-selected tone lands in the chain.
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
        backgroundColor: '#000',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 16,
        padding: 24,
        textAlign: 'center',
        color: '#fff',
        fontFamily: 'system-ui, -apple-system, sans-serif',
        zIndex: 100,
      }}
    >
      <style>
        {`@keyframes oauthOverlaySpinner { to { transform: rotate(360deg); } }`}
      </style>
      {phase === 'returning' && (
        <>
          <div
            aria-label="Loading"
            style={{
              width: 36,
              height: 36,
              border: '3px solid rgba(255, 255, 255, 0.08)',
              borderTopColor: '#9ca3af',
              borderRadius: '50%',
              animation: 'oauthOverlaySpinner 0.65s linear infinite',
            }}
          />
          <div style={{ fontSize: 14, opacity: 0.85 }}>
            Returning from TONE3000…
          </div>
        </>
      )}
      {phase === 'error' && (
        <>
          <div style={{ fontSize: 14, opacity: 0.95, maxWidth: 360 }}>
            {error ?? 'Something went wrong completing TONE3000 sign-in.'}
          </div>
          <div style={{ display: 'flex', gap: 12 }}>
            <button
              type="button"
              onClick={onRetry}
              style={{
                background: '#1C1C1E',
                color: '#fff',
                border: '1px solid rgba(255, 255, 255, 0.2)',
                borderRadius: 6,
                padding: '6px 14px',
                fontSize: 13,
                cursor: 'pointer',
              }}
            >
              Try again
            </button>
            <button
              type="button"
              onClick={onDismiss}
              style={{
                background: 'transparent',
                color: '#fff',
                border: '1px solid rgba(255, 255, 255, 0.2)',
                borderRadius: 6,
                padding: '6px 14px',
                fontSize: 13,
                cursor: 'pointer',
              }}
            >
              Dismiss
            </button>
          </div>
        </>
      )}
    </div>
  );
};
