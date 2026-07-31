/**
 * tone3000-client.ts — TONE3000 OAuth + API client.
 *
 * Adapted from the reference implementation at https://github.com/tone-3000/api
 * (`src/tone3000-client.ts`). Trimmed to the surface this plugin needs:
 *   - Select flow (prompt=select_tone) initiator + callback handler
 *   - PKCE generation
 *   - Token refresh
 *   - Authenticated fetch helpers for the tone + models endpoints
 *
 * Tokens live in localStorage on the WebView's `juce.backend` origin so a
 * signed-in user stays signed in across editor sessions (the refresh token is
 * long-lived; access tokens refresh transparently). The PKCE verifier/state
 * are mid-flow-only values and stay in sessionStorage.
 */

import { T3K_API } from './config';
import type { Tone, Model, PaginatedResponse, User } from '../types/tone';

export interface T3KTokens {
  access_token: string;
  refresh_token: string;
  /** Unix timestamp (ms) when the access token expires. */
  expires_at: number;
}

export type OAuthCallbackResult =
  | { ok: true; tokens: T3KTokens; toneId?: string; canceled?: boolean }
  | { ok: false; error: string };

// ─── PKCE helpers ─────────────────────────────────────────────────────────────

const PKCE_CODE_VERIFIER_KEY = 't3k_code_verifier';
const PKCE_STATE_KEY = 't3k_state';

function randomBase64url(bytes: number): string {
  const buf = crypto.getRandomValues(new Uint8Array(bytes));
  return btoa(String.fromCharCode(...buf))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=/g, '');
}

async function sha256Base64url(input: string): Promise<string> {
  const hash = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(input));
  return btoa(String.fromCharCode(...new Uint8Array(hash)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=/g, '');
}

async function buildPkceParams(): Promise<{
  codeVerifier: string;
  codeChallenge: string;
  state: string;
}> {
  const codeVerifier = randomBase64url(32);
  const [codeChallenge, state] = await Promise.all([
    sha256Base64url(codeVerifier),
    Promise.resolve(randomBase64url(16)),
  ]);
  sessionStorage.setItem(PKCE_CODE_VERIFIER_KEY, codeVerifier);
  sessionStorage.setItem(PKCE_STATE_KEY, state);
  return { codeVerifier, codeChallenge, state };
}

function buildAuthorizeUrl(
  publishableKey: string,
  redirectUri: string,
  extra: Record<string, string>,
  pkce: { codeChallenge: string; state: string }
): string {
  const url = new URL(`${T3K_API}/api/v1/oauth/authorize`);
  url.searchParams.set('client_id', publishableKey);
  url.searchParams.set('redirect_uri', redirectUri);
  url.searchParams.set('response_type', 'code');
  url.searchParams.set('code_challenge', pkce.codeChallenge);
  url.searchParams.set('code_challenge_method', 'S256');
  url.searchParams.set('state', pkce.state);
  for (const [k, v] of Object.entries(extra)) url.searchParams.set(k, v);
  return url.toString();
}

// ─── Flow initiators ──────────────────────────────────────────────────────────

/**
 * Select Flow — send the user to tone3000.com to browse and pick a tone.
 *
 * Replaces the current page with the TONE3000 authorize URL. After the user
 * picks a tone (or closes the menubar), TONE3000 redirects back to
 * `redirectUri` with `code`, `state`, and `tone_id` (or `canceled=true`).
 *
 * @param options.gears        underscore-separated gear filter (e.g. 'amp_pedal')
 * @param options.format       single model-format filter (e.g. 'nam', 'ir')
 * @param options.architecture model architecture filter — restricts the
 *                             catalog to tones whose models match this
 *                             architecture. The plugin passes `2` because it
 *                             only supports v2 NAM architectures at runtime.
 * @param options.menubar      show TONE3000's in-flow menubar with a close button
 */
