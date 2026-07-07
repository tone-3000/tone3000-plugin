import React from 'react';
import { WifiOff } from 'lucide-react';

interface OfflineModalProps {
  open: boolean;
  onRetry: () => void;
  onDismiss: () => void;
  /** Override the body copy for other internet-dependent features. */
  message?: string;
}

/**
 * Modal shown when an internet-dependent action (e.g. loading tones from
 * TONE3000) is attempted while offline. Pairs with useInternetGate.
 */
export const OfflineModal: React.FC<OfflineModalProps> = ({
  open,
  onRetry,
  onDismiss,
  message = 'An internet connection is required to load tones from TONE3000. Check your connection and try again.',
}) => {
  if (!open) return null;

  return (
    <div
      role="alertdialog"
      aria-modal="true"
      aria-label="No internet connection"
      style={{
        position: 'absolute',
        inset: 0,
        backgroundColor: 'rgba(0, 0, 0, 0.7)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: 24,
        zIndex: 110,
      }}
      onClick={onDismiss}
    >
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          background: '#1C1C1E',
          border: '1px solid rgba(84, 84, 88, 0.65)',
          borderRadius: 12,
          padding: '28px 32px',
          maxWidth: 380,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          gap: 16,
          textAlign: 'center',
          color: '#fff',
          fontFamily: 'system-ui, -apple-system, sans-serif',
        }}
      >
        <WifiOff size={32} color="#9ca3af" />
        <div style={{ fontSize: 15, fontWeight: 600 }}>No internet connection</div>
        <div style={{ fontSize: 13, opacity: 0.8, lineHeight: 1.5 }}>{message}</div>
        <div style={{ display: 'flex', gap: 12, marginTop: 4 }}>
          <button
            type="button"
            onClick={onRetry}
            style={{
              background: '#fff',
              color: '#000',
              border: 'none',
              borderRadius: 6,
              padding: '7px 16px',
              fontSize: 13,
              fontWeight: 600,
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
              padding: '7px 16px',
              fontSize: 13,
              cursor: 'pointer',
            }}
          >
            Dismiss
          </button>
        </div>
      </div>
    </div>
  );
};
