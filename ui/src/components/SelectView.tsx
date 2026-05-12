import React, { useEffect, useRef, useState } from 'react';
import { PUBLISHABLE_KEY, T3K_ARCHITECTURE, getRedirectUri } from '../t3k/config';
import { handleOAuthCallback, startSelectFlow } from '../t3k/tone3000-client';
import type { T3KTokens } from '../t3k/tone3000-client';

interface SelectViewProps {
  /**
   * Called once with the tokens + tone_id collected from the new OAuth Select
   * flow. The select webview defers all tone metadata + model fetching to the
   * main view (which holds the active chain) — this view's only job is to
   * complete authentication and hand the result back across the bridge.
   */
  onSelectComplete: (payload: { tokens: T3KTokens; toneId: string }) => void;
  /** Called when the user closes/cancels the select flow. */
  onSelectCancelled: (reason: string) => void;
}

type Phase = 'redirecting' | 'exchanging' | 'error';

export const SelectView: React.FC<SelectViewProps> = ({
  onSelectComplete,
  onSelectCancelled,
}) => {
  const ranRef = useRef(false);
  const [phase, setPhase] = useState<Phase>('redirecting');
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  useEffect(() => {
    // Strict mode runs effects twice in dev — guard against double-redirect /
    // double-token-exchange (the latter would burn the single-use code).
    if (ranRef.current) return;
    ranRef.current = true;

    const params = new URLSearchParams(window.location.search);
    const isCallback =
      params.has('code') ||
      (params.has('error') && params.has('state')) ||
      params.has('canceled');

    if (!isCallback) {
      if (!PUBLISHABLE_KEY) {
        setErrorMessage(
          'Missing TONE3000 publishable key. Set VITE_T3K_PUBLISHABLE_KEY at build time.'
        );
        setPhase('error');
        return;
      }
      // First load — redirect into the TONE3000 OAuth Select flow. Restrict
      // the catalog to architectures the plugin can actually run.
      startSelectFlow(PUBLISHABLE_KEY, getRedirectUri(), {
        menubar: true,
        architecture: T3K_ARCHITECTURE,
      }).catch((err) => {
        console.error('Failed to start TONE3000 select flow', err);
        setErrorMessage('Failed to start TONE3000 select flow.');
        setPhase('error');
      });
      return;
    }

    // We just came back from TONE3000 — exchange code for tokens.
    setPhase('exchanging');
    handleOAuthCallback(PUBLISHABLE_KEY, getRedirectUri())
      .then((result) => {
        // Strip the OAuth params so a refresh doesn't try to re-redeem the code.
        const cleaned = new URL(window.location.href);
        cleaned.search = '';
        window.history.replaceState({}, '', cleaned.toString());

        if (!result.ok) {
          if (result.error === 'canceled') {
            onSelectCancelled('canceled');
            return;
          }
          console.error('TONE3000 OAuth failed:', result.error);
          setErrorMessage(`Authentication failed: ${result.error}`);
          setPhase('error');
          return;
        }

        if (result.canceled || !result.toneId) {
          // User signed in but didn't pick a tone (or closed via menubar).
          onSelectCancelled('no_tone_selected');
          return;
        }

        onSelectComplete({ tokens: result.tokens, toneId: result.toneId });
      })
      .catch((err) => {
        console.error('Error handling TONE3000 OAuth callback:', err);
        setErrorMessage('Error completing TONE3000 sign-in.');
        setPhase('error');
      });
  }, [onSelectComplete, onSelectCancelled]);

  const containerStyle: React.CSSProperties = {
    width: '100%',
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#000',
    color: '#fff',
    fontFamily: 'system-ui, -apple-system, sans-serif',
    gap: 12,
    padding: 24,
    textAlign: 'center',
  };

  if (phase === 'error') {
    return (
      <div style={containerStyle}>
        <div style={{ fontSize: 14, opacity: 0.85 }}>{errorMessage}</div>
        <div style={{ fontSize: 12, opacity: 0.6 }}>
          Close this window and try again.
        </div>
      </div>
    );
  }

  return (
    <div style={containerStyle}>
      <div style={{ fontSize: 14, opacity: 0.7 }}>
        {phase === 'exchanging'
          ? 'Loading tone from TONE3000…'
          : 'Connecting to TONE3000…'}
      </div>
    </div>
  );
};