export async function startSelectFlow(
  publishableKey: string,
  redirectUri: string,
  options?: {
    gears?: string;
    format?: string;
    architecture?: string | number;
    menubar?: boolean;
    loginHint?: string;
  }
): Promise<void> {
  const pkce = await buildPkceParams();
  const extra: Record<string, string> = { prompt: 'select_tone' };
  if (options?.gears) extra.gears = options.gears;
  if (options?.format) extra.format = options.format;
  if (options?.architecture !== undefined) extra.architecture = String(options.architecture);
  if (options?.menubar) extra.menubar = 'true';
  if (options?.loginHint) extra.login_hint = options.loginHint;
  window.location.href = buildAuthorizeUrl(publishableKey, redirectUri, extra, pkce);
}

/**
 * Login Flow — standard OAuth authorization with no `prompt`. The user signs
 * in on tone3000.com (no tone browsing) and is redirected straight back with
 * a `code` to exchange for tokens. Used to authenticate before showing the
 * in-plugin tone browser.
 */
export async function startLoginFlow(
  publishableKey: string,
  redirectUri: string,
  options?: { loginHint?: string; menubar?: boolean }
): Promise<void> {
  const pkce = await buildPkceParams();
  const extra: Record<string, string> = {};
  if (options?.loginHint) extra.login_hint = options.loginHint;
  if (options?.menubar) extra.menubar = 'true';
  window.location.href = buildAuthorizeUrl(publishableKey, redirectUri, extra, pkce);
}

// ─── Callback handler ─────────────────────────────────────────────────────────

/**
 * Handle the OAuth callback after TONE3000 redirects back to your redirect URI.
 *
 * Parses `code` / `state` / `tone_id` / `error` / `canceled` from
 * `window.location.search`, verifies state, exchanges the code for tokens, and
 * returns a typed result. Always check `result.ok` before using the tokens.
 */
export async function handleOAuthCallback(
  publishableKey: string,
  redirectUri: string
): Promise<OAuthCallbackResult> {
  const params = new URLSearchParams(window.location.search);
  const code = params.get('code');
  const error = params.get('error');
  const returnedState = params.get('state');
  const toneId = params.get('tone_id') ?? undefined;
  const canceled = params.get('canceled') === 'true';

  const storedState = sessionStorage.getItem(PKCE_STATE_KEY);
  const codeVerifier = sessionStorage.getItem(PKCE_CODE_VERIFIER_KEY);

  // PKCE values are single-use — clear them regardless of outcome.
  sessionStorage.removeItem(PKCE_STATE_KEY);
  sessionStorage.removeItem(PKCE_CODE_VERIFIER_KEY);

  if (returnedState !== storedState) return { ok: false, error: 'state_mismatch' };
  if (canceled && !code) return { ok: false, error: 'canceled' };
  if (error) return { ok: false, error };
  if (!code || !codeVerifier) return { ok: false, error: 'missing_code' };

  const res = await fetch(`${T3K_API}/api/v1/oauth/token`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      code,
      code_verifier: codeVerifier,
      redirect_uri: redirectUri,
      client_id: publishableKey,
    }),
  });

  if (!res.ok) {
    const err = (await res.json().catch(() => ({}))) as { error?: string };
    return { ok: false, error: err.error ?? 'token_exchange_failed' };
  }

  const data = (await res.json()) as {
    access_token: string;
    refresh_token: string;
    expires_in: number;
  };
  const tokens: T3KTokens = {
    access_token: data.access_token,
    refresh_token: data.refresh_token,
    expires_at: Date.now() + data.expires_in * 1000,
  };

  return { ok: true, tokens, toneId, ...(canceled ? { canceled: true } : {}) };
}

// ─── Token refresh ────────────────────────────────────────────────────────────

export async function refreshTokens(
  refreshToken: string,
  publishableKey: string
): Promise<T3KTokens> {
  const res = await fetch(`${T3K_API}/api/v1/oauth/token`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      grant_type: 'refresh_token',
      refresh_token: refreshToken,
      client_id: publishableKey,
    }),
  });

  if (!res.ok) throw new Error('token_refresh_failed');

  const data = (await res.json()) as {
    access_token: string;
    refresh_token: string;
    expires_in: number;
  };
  return {
    access_token: data.access_token,
    refresh_token: data.refresh_token,
    expires_at: Date.now() + data.expires_in * 1000,
  };
}

