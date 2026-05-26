import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { PUBLISHABLE_KEY, T3K_ARCHITECTURE, getRedirectUri } from '../t3k/config';
import {
  T3KClient,
  startSelectFlow as startSelectFlowRedirect,
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
 * Phase of the OAuth Select flow as far as the UI is concerned:
 *   - 'idle'      → no flow in progress; main UI is interactive
 *   - 'returning' → we just landed back on the page with ?code/?error/?canceled
 *                   and are exchanging the code / fetching tone+models. The
 *                   main UI should be covered by a spinner.
 *   - 'error'     → the callback resolved with an error we want to surface;
 *                   the consumer should show a message + a retry affordance.
 */
export type OAuthPhase = 'idle' | 'returning' | 'error';

/**
 * Return true synchronously if the current URL looks like an OAuth callback
 * landing. Used to initialise the phase before the first render so the spinner
 * overlay covers the main UI on the very first paint after the redirect.
 */
function detectInitialCallback(): boolean {
  if (typeof window === 'undefined') return false;
  const params = new URLSearchParams(window.location.search);
  return (
    params.has('code') ||
    (params.has('error') && params.has('state')) ||
    params.has('canceled')
  );
}

/**
 * Drives the TONE3000 Select OAuth flow for the single main webview.
 *
 * Flow:
 *   1. User clicks + → `startSelectFlow()` navigates the webview to
 *      tone3000.com's authorize URL.
 *   2. After the user picks (or cancels), TONE3000 redirects back to the
 *      same page with `?code&state&tone_id` (or `?canceled=true` / `?error=`).
 *   3. On mount the hook detects those params, exchanges the code for tokens,
 *      fetches tone + models, and fires `onToneSelected`.
 *
 * The `oauthPhase` return drives a spinner overlay in the main UI so the user
 * doesn't see an empty React tree flash between landing back and the tone
 * being loaded into the chain.
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

  // Initialise the phase synchronously from the URL so the first render of
  // the consumer already has the overlay in place — otherwise the main UI
  // briefly paints before the useEffect below sets 'returning'.
  const [oauthPhase, setOauthPhase] = useState<OAuthPhase>(() =>
    detectInitialCallback() ? 'returning' : 'idle'
  );
  const [oauthError, setOauthError] = useState<string | null>(null);

  const processedCallbackRef = useRef(false);
  useEffect(() => {
    if (processedCallbackRef.current) return;
    if (!detectInitialCallback()) return;
    processedCallbackRef.current = true;

    handleOAuthCallback(PUBLISHABLE_KEY, getRedirectUri())
      .then(async (result) => {
        // Strip the OAuth params so a refresh doesn't try to re-redeem the code.
        const cleaned = new URL(window.location.href);
        cleaned.search = '';
        window.history.replaceState({}, '', cleaned.toString());

        if (!result.ok) {
          if (result.error === 'canceled') {
            setOauthPhase('idle');
            return;
          }
          console.error('TONE3000 OAuth failed:', result.error);
          setOauthError(result.error);
          setOauthPhase('error');
          return;
        }

        client.setTokens(result.tokens);
        if (result.toneId) {
          const tone = await fetchToneAndModels(result.toneId);
          onToneSelected?.(tone, result.tokens);
        }
        setOauthPhase('idle');
      })
      .catch((err) => {
        console.error('TONE3000 callback error:', err);
        setOauthError(err instanceof Error ? err.message : String(err));
        setOauthPhase('error');
      });
  }, [client, fetchToneAndModels, onToneSelected]);

  /**
   * Kick off (or restart) the Select flow by navigating the main webview to
   * the TONE3000 authorize URL. After the user picks a tone, TONE3000
   * redirects back here and the effect above resolves the rest.
   */
  const startSelectFlow = useCallback(() => {
    if (!PUBLISHABLE_KEY) {
      const msg =
        'TONE3000 publishable key not configured. Set VITE_T3K_PUBLISHABLE_KEY at build time.';
      console.error(msg);
      setOauthError(msg);
      setOauthPhase('error');
      return;
    }
    setOauthError(null);
    startSelectFlowRedirect(PUBLISHABLE_KEY, getRedirectUri(), {
      menubar: true,
      architecture: T3K_ARCHITECTURE,
    }).catch((err) => {
      console.error('Failed to start TONE3000 select flow', err);
      setOauthError(err instanceof Error ? err.message : String(err));
      setOauthPhase('error');
    });
  }, []);

  /** Dismiss the current error state without restarting the flow. */
  const clearOauthError = useCallback(() => {
    setOauthError(null);
    setOauthPhase('idle');
  }, []);

  return {
    startSelectFlow,
    oauthPhase,
    oauthError,
    clearOauthError,
  };
};
