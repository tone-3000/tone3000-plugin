# The UI (`plugin/ui`)

The plugin's editor, in JUCE/C++ (`namespace t3k::ui`): every screen, drawn
and laid out natively, no browser engine. The design and the decisions
behind it are in [`../docs/native-ui.md`](../docs/native-ui.md); this file
is the working guide: how to build it, how to test it, where things go.

## Build

The plugin build compiles this tree into every format; nothing extra to
install. Configuration (`T3K_PUBLISHABLE_KEY`, `T3K_API_DOMAIN`,
`T3K_UPDATE_NOTICE`) is read at CMake configure time from the repo-root
`.env` and `.env.local`, with the configure environment overriding both,
into the generated `T3kConfig.h` (`NativeUi.cmake`). See
[`.env.example`](../../.env.example).

### The testbed

`testbed/` is a standalone JUCE app that renders `PluginRoot` over a
fixture-driven mock backend and mock TONE3000 session. Its own project keeps
UI iteration from rebuilding the DSP:

```sh
cmake -S plugin/ui/testbed -B build-ui -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ui -j
UI=build-ui/UiTestbed_artefacts/Debug/UiTestbed.app/Contents/MacOS/UiTestbed   # macOS path

$UI --scenario main-mono [--live]            # interactive window on one scenario (--live: moving signal)
$UI --capture out [--ref beforeDir] [filter…] # 2x PNG per scenario (+ diff table against an earlier run); fails on unnamed Tab stops
$UI --compare before.png after.png [diff.png] # one pair
$UI --selftest                               # unit tests: pure logic + the focus policy in a real window
$UI --bench [--seconds 10] [--json out.json] [phase…]   # CPU / memory under load (--list for the phases)
```

### Measuring load

`--bench` runs the UI under a realistic load, one phase at a time. Each phase
gets its own window, and `MockSignal` feeds the mock backend a deterministic
guitar-like signal: note attacks and decays, rests, an occasional clipping
hit, a harmonic spectrum and a drifting pitch. That signal drives the
meters, tile glows, clip LEDs, the EQ curve's live spectrum and the tuner.
The interaction phases send input through the window's peer, the way the OS
delivers it: dragging an EQ band dot, dragging a knob, trackpad-scrolling
the tone browser, and a click tour across the screens. Each phase reports:

- process and UI-thread CPU, as a percentage of one core;
- paint passes per second, and repainted area in whole windows per second;
- stalls, meaning message-loop gaps over 50 ms;
- memory (macOS physical footprint, Linux RSS, Windows working set).

A workload that never takes effect (its target isn't found, or the knob or
grid never moves) is flagged on its row and fails the run. Build the testbed
as Release when measuring (`-B build-ui-release -DCMAKE_BUILD_TYPE=Release`),
since Debug numbers mostly measure JUCE's assertions. `--live` gives the
interactive window the same signal. Run it before and after a rendering
change with the mouse left alone and the window unobscured (a locked or
sleeping display drops the synthetic input and skews the paint costs), and
expect ±0.5% run-to-run noise; `--json` keeps the numbers for a diff.

`-DT3K_BUILD_UI_TESTBED=ON` on the plugin build adds the same target and
registers `--selftest` with ctest (CI runs it next to the DSP suite).

Scenario ids come from `testbed/fixtures/scenarios.json`: each is a mock
backend state (chain, session, banners, settings) plus, where a screen needs
interaction to reach, the drive steps in `testbed/Scenarios.cpp`; the
capture table flags ids whose drive is missing. The fixtures are the
testbed's own and are edited in place (`fixtures/img` holds the artwork the
mock image host serves).

### Visual regression

Captures are compared against captures, before a change and after it:

```sh
git stash                    # or check out main
$UI --capture /tmp/t3k-before
git stash pop
$UI --capture /tmp/t3k-after --ref /tmp/t3k-before
```

Every row of the table should read 0.00 % except the scenarios the change
touched; a diff image (`<id>.diff.png`) lands next to each capture that
differs. The worst-tile figure is the one that catches a shifted control:
one line of misplaced text is a rounding error globally but lights up its
64px tile. Text and JPEG artwork rasterise identically run to run on one
machine, so anything non-zero is real. New screens with nothing to diff
against are reviewed by eye against the Figma mockups.

## Layout