// ─── Authenticated API client ─────────────────────────────────────────────────

const STORAGE_KEY = 't3k_tokens';

/**
 * T3KClient — authenticated API client with proactive token refresh.
 *
 * Tokens are persisted in localStorage so they survive the OAuth redirect
 * round-trip, WebView reloads, and editor restarts — the in-plugin tone
 * browser can open instantly in later sessions without re-running the OAuth
 * redirect. After the user comes back from tone3000.com, call `setTokens()`
 * once with the freshly exchanged tokens to seed the client.
 */
export class T3KClient {
  private refreshPromise: Promise<T3KTokens> | null = null;
  private onTokensUpdated?: (tokens: T3KTokens) => void;
  private readonly publishableKey: string;
  private readonly onAuthRequired: () => void;

  constructor(publishableKey: string, onAuthRequired: () => void) {
    this.publishableKey = publishableKey;
    this.onAuthRequired = onAuthRequired;
  }

  /**
   * Subscribe to token changes — fires for both `setTokens()` calls and
   * automatic refreshes. Use this to keep an external store (e.g. the
   * native-side Bearer token in C++) in sync with the latest access token.
   */
  setTokenListener(listener: (tokens: T3KTokens) => void): void {
    this.onTokensUpdated = listener;
  }

  setTokens(tokens: T3KTokens): void {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(tokens));
    this.onTokensUpdated?.(tokens);
  }

  getTokens(): T3KTokens | null {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? (JSON.parse(raw) as T3KTokens) : null;
  }

  clearTokens(): void {
    localStorage.removeItem(STORAGE_KEY);
  }

  /**
   * Full sign-out on this client: drops the persisted token set and any
   * mid-flight PKCE state so the next + press starts a fresh login flow.
   * (Native's copy of the access token is cleared separately by the caller.)
   */
  logout(): void {
    this.clearTokens();
    sessionStorage.removeItem(PKCE_CODE_VERIFIER_KEY);
    sessionStorage.removeItem(PKCE_STATE_KEY);
  }

  /**
   * Returns a valid access token, refreshing it if it is within 60s of expiry.
   * Multiple concurrent callers share a single in-flight refresh.
   */
  async getAccessToken(): Promise<string> {
    const tokens = this.getTokens();
    if (!tokens) {
      this.onAuthRequired();
      throw new Error('not_authenticated');
    }

    if (Date.now() > tokens.expires_at - 60_000) {
      if (!this.refreshPromise) {
        this.refreshPromise = refreshTokens(tokens.refresh_token, this.publishableKey)
          .then((t) => {
            this.setTokens(t);
            this.refreshPromise = null;
            return t;
          })
          .catch((err) => {
            this.clearTokens();
            this.refreshPromise = null;
            this.onAuthRequired();
            throw err;
          });
      }
      return (await this.refreshPromise).access_token;
    }

    return tokens.access_token;
  }

  async fetch(path: string, init?: RequestInit): Promise<Response> {
    const token = await this.getAccessToken();
    const res = await globalThis.fetch(`${T3K_API}${path}`, {
      ...init,
      headers: { ...init?.headers, Authorization: `Bearer ${token}` },
    });

    // Retry once on 401 to handle the rare race between expiry check and request.
    if (res.status === 401) {
      const stored = this.getTokens();
      if (stored) {
        this.setTokens({ ...stored, expires_at: 0 });
        const retryToken = await this.getAccessToken();
        return globalThis.fetch(`${T3K_API}${path}`, {
          ...init,
          headers: { ...init?.headers, Authorization: `Bearer ${retryToken}` },
        });
      }
    }

    return res;
  }

  /** True when a token set is present (it may still be refreshed on use). */
  isAuthenticated(): boolean {
    return this.getTokens() !== null;
  }

  /** The currently authenticated user (avatar + username for the header). */
  async getUser(): Promise<User> {
    const res = await this.fetch('/api/v1/user');
    if (!res.ok) throw new Error(`getUser failed: ${res.status}`);
    return res.json();
  }

  async getTone(id: number | string): Promise<Tone> {
    const res = await this.fetch(`/api/v1/tones/${id}`);
    if (!res.ok) throw new Error(`getTone failed: ${res.status}`);
    return res.json();
  }

  /** Shared pagination for the session-scoped tone streams; each accepts an
      optional single-value `gear` filter (the tone browser's filter pills). */
  private async listTones(
    endpoint: 'created' | 'favorited' | 'downloaded',
    options?: { page?: number; pageSize?: number; gear?: string }
  ): Promise<PaginatedResponse<Tone>> {
    const qs = new URLSearchParams({
      page: String(options?.page ?? 1),
      page_size: String(options?.pageSize ?? 10),
    });
    if (options?.gear) qs.set('gear', options.gear);
    const res = await this.fetch(`/api/v1/tones/${endpoint}?${qs.toString()}`);
    if (!res.ok) throw new Error(`list ${endpoint} failed: ${res.status}`);
    return res.json();
  }

  listCreatedTones(options?: { page?: number; pageSize?: number; gear?: string }) {
    return this.listTones('created', options);
  }

  listFavoritedTones(options?: { page?: number; pageSize?: number; gear?: string }) {
    return this.listTones('favorited', options);
  }

  listDownloadedTones(options?: { page?: number; pageSize?: number; gear?: string }) {
    return this.listTones('downloaded', options);
  }

  /**
   * Fetch that attaches a Bearer token when a session exists but degrades to
   * a plain anonymous request otherwise, for the handful of endpoints the
   * API documents as auth-optional (e.g. Trending Tones) — signed-out users
   * still get a public teaser feed instead of being routed through
   * `onAuthRequired`.
   */
  private async fetchOptionalAuth(path: string, init?: RequestInit): Promise<Response> {
    if (!this.getTokens()) return globalThis.fetch(`${T3K_API}${path}`, init);
    return this.fetch(path, init);
  }

  /** Top 10 trending tones, the same feed as the homepage's trending lanes
      (homepage feed; not paginated). Optionally filtered by gear. Auth is
      optional: signed-in users get their session automatically; signed-out
      users still get the public feed as a discovery teaser. */
  async listTrendingTones(gear?: string): Promise<{ data: Tone[] }> {
    const qs = gear ? `?gear=${encodeURIComponent(gear)}` : '';
    const res = await this.fetchOptionalAuth(`/api/v1/tones/trending${qs}`);
    if (!res.ok) throw new Error(`listTrendingTones failed: ${res.status}`);
    return res.json();
  }

  /** 10 most recently published tones (homepage feed; not paginated). */
  async listLatestTones(): Promise<{ data: Tone[] }> {
    const res = await this.fetch('/api/v1/tones/latest');
    if (!res.ok) throw new Error(`listLatestTones failed: ${res.status}`);
    return res.json();
  }

  /**
   * List models for a tone. Pass `architecture` (e.g. `2` for NAM v2) only for
   * `format=nam` tones; omit for IR and other formats.
   */
  async listModels(
    toneId: number | string,
    options?: { page?: number; pageSize?: number; architecture?: string | number }
  ): Promise<PaginatedResponse<Model>> {
    const qs = new URLSearchParams({
      tone_id: String(toneId),
      page: String(options?.page ?? 1),
      page_size: String(options?.pageSize ?? 100),
    });
    if (options?.architecture !== undefined) {
      qs.set('architecture', String(options.architecture));
    }
    const res = await this.fetch(`/api/v1/models?${qs.toString()}`);
    if (!res.ok) throw new Error(`listModels failed: ${res.status}`);
    return res.json();
  }
}
