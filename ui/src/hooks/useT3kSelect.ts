import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  PUBLISHABLE_KEY,
  T3K_ARCHITECTURE,
  PREVIEW_PLAYERS_ENABLED,
  getRedirectUri,
} from '../t3k/config';
import {
  T3KClient,
  startSelectFlow as startSelectFlowRedirect,
  startLoginFlow as startLoginFlowRedirect,
  handleOAuthCallback,
} from '../t3k/tone3000-client';
import type { Model, Tone } from '../types/tone';

interface UseT3kSelectOptions {
  /** Called when a selection has been resolved into a Tone (with embedded models). */
  onToneSelected?: (tone: Tone & { models: Model[] }, accessToken: string) => void;
  /** Called whenever a fresh access token is available (initial + refresh). */
  onAccessTokenUpdated?: (accessToken: string) => void;
  /**
   * Called when an OAuth callback resolved with tokens but no tone pick —
   * i.e. the no-prompt login flow finished (or the user closed the Select
   * catalog after signing in). The consumer should open the tone browser.
   */
  onAuthenticated?: () => void;
}

/**
 * Phase of the OAuth Select flow as far as the UI is concerned:
 *   - 'idle'      → no flow in progress; main UI is interactive
 *   - 'leaving'   → a redirect to tone3000.com has been kicked off; the
 *                   current screen should dim + show the busy dots until the
 *                   webview actually navigates away.
 *   - 'returning' → we just landed back on the page with ?code/?error/?canceled
 *                   and are exchanging the code / fetching tone+models. The
 *                   normal UI renders underneath, dimmed by the busy overlay.
 *   - 'error'     → the callback resolved with an error we want to surface;
 *                   the consumer should show a message + a retry affordance.
 */
export type OAuthPhase = 'idle' | 'leaving' | 'returning' | 'error';

/**
 * Why the user was sent to tone3000.com, persisted across the redirect
 * (which remounts React): 'browse' reopens the in-plugin tone browser on
 * return; absent means a plain sign-in that lands on the main screen.
 */
const LOGIN_INTENT_KEY = 't3k.loginIntent';

/**
 * Which flow last navigated to tone3000.com ('select' | 'login' |
 * 'login-browse'). Retry after an error must restart the *same* flow, and
 * errors can surface after a full remount (failed-navigation recovery,
 * callback errors), so the marker lives in sessionStorage rather than a ref.
 */
const LAST_FLOW_KEY = 't3k.lastFlow';

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
 * True on the very first render after returning from a *browse-intent* redirect
 * (Browse CTA / + / swap) that did **not** carry a picked tone — i.e. sign-in
 * only, catalog closed, or canceled. The consumer seeds `showToneBrowser` from
 * this so the in-plugin browser is already mounted under the busy scrim as the
 * callback resolves, instead of the main chain flashing through first. When a
 * tone *was* picked (`tone_id` present) it resolves into the chain, so we leave
 * the browser closed and avoid a reverse flash.
 */
export function shouldRestoreToneBrowser(): boolean {
  if (typeof window === 'undefined') return false;
  if (!detectInitialCallback()) return false;
  if (sessionStorage.getItem(LOGIN_INTENT_KEY) !== 'browse') return false;
  return !new URLSearchParams(window.location.search).has('tone_id');
}

/**
 * Set by native when a webview navigation to tone3000.com failed (offline /
 * site down) and it recovered by reloading the plugin UI — see
 * GuardedWebView::pageLoadHadNetworkError. Detected here so the reload lands
 * on the OAuth error overlay (retry / dismiss) instead of silently on the
 * main screen.
 */
const NAV_ERROR_PARAM = 't3k-nav-error';
const NAV_ERROR_MESSAGE = 'Could not reach TONE3000. Check your internet connection and try again.';

function detectNavErrorRecovery(): boolean {
  if (typeof window === 'undefined') return false;
  return new URLSearchParams(window.location.search).has(NAV_ERROR_PARAM);
}

/**
 * Drives the TONE3000 OAuth flows for the single main webview.
 *
 * Two redirect flows share the callback handling:
 *   - Login (no `prompt`): authenticates the user so the in-plugin tone
 *     browser can query the API directly. The callback carries tokens only;
 *     `onAuthenticated` fires so the consumer can open the browser.
 *   - Select (`prompt=select_tone`): the user browses on tone3000.com; the
 *     callback also carries the picked `tone_id`, which is resolved into a
 *     full tone (+models) and handed to `onToneSelected`.
 *
 * The `oauthPhase` return drives a spinner overlay in the main UI so the user
 * doesn't see an empty React tree flash between landing back and the tone
 * being loaded into the chain.
 */