```
plugin/ui/
  NativeEditor.*      AudioProcessorEditor: owns UiPrefs, HttpClient, Tone3000Session,
                      Services, PluginRoot; scales the 1024-wide design space
  NativeUi.cmake      t3k_add_native_ui(<target>), T3kConfig.h, embedded assets
  assets/             Roboto Mono, Arimo (Arial stand-in), brand SVGs (UiBinaryData)
  core/               no JUCE components: Theme, Fonts, Icons (+ generated
                      LucideIcons.h, CustomIcons, GearGlyphs), Design, Paint,
                      TextFlow / RichText (wrapped and styled text), Tween /
                      AlphaTween (vblank-driven, via juce_animation),
                      DelayedCall, AsyncScope, Result, Help
                      (hint strings), Labels, Pitch, EqMath, KnobScale,
                      MeterScale, MidiCatalog, Alerts, Blur, Bitmap (photos
                      resampled once at device density), Wheel
  model/              juce::var → structs: ChainState, Tone, AudioDeviceState,
                      MidiMapState (VarReader); ToneQuery (the browser's
                      filters → the API query string)
  backend/            ui::Backend (everything the UI asks of the processor)
                      and ProcessorBackend over TONE3000Processor
  services/           Services (one bundle per editor) and its members
                      (BrowserState: what the tone browser keeps between visits):
                      UiClock (the one 30 Hz tick every poller shares),
                      ChainStore, MeterStore, PresetStore, AudioDeviceStore,
                      MidiMapStore, UiPrefs, HintBus, Toast, Banners,
                      ParamBinding, AutoMeasure, SpectrumFeed, TunerFeed,
                      ModelLoads, LocalFiles, ImageLoader, ConnectionGate,
                      UpdateCheck, ToneLoadFlow, Zoom, Pointer (touch or
                      mouse, read off the input at run time); the TONE3000 stack:
                      HttpClient, OAuth (PKCE), LoopbackServer,
                      Tone3000Client, ToneSession / Tone3000Session
  widgets/            reusable controls that know nothing about services
                      (Knob, PillButton, Popover, ContextMenu, ModalLayer,
                      DbMeter, DotMeter, TextField, DragScroller, …);
                      widgets/form/ is the
                      settings form kit (FormItem layout, rows, controls)
  vendor/             third-party code carried in-tree, each with its licence:
                      qrcodegen (Nayuki's QR Code generator, MIT)
  views/              screens, wiring widgets to services: PluginRoot,
                      PluginHeader, Faceplate, MainScreen, TunerView,
                      SignInScreen, …
    gallery/          ChainView, GalleryLane, ToneTile, AddTile, StereoPanRail
    block/            BlockDetail, BlockCard, BlockInfoPanel, BlockEqView
    browser/          ToneBrowser (the Select tone takeover: search + FilterBar over
                      the card grid, Paginator pinned under it), FilterBar /
                      FilterChip / FilterMenu, ToneCard, Paginator, BrowserPrompt
    settings/         SettingsScreen, PluginSettingsPage, SystemSettingsPage,
                      MidiMapSection, …
    modals/           ConnectionModal, UpdateNotice
  testbed/            UiTestbed: Main (--capture/--compare/--selftest/--bench),
                      Host (the root over the mocks, fitted like NativeEditor),
                      MockBackend, MockSession, MockSignal (the bench's moving
                      signal), Scenarios (drives), Drive (find/click/drag
                      helpers + the a11y audit), Compare, Bench, ProcessStats,
                      SelfTests, fixtures/
script/gen-lucide-icons.mjs   regenerates core/LucideIcons.h from the Lucide icon set
```

One screen element is one C++ component. Each `.h` opens with a comment
saying what the component is and the layout facts that fixed its numbers.
Many of those headers name the React component the numbers were ported
from (`port of KnobControl.tsx`); that is lineage, recorded in
`../docs/native-ui.md` §6, not a pointer to living code.

## Conventions

- **Design space.** Everything is laid out in the 1024 × 578 design box;
  `NativeEditor` applies one `AffineTransform`. Never scale by hand. The one
  view that holds its size on screen instead (the tone browser's body, so a
  bigger window shows more results) counter-scales by `Services::zoom`, the
  factor the shell publishes on every fit; a `Popover` adopts its anchor's
  scale, so menus opened from it are 1x too.
- **Ownership.** `NativeEditor` → `Services` → `PluginRoot` → views. Views
  hold references to the services they use and register as listeners in
  their constructor, deregister in their destructor. No singletons, no
  globals beyond the constexpr tables in `core/`.
