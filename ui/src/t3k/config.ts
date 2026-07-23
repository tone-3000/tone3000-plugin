// TONE3000 API configuration.
//
// PUBLISHABLE_KEY: your `t3k_pub_…` key from Settings → API Keys on tone3000.com.
//   Bake one in at build time via `VITE_T3K_PUBLISHABLE_KEY`. Required for the
//   Select flow to work — tone3000.com rejects authorize requests without a
//   recognised client_id.
//
// T3K_API: API origin. Defaults to production; override with `VITE_T3K_API_DOMAIN`
//   when pointing at a staging deployment. Trailing slashes are stripped because
//   Vercel issues 308 redirects on `//api/...` paths and redirects drop CORS.
//
// REDIRECT_URI: where TONE3000 sends the user back after the OAuth flow.
//   We compute it at runtime from the page that the main webview is loaded at,
//   so it stays correct in both dev (`http://localhost:5173/`) and production
//   builds where JUCE serves the webview through its resource provider
//   (`juce://juce.backend/index.html` on macOS/Linux,
//   `https://juce.backend/index.html` on Windows). Each of those origins must
//   be added to the publishable key's allowed redirect URIs in TONE3000
//   Settings → API Keys (localhost is auto-allowed in dev).

export const T3K_API = (
  (import.meta.env.VITE_T3K_API_DOMAIN as string | undefined) ?? 'https://www.tone3000.com'
).replace(/\/+$/, '');

export const PUBLISHABLE_KEY =
  (import.meta.env.VITE_T3K_PUBLISHABLE_KEY as string | undefined) ?? '';

// UPDATE_NOTICE_ENABLED: startup update check (see useUpdateNotice). Off by
//   default so forks never ping tone3000.com; enable with
//   `VITE_T3K_UPDATE_NOTICE=true`. The endpoint lives on the same origin as
//   everything else (T3K_API), so a staging override redirects it too.
export const UPDATE_NOTICE_ENABLED =
  (import.meta.env.VITE_T3K_UPDATE_NOTICE as string | undefined) === 'true';

export const UPDATE_CHECK_URL = `${T3K_API}/api/v1/plugin/version`;

// Model-architecture `2` — passed to the Select OAuth URL and to `GET /api/v1/models`
// only when the tone is format=nam. IR and other formats omit the list-models filter.
// Hardcoded because the plugin runtime only loads v2 NAM weights.
// TEMP: set to `undefined` to disable both filters while testing.
export const T3K_ARCHITECTURE: number | undefined = 2;

export function getRedirectUri(): string {
  return window.location.origin + window.location.pathname;
}
