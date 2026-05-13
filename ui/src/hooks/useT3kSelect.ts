import { useCallback, useEffect, useMemo, useRef } from 'react';
import { PUBLISHABLE_KEY, T3K_ARCHITECTURE, getRedirectUri } from '../t3k/config';
import {
  T3KClient,
  startSelectFlow,
  handleOAuthCallback,
} from '../t3k/tone3000-client';
import type { T3KTokens } from '../t3k/tone3000-client';
import type { Model, Tone } from '../types/tone';

interface UseT3kSelectOptions {
  /** Called when a selection has been resolved into a Tone (with embedded models). */
  onToneSelected?: (tone: Tone & { models: Model[] }, tokens: T3KTokens) => void;
  /** Called whenever a fresh access token is available (initial + refresh). */
  onAccessTokenUpdated?: (accessToken: string) => void;
}

/**
 * Drives the TONE3000 Select OAuth flow on behalf of the main webview.
 *
 * - In a desktop browser (i.e. no JUCE bridge present) it runs the OAuth flow
 *   in the same window, exchanging the callback when we land back here.
 * - Inside JUCE, the select webview runs the flow and hands tokens + toneId
 *   back to the main view via the message bus — `applySelection` accepts that
 *   payload and finishes the work (fetch tone metadata + models, fire the
 *   callback).
 *
 * Either way, after a successful selection we hold a `T3KClient` with valid
 * tokens that can be reused for follow-up requests (e.g. switching models).
 */
export const useT3kSelect = ({
  onToneSelected,
  onAccessTokenUpdated,
}: UseT3kSelectOptions) => {
  const tokenListenerRef = useRef(onAccessTokenUpdated);
  tokenListenerRef.current = onAccessTokenUpdated;

  const client = useMemo(() => {
    const c = new T3KClient(PUBLISHABLE_KEY, () => {
      // If the refresh token is rejected we lose access — the user has to
      // start the Select flow again from the +. Nothing to do automatically.
      console.warn('TONE3000 session expired; re-auth required.');
    });
    // Forward initial token sets *and* automatic refreshes to the consumer
    // so the native-side Bearer token always matches the live access token.
    c.setTokenListener((tokens) => tokenListenerRef.current?.(tokens.access_token));
    return c;
  }, []);

  const setTokens = useCallback(
    (tokens: T3KTokens) => {
      client.setTokens(tokens);
    },
    [client]
  );

  const fetchToneAndModels = useCallback(
    async (toneId: string | number) => {
      const tone = await client.getTone(toneId);
      // Only NAM tones use `architecture=2` on list models (v2 weights the plugin
      // loads). IR and other platforms are not NAM architectures — never pass it there.
      const isNamPlatform = tone.platform?.toLowerCase() === 'nam';
      const modelsRes = await client.listModels(toneId, {
        ...(isNamPlatform && T3K_ARCHITECTURE !== undefined
          ? { architecture: T3K_ARCHITECTURE }
          : {}),
      });
      return { ...tone, models: modelsRes.data } as Tone & { models: Model[] };
    },
    [client]
  );

  /**
   * Called from the JUCE select webview message bridge: take the tokens +
   * toneId we just received, store the tokens, fetch tone metadata + models,
   * and forward the combined Tone object to the consumer.
   */
  const applySelection = useCallback(
    async (payload: { tokens: T3KTokens; toneId: string | number }) => {
      setTokens(payload.tokens);
      const tone = await fetchToneAndModels(payload.toneId);
      onToneSelected?.(tone, payload.tokens);
    },
    [setTokens, fetchToneAndModels, onToneSelected]
  );

  // For the standalone web-browser dev path: handle the OAuth callback if the
  // page just landed back from tone3000.com.
  const processedCallbackRef = useRef(false);
  useEffect(() => {
    if (processedCallbackRef.current) return;
    const params = new URLSearchParams(window.location.search);
    const isCallback =
      params.has('code') ||
      (params.has('error') && params.has('state')) ||
      params.has('canceled');
    if (!isCallback) return;
    processedCallbackRef.current = true;

    handleOAuthCallback(PUBLISHABLE_KEY, getRedirectUri())
      .then(async (result) => {
        const cleaned = new URL(window.location.href);
        cleaned.search = '';
        window.history.replaceState({}, '', cleaned.toString());

        if (!result.ok) {
          if (result.error !== 'canceled') {
            console.error('TONE3000 OAuth failed:', result.error);
          }
          return;
        }

        setTokens(result.tokens);
        if (result.toneId) {
          const tone = await fetchToneAndModels(result.toneId);
          onToneSelected?.(tone, result.tokens);
        }
      })
      .catch((err) => console.error('TONE3000 callback error:', err));
  }, [setTokens, fetchToneAndModels, onToneSelected]);

  /** Kick off the Select flow in the current window (web-browser path). */
  const startSelectFlowInBrowser = useCallback(() => {
    if (!PUBLISHABLE_KEY) {
      console.error(
        'TONE3000 publishable key not configured. Set VITE_T3K_PUBLISHABLE_KEY at build time.'
      );
      return;
    }
    startSelectFlow(PUBLISHABLE_KEY, getRedirectUri(), {
      menubar: true,
      architecture: T3K_ARCHITECTURE,
    });
  }, []);

  return {
    client,
    applySelection,
    startSelectFlowInBrowser,
    setTokens,
  };
};