- **Data flow.** Stores are the only callers of `Backend`; views subscribe
  to stores, read plain structs, call store actions. Optimistic edits live
  in the store, reconciled on the next revision. A store action refreshes
  and notifies synchronously, so a view's `chainChanged` runs inside the
  click that caused it: a sync must never rebuild the control whose handler
  is on the stack (re-select it instead), or the handler's closure is freed
  under it (see `BlockCard::syncHeader`).
- **Async.** Anything that lands later goes through `AsyncScope::wrap` (or
  `juce::Component::SafePointer`) so a closed editor never gets a callback.
  HTTP runs on `HttpClient`'s pool, images on the same pool, the OAuth
  listener on its own thread; all deliver on the message thread.
- **Text.** Line boxes come from `Fonts::normalLineHeight` and
  `Fonts::cssBaseline` (the design's line-height and baseline rules), with
  `TextFlow` for wrapping/clamping/ellipsis and `RichText` for mixed runs and
  links. Fractional layout positions are kept and snapped only at draw time
  (`FormItem::subpixelTop`, `TextFlow::draw`).
- **Focus and keys.** Nothing is focused until Tab or an explicit `focus()`
  / `grabKeyboardFocus()`; a click focuses only text fields, so the host's
  Space / Enter keep working after mouse work. Every button derives from
  `widgets/Clickable` (Tab-focusable, never by click, named for screen
  readers from its text or help lead); a new focusable control follows the
  same two `set…KeyboardFocus` calls and gives itself a name (`setTitle`,
  button text or a "Name: …" help hint). `PluginRoot::FocusPolicy` owns the
  rest (Tab from nothing, Escape / press elsewhere blur). Decorative
  components call `setAccessible(false)`; status that only paints elsewhere
  goes through `help::announce()`. See native-ui.md §5.8a.
- **Pixels.** New or changed visuals get a scenario (an entry in
  `testbed/fixtures/scenarios.json`) and a matching drive in `Scenarios.cpp`
  if it needs one. The before/after capture diff is the acceptance test;
  a new screen is reviewed by eye against the Figma mockups.
- **Logic.** Pure logic (parsers, state machines, math) is tested in
  `testbed/SelfTests.cpp`, one `juce::UnitTest` per file it covers.
- **Icons.** Lucide glyphs come from `script/gen-lucide-icons.mjs` (add the
  name to its `ICONS` list, run it, use `Icon::<Name>`) and are never
  hand-edited; brand and gear artwork are in `CustomIcons` / `GearGlyphs`
  with their SVG source noted.
- **Style.** Two files per component, CamelCase, `t3k::ui`; `-Wshadow`
  clean; comments explain the *why*, and cite the design rule a number
  comes from.

## Sign-in

OAuth (PKCE) runs in the system browser. `Tone3000Session::login` starts
`LoopbackServer` on `127.0.0.1:<ephemeral>`, opens the authorize URL with
`redirect_uri=http://localhost:<port>/`, and the plugin shows the
`SignInScreen` for as long as the session's `AuthFlow` is not idle: a full
page in the tone browser's slot (Select Tone mockups 13342:46017 and
13342:46055) with a bare ← that abandons the sign-in, the loading dots over
"Sign in with your browser", and under them the two ways round a browser
that did not open. Every entry point (account menu, info panel, the tone
browser's CTAs) shows this same screen; only the landing differs: a login
started from the tone browser (`LoginIntent::browse`) comes back to it,
the rest land on the chain.

The browser launch is best effort — JUCE reports success once the launcher
forks, so a browser that refuses to start (Chromium under root on Linux)
gives no error — hence the fallbacks, always offered:

- **Copy Link** ("Browser didn't open? Copy link to sign in, then return
  here.") puts the authorize URL (`AuthFlow::authorizeUrl`) on the
  clipboard, for any browser on this machine: the loopback listener takes
  the redirect from whichever browser completes it.
- **Sign in on a different device** (a grey text link; hand cursor, no
  hover state) runs the device flow (RFC 8628, `startDeviceFlow`) on its
  own page, "Sign in on your phone": `POST /oauth/device_authorization`,
  then two cards with OR between them, a QR code (`widgets/QrCode`) of
  `verification_uri_complete`, and "Visit tone3000.com/activate, sign in,
  and enter this code:" over the `user_code` (the address is underlined so
  it reads as one, but is not a link: it is for typing into the other
  device), while the session polls `/oauth/token` with the device-code
  grant at the server's interval (`slow_down` adds 5 s; `expired_token`,
  `access_denied` and the code's own deadline end it with a reason and a
  New code button).

