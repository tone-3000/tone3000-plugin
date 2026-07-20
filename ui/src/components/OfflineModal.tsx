import React from 'react';
import { WifiOff } from 'lucide-react';
import { pillButtonStyle } from './theme';

interface OfflineModalProps {
  open: boolean;
  onRetry: () => void;
  onDismiss: () => void;
}

/**
 * First-line offline gate: shown when a network-dependent action (add /
 * swap / login) is attempted while the OS reports no connection at all
 * (`navigator.onLine === false`). Same full-window scrim + button language
 * as OAuthOverlay so the two error surfaces read as one system.
 */
export const OfflineModal: React.FC<OfflineModalProps> = ({ open, onRetry, onDismiss }) => {
  if (!open) return null;

  return (
    <div
      role="alertdialog"
      aria-label="No internet connection"
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
      <WifiOff size={28} style={{ opacity: 0.9 }} />
      {/* Body copy: reset the global 600 default. */}
      <div style={{ fontSize: 14, fontWeight: 400, opacity: 0.95, maxWidth: 360 }}>
        No internet connection. Connect to browse and load tones from TONE3000.
      </div>
      <div style={{ display: 'flex', gap: 12 }}>
        <button type="button" onClick={onRetry} style={pillButtonStyle()}>
          Try again
        </button>
        <button type="button" onClick={onDismiss} style={pillButtonStyle(false)}>
          Dismiss
        </button>
      </div>
    </div>
  );
};
