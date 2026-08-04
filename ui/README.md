# TONE3000 Plugin UI

React + TypeScript frontend for the TONE3000 plugin. The build output goes to
`../plugin/webview/`, where CMake embeds it as JUCE binary data and the plugin
serves it in a native WebView (WebView2 on Windows, WebKit elsewhere).

## Development

```sh
npm install
npm run dev        # Vite dev server at http://localhost:5173
npm run build      # typecheck + build into ../plugin/webview
npm run lint       # eslint
npm run format     # prettier
```

Set `VITE_T3K_PUBLISHABLE_KEY` in `ui/.env` before building or running the
dev server (see the root README for redirect URI setup).

The dev server is useful for layout and TONE3000 browsing work, but anything
that calls into the plugin (parameters, chain state, meters) needs the real
JUCE backend, so the usual loop is `npm run build` followed by a plugin build.

## How it talks to the plugin

`juce-framework-frontend` (from the JUCE tree in `../libs`) provides three
primitives, wrapped by `src/backend/JuceBackend.ts`:

- **Parameters**: slider/toggle/combo state bound to APVTS parameters, used
  through `src/hooks/useParameter.ts`.
- **Native functions**: request/response calls into C++ (registered in
  `plugin/src/EditorWebViewSetup.cpp`), used through
  `src/hooks/useFunction.ts`.
- **Events**: pushed from C++ (chain updates, meters, tuner frames).

The native side is the source of truth. React holds UI-only state; anything
persistent lives in the processor and comes back through events or getters.

## TONE3000 integration

`src/t3k/tone3000-client.ts` is the API client (adapted from the
[TONE3000 API examples](https://github.com/tone-3000/api)). The select/login
flows are OAuth 2.0 + PKCE and run in the same WebView as the UI: we navigate
to the authorize URL, TONE3000 redirects back to `index.html?code=...`, React
remounts and exchanges the code, then hands the access token to native via
`setAccessToken` so C++ downloads can send `Authorization: Bearer` headers.
Session logic lives in `src/hooks/useToneSession.ts`, and the add/swap tone
flows in `src/hooks/useToneLoadFlow.ts`.

## Layout

| Path              | Contents                                             |
| ----------------- | ---------------------------------------------------- |
| `src/components/` | UI components; `Plugin.tsx` is the root orchestrator |
| `src/hooks/`      | Parameter, chain, session, and utility hooks         |
| `src/backend/`    | The JUCE bridge                                      |
| `src/t3k/`        | TONE3000 API client and config                       |
| `src/types/`      | Shared types and ambient declarations                |

Styling is inline styles with shared design tokens in
`src/components/theme.ts`; global resets live in `src/index.css`.