The launch is attempted on every platform regardless; only a launcher that
refuses outright is reported (`AuthFlow::browserProblem`, the copy says
so), and a browser that starts and dies simply leaves the user on the
fallbacks. All paths feed the same token store: the first to finish signs
in and the others are dropped. The redirect is checked against the PKCE `state` and
exchanged for tokens (`Tone3000Client`, persisted in `UiPrefs`, refreshed
transparently with a single in-flight refresh and one 401 retry). Closing
the editor stops the listener and the polling; a stale callback is ignored.

## The tone browser (Select tone)

The screen takes over everything under the header (meters, chain and
faceplate; `PluginRoot` mounts it only while open and hides what it
covers). Searching needs a session. Signed out, the screen is a preview
instead: the gear chips alone (`FilterBar::setGearOnly`) over TONE3000's
trending feed (`ToneSession::listTrending`, `GET /tones/trending[?gear=]`,
which takes no token; the feed's one filter is the gear, and the pick is
the same `ToneQuery::gear` so it carries into the search after sign-in),
with a `BrowserPrompt` CTA under the cards ("Discover a zillion more tones."
/ "Sign in or create free account"). A card click opens the sign-in page
(the bare `←` back to the cards, the prompt centred); either button runs the
browse-intent login, which lands back on this same screen signed in
(`browser-signed-out*`). Signed in, `ToneBrowser` pins a search box and a `FilterBar`
above the card grid and the `Paginator` below it; the grid scrolls between
the two, fading out under each, and asks `ToneSession::searchTones` for one
page at a time. The ← row zooms with the window; the body under it does
not: it is laid out in screen pixels under the zoom, the search box and
filter row keep their 1x height and widen with the column, the cards keep
their 1x height and widen to fill two columns, and go three-up once three
fit at the default width (`browser-zoom-wide`, `browser-zoom-three-up`,
`browser-zoom-menu`; a scenario's `zoom` sizes the testbed window).

- `BrowserState` (`services/`, one per editor) is what the screen keeps
  between visits: the `ToneQuery`, whether the filter row is unfolded, the
  page and the page's results (flagged when they are the signed-out
  trending feed, so a return across a sign-in or out fetches instead). The
  browser is mounted only while open, so coming back renders the last page
  at once with no fetch; it refreshes on the next search, filter change or
  page turn. Closing the editor forgets it.
- `ToneQuery` (model) is the one place the filters live: text, sort, gear,
  format, tags / makes / creators, calibrated, verified, profile. It builds
  the API path itself (`requestPath`): `/tones/search` with the query string,
  or `/tones/{downloaded,favorited,created}` with the title search and gear
  alone when a profile filter is set. The plugin's NAM architecture always rides along (the API
  ignores it for IR, and omitting it falls back to a legacy A1-only default).
  The default sort (best match with text, else trending) is stored as no
  pick, so Trending never reads as a filter. Unit-tested in `SelfTests.cpp`.
- `FilterBar` edits the state's query through one horizontally scrolling row
  of chips (`FilterChip`): the filters button, then (once unfolded, pushing
  the rest right) Sort, Format, Tags, Makes, Creators, Calibrated, a
  divider, then verified, the profile chip (the user's avatar alone until a
  profile is picked) and the gear chips. A chip holding a value shows it
  with an × that clears it (Sort: back to the default); the filters button
  carries a dot while any of the unfolded ones are set. A profile filter
  parks the catalog-only controls (dimmed, hint says why) without losing
  their values; the search box stays live, matching titles within the
  stream; IR gear (cabinet, space) or the IR
  format parks Calibrated the same way, and the flag stays out of the
  request (`ToneQuery::calibratedInForce`).
- `FilterMenu` is the dropdown (`Popover`). Single-pick menus (Sort, Format,
  Profile) show the current value in white; multi-pick ones add a check
  column. Rows may lead with an icon (Favorites' bookmark) or a creator
  avatar. Taxonomy menus add a search field and look their rows up from
  `ToneSession::listTaxonomy` (debounced, a newer lookup cancels the one in
  flight). Any pick closes the menu.
- The search box submits on Enter (or its ×, or Escape, which clear it);
  every filter change fetches page 1 at once. An older page arriving after
  a newer one is dropped.