export const useT3kSelect = ({
  onToneSelected,
  onAccessTokenUpdated,
  onAuthenticated,
}: UseT3kSelectOptions) => {
  const tokenListenerRef = useRef(onAccessTokenUpdated);
  tokenListenerRef.current = onAccessTokenUpdated;
  const authenticatedListenerRef = useRef(onAuthenticated);
  authenticatedListenerRef.current = onAuthenticated;

  const client = useMemo(() => {
    const c = new T3KClient(PUBLISHABLE_KEY, () => {
      // If the refresh token is rejected we lose access — the user has to
      // start from the + again, which relaunches the login flow. Nothing to
      // do automatically here.
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
      // loads). IR and other formats are not NAM architectures — never pass it there.
      const isNamFormat = tone.format?.toLowerCase() === 'nam';
      // Just the first model: native only stores/loads the active model — the
      // detail card's picker pages the full catalog from the API separately.
      const modelsRes = await client.listModels(toneId, {
        pageSize: 1,
        ...(isNamFormat && T3K_ARCHITECTURE !== undefined
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
  const [oauthPhase, setOauthPhase] = useState<OAuthPhase>(() => {
    if (detectNavErrorRecovery()) return 'error';
    return detectInitialCallback() ? 'returning' : 'idle';
  });
  const [oauthError, setOauthError] = useState<string | null>(() =>
    detectNavErrorRecovery() ? NAV_ERROR_MESSAGE : null
  );

  // Strip the recovery marker so a manual refresh doesn't re-show the error.
  useEffect(() => {
    if (!detectNavErrorRecovery()) return;
    const cleaned = new URL(window.location.href);
    cleaned.search = '';
    window.history.replaceState({}, '', cleaned.toString());
  }, []);

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

        // Login intent survives the redirect in sessionStorage: 'browse'
        // (from the + / swap flows) reopens the in-plugin tone browser on
        // return; a plain sign-in (account menu) just lands on the main
        // screen. Consume it regardless of outcome.
        const wantsBrowser = sessionStorage.getItem(LOGIN_INTENT_KEY) === 'browse';
        sessionStorage.removeItem(LOGIN_INTENT_KEY);

        client.setTokens(result.tokens);
        if (result.toneId) {
          const tone = await fetchToneAndModels(result.toneId);
          onToneSelected?.(tone, result.tokens.access_token);
        } else if (wantsBrowser) {
          // Login finished (or Select was closed after sign-in) on the way
          // to browsing tones — open the tone browser.
          authenticatedListenerRef.current?.();
        }
        setOauthPhase('idle');
      })
      .catch((err) => {
        console.error('TONE3000 callback error:', err);
        setOauthError(err instanceof Error ? err.message : String(err));
        setOauthPhase('error');
      });
  }, [client, fetchToneAndModels, onToneSelected]);

  const requireKey = useCallback((): boolean => {
    if (PUBLISHABLE_KEY) return true;
    const msg =
      'TONE3000 publishable key not configured. Set VITE_T3K_PUBLISHABLE_KEY at build time.';
    console.error(msg);
    setOauthError(msg);
    setOauthPhase('error');
    return false;
  }, []);

  /**
   * Kick off (or restart) the Select flow by navigating the main webview to
   * the TONE3000 authorize URL with `prompt=select_tone`. After the user
   * picks a tone, TONE3000 redirects back here and the effect above resolves
   * the rest.
   */
  const startSelectFlow = useCallback(() => {
    if (!requireKey()) return;
    setOauthError(null);
    sessionStorage.setItem(LAST_FLOW_KEY, 'select');
    // Select is always part of a browse: if the user signs in but closes the
    // catalog without picking, return to the in-plugin tone browser.
    sessionStorage.setItem(LOGIN_INTENT_KEY, 'browse');
    // Dim the current screen immediately — the redirect takes a beat.
    setOauthPhase('leaving');
    startSelectFlowRedirect(PUBLISHABLE_KEY, getRedirectUri(), {
      menubar: true,
      architecture: T3K_ARCHITECTURE,
      preview: PREVIEW_PLAYERS_ENABLED,
    }).catch((err) => {
      console.error('Failed to start TONE3000 select flow', err);
      setOauthError(err instanceof Error ? err.message : String(err));
      setOauthPhase('error');
    });
  }, [requireKey]);

  /**
   * Kick off the no-prompt login flow — sign-in only, no tone browsing on
   * tone3000.com. With `openBrowser` (the + / swap flows) the in-plugin tone
   * browser takes over on return; without it (account-menu sign-in) the user
   * lands straight back on the main screen.
   */
  const startLoginFlow = useCallback(
    (options?: { openBrowser?: boolean }) => {
      if (!requireKey()) return;
      setOauthError(null);
      sessionStorage.setItem(LAST_FLOW_KEY, options?.openBrowser ? 'login-browse' : 'login');
      if (options?.openBrowser) sessionStorage.setItem(LOGIN_INTENT_KEY, 'browse');
      else sessionStorage.removeItem(LOGIN_INTENT_KEY);
      // Dim the current screen immediately — the redirect takes a beat.
      setOauthPhase('leaving');
      startLoginFlowRedirect(PUBLISHABLE_KEY, getRedirectUri(), { menubar: true }).catch((err) => {
        console.error('Failed to start TONE3000 login flow', err);
        setOauthError(err instanceof Error ? err.message : String(err));
        setOauthPhase('error');
      });
    },
    [requireKey]
  );

  /**
   * Resolve a tone picked inside the in-plugin browser (card click) exactly
   * like a Select-flow callback: fetch tone + models and hand the result to
   * `onToneSelected` with a fresh access token.
   */
  const selectToneById = useCallback(
    async (toneId: number | string) => {
      const [tone, accessToken] = await Promise.all([
        fetchToneAndModels(toneId),
        client.getAccessToken(),
      ]);
      onToneSelected?.(tone, accessToken);
    },
    [client, fetchToneAndModels, onToneSelected]
  );

  /**
   * Restart whichever flow last navigated to tone3000.com — the error
   * overlay's "Try again". Defaults to Select when nothing is recorded
   * (e.g. sessionStorage was cleared).
   */
  const retryFlow = useCallback(() => {
    const last = sessionStorage.getItem(LAST_FLOW_KEY);
    if (last === 'login') startLoginFlow();
    else if (last === 'login-browse') startLoginFlow({ openBrowser: true });
    else startSelectFlow();
  }, [startLoginFlow, startSelectFlow]);

  /** Dismiss the current error state without restarting the flow. */
  const clearOauthError = useCallback(() => {
    setOauthError(null);
    setOauthPhase('idle');
  }, []);

  return {
    client,
    startSelectFlow,
    startLoginFlow,
    selectToneById,
    retryFlow,
    oauthPhase,
    oauthError,
    clearOauthError,
  };
};
