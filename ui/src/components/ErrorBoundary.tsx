import React from 'react';
import * as Juce from 'juce-framework-frontend';

interface ErrorBoundaryState {
  error: Error | null;
}

/**
 * Root-level error boundary. Without this, any uncaught render error unmounts
 * the whole React tree and the plugin window goes black with no way to
 * recover. Instead we show a branded fallback with the error message, a
 * copy-logs shortcut (console output is already forwarded to the native log
 * file) and a reload button that restarts the UI in place.
 */
export class ErrorBoundary extends React.Component<
  { children: React.ReactNode },
  ErrorBoundaryState
> {
  state: ErrorBoundaryState = { error: null };

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    // console.error is shimmed to forward into TONE3000.log on the native side.
    console.error('UI crashed:', error.stack ?? error.message, info.componentStack ?? '');
  }

  private copyLogs = () => {
    try {
      Juce.getNativeFunction('copyLogs')();
    } catch {
      // Not running inside the plugin (plain browser dev) — nothing to copy.
    }
  };

  private reload = () => {
    window.location.reload();
  };

  render() {
    if (!this.state.error) return this.props.children;

    return (
      <div
        role="alert"
        style={{
          position: 'fixed',
          inset: 0,
          backgroundColor: '#000',
          color: '#fff',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 16,
          padding: 32,
          textAlign: 'center',
          fontFamily: 'system-ui, -apple-system, sans-serif',
        }}
      >
        <div style={{ fontSize: 15, fontWeight: 600 }}>Something went wrong</div>
        <div
          style={{
            fontSize: 12,
            fontWeight: 400,
            opacity: 0.7,
            maxWidth: 480,
            maxHeight: 120,
            overflow: 'hidden',
            wordBreak: 'break-word',
          }}
        >
          {this.state.error.message}
        </div>
        <div style={{ display: 'flex', gap: 12 }}>
          <button
            type="button"
            onClick={this.reload}
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
            Reload UI
          </button>
          <button
            type="button"
            onClick={this.copyLogs}
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
            Copy logs
          </button>
        </div>
        <div style={{ fontSize: 11, fontWeight: 400, opacity: 0.45 }}>
          The error has been written to the TONE3000 log file.
        </div>
      </div>
    );
  }
}
