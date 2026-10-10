# The UI (JUCE/C++): design record

The plugin UI is native JUCE components (`plugin/ui`). It began life as a
React app hosted in a `juce::WebBrowserComponent`; this document is the plan
that re-implemented it as JUCE components, pixel for pixel and feature for
feature, moved the TONE3000 OAuth flows out of the embedded webview and into
the user's browser, and then retired the webview entirely. The plan is
complete (§8, §11); the technical decisions in §5 are the ones the code
still lives by, and the component map in §6 records where each component's
layout came from.

Read `notes/MAINTAINING.md` first. Everything below follows it: delete over
add, one obvious way, comments say *why*, no compatibility scaffolding.

---

## 1. Goal and non-goals

Goal

- One native `AudioProcessorEditor` (`NativeEditor`) that renders every
  screen of the React build identically at the same viewport, and reproduces
  every interaction including the networking edge cases (offline, retry,
  token refresh, cancelled flows).
- No change to DSP, processor API, presets, state schema or MIDI mapping.
- Login runs in the system browser and completes through a loopback
  redirect; no webview is required for any flow. Tone discovery is native:
  the Select tone screen searches the TONE3000 API directly (the web's
  `select_tone` hand-off is retired).
- Runs on macOS, Windows, Linux, iOS with the same code (platform branches
  only where the React app already branches: touch/iOS, standalone).
- A standalone testbed app that drives the UI from the same fixture data the
  React screenshot suite used, so the two could be diffed during the port,
  and that stays as the UI's own capture, diff, unit-test and benchmark tool.

Non-goals

- Visual redesign. Where JUCE cannot do what CSS does (backdrop blur, text
  shadows) the closest faithful equivalent is used and noted in §5.

## 2. Ground rules

- **The React source was the spec.** Every component ported from a React one
  names it in its header comment (`port of KnobControl.tsx`); layout
  constants came from `theme.ts` and the component's inline styles, not from
  eyeballing screenshots. Now that the React tree is gone, those names are
  lineage: the reason a number is what it is. New components need no such
  note.
- **1 design px = 1 JUCE logical unit.** The root component is laid out in
  the fixed `1024 × 578(+chrome)` design space and scaled with one
  `AffineTransform`; children never see the scale (mirrors `useUiScale`).
- **The UI never touches audio state directly.** All reads/writes go through
  `ui::Backend` (the C++ twin of `IAudioBackend`), implemented by
  `ProcessorBackend` in the plugin and `MockBackend` in the testbed.
- **Message thread only.** Network and image decoding run on a `ThreadPool`;
  results hop back with `MessageManager::callAsync` guarded by
  `Component::SafePointer`/generation counters, exactly where React used
  `seq` guards and `AbortController`.
- **Repaint what changed.** Meters repaint only their own bounds at ≤30 Hz;
  chain re-layout happens only when the `revision` changes. Layers that
  only change on interaction but sit over a live one (the EQ curve and
  sliders over the spectrum) are `setBufferedToImage` so a tick blits them.
- **Rendering.** Plain JUCE 2D: Direct2D on Windows (JUCE's default since
  8.0; GDI only under Wine), CoreGraphics on macOS. On macOS the editor is
  built with `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` so each
  dirty rect paints on its own through a Metal-backed layer; plain
  CoreGraphics merges them, and the two meters would repaint the whole plate
  between them 30 times a second. Rasterisation stays on the CPU either
  way. No `juce_opengl` context: it fights host compositing and would drop
  Windows to the software renderer.
- **No new dependencies** unless a JUCE module already ships it. JUCE 9's
  `juce_animation` covers tweens; `juce_cryptography` covers PKCE SHA-256.

## 3. Architecture

```
TONE3000Processor ──► ProcessorBackend : ui::Backend ◄── MockBackend (testbed)
                                  │
                       ┌──────────┴──────────┐
                       │      ui::Services   │   owned by the editor; one per editor
                       │  UiClock            │   the one 30 Hz tick every poller shares
                       │  ChainStore         │   revision poll + optimistic edits
                       │  MeterStore         │   level/CPU feed, per-meter subs
                       │  PresetStore        │
                       │  AudioDeviceStore   │   standalone only
                       │  MidiMapStore       │
                       │  ToneSession        │   tokens, user, refresh, OAuth phases
                       │  T3kClient          │   REST calls (HttpClient)
                       │  ImageLoader        │   async artwork/avatars, one small square per URL, 48 MB budget
                       │  ConnectionGate     │   online/offline probing
                       │  UpdateNotice       │
                       │  HintBus            │   help-text + pin, toast
                       │  Pointer            │   touch or mouse, from the input that arrives
                       │  UiPrefs            │   PropertiesFile (was localStorage); see README "Settings and where they live"
                       └──────────┬──────────┘
                                  │
                 PluginRoot (1024 × H design space, AffineTransform scaled)
                 ├─ PluginHeader ─ PresetBar / AccountMenu / StereoModeToggle
                 ├─ AppBanner (window grows first, then the strip appears)
                 ├─ middle: ChainView | ChainBlockView | TunerView
                 ├─ Faceplate (knobs, SpreadControls, AlignControls)
                 │  (ToneBrowser takes over middle + Faceplate while open;
                 │   SignInScreen takes the same slot while a sign-in runs)
                 ├─ HintBar
                 └─ OverlayLayer: Settings, ConnectionModal, UpdateNotice,
                    popovers (TileMenu, ImageDeckPanel, …), Toast
```

Ownership: `NativeEditor` owns `Services` then `PluginRoot`; components hold
references to the services they need and register as listeners in their
constructor, unregister in their destructor. No singletons.

Data flow: stores are the only things that call the backend. Views subscribe
to stores (`juce::ListenerList`), read plain structs, and call store actions.
This is the same shape as `useChainState`/`useChainActions` etc., minus React.

### Threading

| Work | Thread | Hand-off |
| --- | --- | --- |
| Backend calls (`processor.*`) | message | direct (the processor API is message-thread safe; the webview bridge called it the same way) |
| HTTP (T3K API, update check, connectivity probe) | `ThreadPool` (2 threads) | `callAsync` + request generation guard |
| Image download + decode + resample to a 512 px (art) / 64 px (avatar) square | `ThreadPool` (4 threads, `ImageLoader`'s own) | `callAsync` + `WeakReference`; the component's own final resample to its box is a sub-ms pass from that square |
| OAuth loopback listener | dedicated `juce::Thread` | `callAsync` |
| Meters, spectrum, tuner, chain revision | message, `UiClock` 30 Hz | direct |

## 4. Source layout

```
plugin/ui/                          namespace t3k::ui
  README.md                         how to build/run the testbed, add a component
  core/    Theme.h                  colours, radii, sizes (theme.ts)
           Fonts.h/.cpp             Roboto Mono (embedded) + Arial, or embedded Arimo
           Icons.h/.cpp             lucide glyphs → cached Drawables
           LucideIcons.h            GENERATED by script/gen-lucide-icons.mjs
           Design.h                 DESIGN_WIDTH/HEIGHT, chrome heights
           Paint.h/.cpp             shared painters: rounded panels, ellipsised text, edge fades
           Tween.h / AlphaTween.h   one CSS transition each, over juce_animation
           HelpText.h/.cpp          hint strings (helpText.ts)
           Touch.h                  isTouch(), long-press/double-tap helpers
  model/   ChainState.h/.cpp        var → structs (chain.ts)
           Tone.h/.cpp              Tone/User/Model/Paginated (tone.ts)
           AudioDeviceState.h/.cpp  (audioDevice.ts)
           MidiMapState.h/.cpp      (midiMap.ts)
  backend/ Backend.h                abstract ui::Backend (IAudioBackend + native fns)
           ProcessorBackend.h/.cpp  plugin implementation
  services/ *.h/.cpp                one file pair per store listed in §3
           HttpClient.h/.cpp        pooled juce::URL requests, JSON, cancellation
           OAuthFlow.h/.cpp         PKCE + loopback redirect (§5.14)
           LoopbackServer.h/.cpp
  widgets/ reusable leaf controls: Knob, ChromeIconButton, IconButton, Pill,
           SegmentedControl, SelectField, TextField, Toggle, DbMeter, DotMeter,
           BlockLed, LoadingDots, BusyOverlay, RetryLoadBadge, FormatBadge,
           GearGlyph, Avatar, Popover, Scrim, ContextMenu, ScrollView
  views/   one folder-less file pair per React screen component (§6)
  NativeEditor.h/.cpp
  testbed/ CMakeLists.txt           juce_add_gui_app(UiTestbed)
           Main.cpp                 --capture / --scenario / --compare / --selftest / --bench
           MockBackend.h/.cpp       fixture-driven ui::Backend
           Scenarios.h/.cpp         drive steps per scenario id
           fixtures/scenarios.json  one mock state per scenario (the testbed's own data)
           Compare.cpp              PNG diff (mismatch %, worst tile, diff image)
script/gen-lucide-icons.mjs         regenerates core/LucideIcons.h
```

Two files per component (`.h` + `.cpp`), CamelCase, `t3k::ui` namespace.
Widgets know nothing about services; views wire widgets to services.

## 5. Technical decisions

### 5.1 Scaling
`NativeEditor::resized()` computes `s = min(w/1024, h/designH)` and applies
`root.setTransform(AffineTransform::scale(s))`, then centres. Mouse
coordinates are transformed by JUCE. Text rendered through a scale transform
uses the platform rasteriser at the final size, so glyphs stay crisp.
`designH = 578 + bannerHeight + hintBarDelta`, mirroring `extraContentHeight`.

The shell also publishes `s` as `Services::zoom`. The tone browser's body
(everything under the ← SELECT TONE row) subscribes and counter-scales by it
(`setTransform(scale(1/s))` over the same design-space area, bounds in
screen pixels), so the search box, filter row and cards hold their 1x size
and a larger window shows more: the controls widen with the column, the
cards fill two columns, three once three fit at the default 392px card
width. `Popover` adopts its anchor's scale relative to the overlay layer, so
the filter menus opened from that body are 1x as well.

Resize limits/aspect and the persisted `editorScale`/`editorExtraHeight`
are kept verbatim from `Editor.cpp` (moved, not rewritten).

### 5.2 Fonts
Roboto Mono (Regular, Bold) and Arimo (Regular, Bold, Italic, Bold Italic)
are embedded (both Apache-2.0, `plugin/ui/assets/`). The web UI's
`Arial, sans-serif` maps to the installed Arial on macOS, Windows and iOS.
JUCE on Linux matches family names against the font files it scans, not
fontconfig's aliases, so a machine without Arial would get a null typeface
(height 0, NaN ascent: text at NaN positions, and the software renderer
writing out of bounds; on x86 the NaN→`INT_MIN` conversion froze the
standalone's Settings screen). `Fonts::sans` therefore checks once per
process whether Arial resolves and otherwise uses the embedded Arimo, whose
metrics are Arial's, so line boxes and wrapping are identical. Weight
600/700 → Bold. `Fonts::mono(px)` / `Fonts::sans(px, bold)` are the only
constructors used; the embedded typefaces are built once and shared.

### 5.3 Icons
`script/gen-lucide-icons.mjs` reads the `__iconNode` arrays from a pinned
`lucide-react` (installed into a temp dir for the run) for the icons in its
`ICONS` list and emits SVG strings into `LucideIcons.h`. `Icons::draw(g, id, bounds, colour,
strokeWidth)` parses once into a `Drawable`, caches per id, tints via
`replaceColour`. Stroke stays 2 viewBox units → scales with the box, like
lucide.

### 5.4 Text
- Single-line ellipsis: `GlyphArrangement::addFittedText` with 1 line.
- Multi-line clamp (2-3 lines): `TextLayout` from `AttributedString` with
  the height cap, then trailing ellipsis on the last line (helper in
  `Paint`).
- Mixed weights in one run (banners): `AttributedString`.
- Letter-spacing (`0.02em` uppercase labels): `Font::withExtraKerningFactor`.
- Baselines land where Blink puts them: `Fonts::cssBaseline` rounds ascent
  and descent to whole pixels and floors the half-leading;
  `Fonts::normalLineHeight` is `line-height: normal` for Arial (14px → 16,
  not 17). `paint::cssLine` draws one nowrap/ellipsised line that way;
  `RichFlow` (core/RichText) is the wrapped, mixed-style paragraph, with
  `InlineBox` runs for chips and icons that ride the text line.
- Fractional layout (settings forms): the web stacks `1.45`-line-height
  paragraphs, so box tops fall on fractions and Blink snaps each box and
  each baseline from its *absolute* position. `FormItem` keeps float
  heights, `placeChild` snaps edges like `PixelSnappedIntRect` and hands the
  lost fraction down (`subpixelTop`), and text painters add it back before
  rounding. A bare `<span>` in a plain `<div>` also shares its line with the
  16px body strut (`FormLabel::setStrut`), which is why some section labels
  are 18px tall and others 17.

### 5.5 Blur / shadows
CSS `backdrop-filter: blur(4px)` on the full-window scrims: `ModalLayer`
asks the root for a half-resolution snapshot of everything beneath the
overlay layer (`PluginRoot::snapshotBeneathOverlay`), runs `core/Blur`'s
three-pass box blur over it (a few ms) and redraws it a few times a second
so the meters keep moving softly behind a dialog, as in the browser. The
in-place `BusyOverlay` (a reloading list) keeps the 50 % scrim alone.
`box-shadow` → `DropShadow`; `text-shadow` → draw twice with offset.

### 5.6 Animation
Only transitions that carry state animate: dims (`DimGroup`, the EQ views,
a disabled tile's image), the pill toggle, the tuner's in-tune mark. All
run through `core/Tween` (`AlphaTween` for opacity), which is
`juce_animation`'s `ValueAnimatorBuilder` + `VBlankAnimatorUpdater`: one
frame per display refresh, CSS `ease`, snapped when the owner isn't showing.
Decoration snaps: the banner strip, the toast, the tile chrome and the
gallery's branch dots appear and disappear in one frame.

### 5.7 Overlays and dismissal
`PluginRoot::overlays()` returns the top layer. `Popover` positions itself
relative to an anchor rectangle (with the same edge clamps the React
components do) and dismisses on outside pointer-down or Escape through a
root `MouseListener` (`useDismissable`). The web's menus were children of
the element that opened them, so they scrolled with it; the overlay layer
is not, so a `Popover` watches its anchor (or the component a context
menu's point was given in) with a `ComponentMovementWatcher` and re-places
itself on every move of it or an ancestor: a wheel or drag on the chain
lane, the settings page or the filter row carries the open panel along,
and once the anchor has left its viewport altogether the panel dismisses
(`PopoverFollowTests`). `ModalLayer` is the modal base
(blurred scrim + centred content, every press swallowed); `ScrimMessage` is
the card-less column (glyph or dots, copy, pill row) the connection modal
uses. `PluginRoot` owns one instance of each modal and rebuilds it from its
store on the next message-loop turn (a store change usually arrives from
inside the modal's own button handler), stacked update notice → connection
modal as in `Plugin.tsx`. A sign-in in flight is not a modal but a page:
`SignInScreen` takes the tone browser's slot (see the UI README, "Sign-in").

### 5.8 Scrolling
Every scroll area in the UI is one widget, `DragScroller`: `juce::Viewport`
with scrollbars hidden, one axis, and every way of moving it built in, so an
owner only picks the axis, whether the keys are its own (`Keys`) and how much
room to leave when it follows focus (`setFocusMargin`). There are no
scrollbars anywhere; the ways in are:

- **Wheel / trackpad.** A plain wheel pans a sideways scroller by the
  gesture's dominant axis, as the web's `useHorizontalWheelScroll` did, with
  the sub-pixel remainder carried between events. JUCE's own remap takes
  `deltaX` whenever it is non-zero, and a mostly vertical trackpad gesture
  jitters a few sideways pixels of either sign, so it stuttered back and
  forth until the gesture was big enough to read as purely vertical; it also
  rounded every event up to a whole pixel.
- **Drag, with any pointer** (`ScrollOnDragMode::all`). A finger or a mouse
  that moves 8px before release pans the content, with the viewport's own
  inertia; a shorter press is a click. JUCE leaves out one rule: once a drag
  has become a pan, the press it started with is spent. `juce::Button` would
  still fire on release, because for touch it counts "still over" by bounds
  and the content pans along under the pointer; so `Clickable` asks
  `DragScroller::panning()` on every drag and on release, lets go of the
  pressed state as soon as the pan begins and does not click
  (`TouchScrollTests`, driven through the peer). Controls that drag for
  themselves set `setViewportIgnoreDragFlag`, so a drag on them never pans
  the page and the gaps around them do: knobs, EQ dots and faders, gallery
  tiles (a drag sorts), the preset browser's grips, and text fields (a drag
  selects text). Gallery tiles make one exception: a quick, mostly sideways
  touch swipe (the drag distance within `GalleryTile::kFlickMs`) clears the
  flag mid-gesture and the lane pans from the press; a slower drag sorts.
  Mouse drag used to be opted out (`nonHover`: a finger pans, a mouse
  selects); it is in because it is the only pointer affordance left where
  the pointer has no wheel and no touch: a mouse without one, or a
  touchscreen the OS hands the plugin as a mouse. Raspberry Pi OS's labwc
  does that by default (`<touch mouseEmulation="yes"/>`; the "Multitouch"
  switch in Screen Configuration turns it off and the finger arrives as
  touch), and a Windows or Linux tablet may too.
- **Keys**, as a browser scrolls a document: arrows a line
  (`DragScroller::kKeyLineStep`), Page Up / Down a page less a line, Home /
  End to the ends (`DragScroller::scrollByKey`; a key at an end is still
  spent, a page that fits takes none, Space, Enter, cross-axis and modified
  arrows are never the scroller's). With a control focused the key bubbles
  up from it through `DragScroller::keyPressed` to the nearest enclosing
  scroller whose keys are its own. With nothing focused it comes through
  `PluginRoot::FocusPolicy` to `PluginRoot::frontScroller()`: the screen in
  front's main scroller (Settings' page, the browser's results, the chain
  lane or an open block's detail column; none under a modal, the tuner or
  sign-in). `juce::Viewport` scrolls from keys only with a visible
  scrollbar, hence the widget's own. Popover lists (`SelectField`,
  `ModelSelect`, `PresetBar`, the filter menus) are `Keys::none`: the popover
  walks the rows with the arrows and the list follows the focused row.
- **Focus follow.** A `DragScroller` listens for keyboard focus and reveals
  the focused control with its margin (`DragScroller::reveal`, from
  `FocusChangeListener`): a Tab stop below the fold, a tile moved with the
  arrows, a popover row walked to. For a control inside a nested scroller
  (a knob in a lane on a page) the outer one aims at the part the inner one
  shows, not at the control's far-off spot.

Edge fades are painted over the viewport by the owner.

What a mouse reveals on hover (the tile's power / swap / trash strip, the
branch dots in the stereo gaps) has to stay up for a finger, which can't
hover. The web read that off `pointer: coarse`; here it is
`Services::pointer`, decided at run time rather than per build, because a
Windows or Linux tablet runs the desktop build and a convertible is both
in one session. iOS and Android are touch throughout. A desktop build
seeds from the hardware where the OS can say (Windows: an integrated touch
digitizer and slate mode) and then follows the last press or wheel turn
(`PointerTracker`, a root mouse listener beside `HintTracker`): the first
touch turns the affordances on everywhere, the next mouse click or scroll
turns them off again. Moves are deliberately not consulted: JUCE synthesises
mouse-source moves (`Component::sendFakeMouseMove` on any relayout under the
pointer, the peers' pointer-leave handling), and on a tablet those arrive
mid-gesture and would read as a mouse. Views that carry such an affordance listen (`ToneTile`,
`GalleryLane::BranchGap`). The hint copy stays compile-time
(`design::kCoarsePointer`): its touch wording is more than a word swap in
places, so a desktop build with a finger on it reads the desktop copy.

### 5.8a Keyboard focus and accessibility
Nothing is focused by default, as in a browser, and the host's transport
keys must keep working while the plugin window is in front: Space and Enter
that no control takes go back to the host (`NativeEditor::keyPressed`, the
`keyPassthrough.ts` port). The rules that make both true:

- **Clicks never focus a control** except text fields. Every button derives
  from `widgets/Clickable` (`juce::Button` + `setMouseClickGrabsKeyboardFocus
  (false)`); `Knob`, `Paginator`, `GalleryTile` and `Popover` do the same. So
  after any mouse work nothing is focused and Space / Enter reach the DAW.
- **Tab / Shift+Tab** is the only way to a non-text control. JUCE ignores Tab
  with nothing focused, so `PluginRoot::FocusPolicy` listens on the window's
  component and enters the order at either end (focus resting on a
  standalone `DocumentWindow` counts as nothing). Tab order is JUCE's
  (top-to-bottom, left-to-right per parent). While the Settings takeover is
  up (and no modal over it) Tab enters its cycle instead: `SettingsScreen` is
  a keyboard focus container, since the chrome it covers is still showing
  to JUCE and must stay out of reach. Every scroller scrolls each stop into
  view (5.8).
- **Focus is dropped** by Escape (`PluginRoot::keyPressed`) and by a press
  anywhere outside the focused control's line of ancestry (`FocusPolicy::
  mouseDown`, a recursive mouse listener on the root). `PluginRoot`,
  `SettingsScreen` and `Popover` are keyboard focus containers whose traverser
  (`core/NoDefaultFocus`) names no default, so removing the focused
  component or activating the window focuses nothing instead of JUCE's
  "first focusable".
- **With a control Tab-focused**: Enter presses a button (`juce::Button`);
  Space is not taken by buttons and still reaches the host. Knobs take the
  arrows (Shift: fine), Home/End and Enter (type-in editor); a chip takes
  Backspace/Delete as its ×; the paginator takes Left/Right; a gallery tile
  takes Space/Enter + arrows for keyboard sorting (5.9). A text field takes
  everything, as it always did.
- **Popovers** take focus on open and are their own focus container: Tab /
  arrows walk the rows (scrolling them into view), Enter picks. A panel the
  keyboard opened (its anchor had focus) or walked returns focus to its
  anchor on close; a mouse-opened one leaves nothing focused.
- **No focus ring**: the design has none for now (`Clickable` and the tiles
  draw no focus state), so the keyboard path is for screen-reader users
  first.

Screen readers get JUCE's accessibility tree with these names and roles:
`Clickable` reports a button (checkable when toggleable) named by
`setTitle()`, else the button text, else the component name, else the lead
of its help hint (`help::lead("Undo: …") == "Undo"`), with the full hint as
help; `Knob` a slider valued in display units ("-3.2 dB", settable); the
paginator a slider "Page 3 of 12"; tiles a button named by the tone; text
fields are named by their placeholder; popovers are `popupMenu`s named after
their anchor, modals `dialogWindow`s named by their heading. Meters, LEDs,
avatars, artwork, badges and busy overlays are `setAccessible(false)`.
Status that only paints elsewhere is spoken through `help::announce()`
(`AccessibilityHandler::postAnnouncement`): toasts, banners as they show,
browser results ("12 tones, page 1 of 3"), nothing found, fetch failures.
On Windows, stock JUCE speaks these itself through SAPI in-process whenever
`UiaClientsAreListening()` is set, a machine-wide flag any UIA client
(Chromium/Electron apps, Magnifier, Game Bar) flips on, so users with no
screen reader heard a voice from the plugin. The `T3K_UIA_ANNOUNCE` patch in
the root `CMakeLists.txt` raises a UIA notification event instead, so only an
actual screen reader reads them.

Tests: `--selftest` runs the naming rules and each control's keys
(`AccessibilityTests`) and the whole focus policy in a real window
(`FocusPolicyTests`: keys and presses through the peer), the Settings
page's keys and Tab cycle (`SettingsKeyboardTests`), and every scroll
surface in the app, each in its own scenario in a real window, against the
same list: wheel, mouse drag, finger (where the platform has a touch
source), keys with nothing focused, keys from a focused control, and focus
follow to the far end (`ScrollSurfacesTests`: the chain lane, a block's
detail column, the Settings page, the browser's results and filter chips,
the preset browser's rows, a select field's rows; plus a drag on a tile
sorting and a drag across a text field selecting, with the surface around
each staying put). `--capture` audits
every scenario for Tab stops without a name (`drive::unnamedFocusables`) and
fails the run on any.

### 5.9 Gallery drag and drop
`GalleryDrag` (in `ChainView`) replaces `dnd-kit`: press → 8 px slop (touch:
250 ms hold, `useTouchHold`) → lift ghost into the overlay layer → live lane
reflow with the same insertion rules as `GalleryLane.tsx` → commit through
`ChainStore::move/duplicate`. Alt/Option held at drop → duplicate. OS file
drops onto tiles use `FileDragAndDropTarget` (also fixes Linux drops that
the webview could not receive).

### 5.10 Hints
Components call `setHelpText()` (JUCE built-in). `HintBus` listens to root
mouse-move, walks from the hit component up to the first non-empty help text
and publishes it (DOM `data-help` delegation). `pin()`/`unpin()` for drags.

### 5.11 Meters
`MeterStore` polls `backend.getMeterLevels()` on the `UiClock` tick,
quantises, holds the clip latch and CPU EMA (`useMeters.tsx`), notifies only
meters whose value changed. `DbMeter`/`DotMeter`/`BlockMeter` paint from
cached values.

`UiClock` is the single 30 Hz heartbeat behind every UI-side poll (meters,
`SpectrumFeed`, `TunerFeed`, `ChainStore`'s revision check, the channel
picker's input levels). One tick means every poll and the repaints it causes
land in the same frame, and the message thread wakes once per period instead
of once per poller. It runs only while something is subscribed and idles at
4 Hz while `NativeEditor::isShowing()` is false (minimised window, hidden
editor), so a hidden UI costs no polls, parsing or painting.

### 5.12 Chain state
`ChainStore` mirrors `useChainState`: fetch on `chainChanged`
(the `UiClock` tick checks `getCurrentChainRevision()`, cheaper than the
webview event) plus a 1 s fallback poll; parse `var` → `ChainState` structs
once; optimistic local reorder during drags; `revision` gates re-layout.

The faceplate's input-mode button reads `inputMode` (`stereo` / `dual` /
`left` / `right`) and the processor-derived `dualMonoActive` flag. The menu
is built per open: a mono chain offers Stereo SUM (L+R) / Stereo Dual Mono
(L&R) / Left / Right,
stereo chains offer Stereo / Left / Right (a stored `dual` displays as
Stereo there), and a branched chain drops both stereo feeds. `dualMonoActive`
(mode is dual, chain is mono, source and rig are stereo) is what shows Bal
and Auto Balance, dims the Spread group with its own reason and splits the
output meter; the UI never re-derives it from the flags. `MockBackend::
setInputMode` mirrors the derivation so the testbed's `chrome-dual-mono`
scenario and the `Faceplate dual mono` self-test see the same state.

### 5.13 Parameters
Faceplate knobs and toggles bind via `juce::ParameterAttachment` to
`backend.parameter(id)`. `MockBackend` owns a minimal `juce::AudioProcessor`
with the same parameter ids so attachments behave identically in the testbed.

### 5.14 OAuth in the system browser
Replaces `useT3kSelect` navigation. `OAuthFlow`:

1. Generate PKCE verifier/challenge (`juce::SHA256`, base64url) and `state`.
2. Start `LoopbackServer` on `127.0.0.1:0` (ephemeral port): a
   `StreamingSocket` accept loop on its own thread, answers the single GET
   with a short "You can return to TONE3000" page, then stops.
3. `redirect_uri = http://localhost:{port}/` (localhost is auto-allowed for
   the publishable key), `launchInDefaultBrowser`.
4. Phase `leaving` shows the existing overlay copy plus a **Cancel** button
   (new: with an external browser the user may never return). Callback →
   phase `returning` → exchange code → tokens → `handleToneSelected(tone_id)`
   exactly as today; `canceled=true` → idle.
5. Closing the editor stops the listener; a stale callback is ignored by
   `state` mismatch.

Same code for iOS; the app has background-audio so the process and socket
stay alive while Safari is foregrounded. If Apple's sandbox blocks loopback
in practice, the fallback is `ASWebAuthenticationSession` with a custom
scheme, recorded as a risk in §10 and not implemented speculatively.

### 5.15 Session and preferences
`UiPrefs` wraps a `PropertiesFile` in the same app-data folder
`ProcessorState.cpp` uses (`userSettingsOptions()` pattern, own filename).
Keys map 1:1 to the `localStorage` keys in `useToneSession`,
`uiPreferences.ts`, `useUpdateNotice`. Session-scoped values (`sessionStorage`
during OAuth) become fields on `OAuthFlow` since the process no longer
reloads.

The file is one per machine user and shared by every host process (each
DAW, the standalone), which all hold their own in-memory copy. Instances in
one process share a single `PropertiesFile` (`NativeEditor::PrefsFile`).
Across processes each write is a merge under an `InterProcessLock`: reload,
apply the one key, save at once (autosave is off). `UiPrefs::sync()` exposes
the reload on its own and reports changed keys to listeners; `Tone3000Client`
calls it before refreshing and before giving up on a rejected refresh, so a
token pair another host rotated is adopted instead of clobbered or cleared.

### 5.16 Configuration
The repo-root `.env` (`.env.local` overrides, the configure environment
overrides both) is the single source of `T3K_PUBLISHABLE_KEY`,
`T3K_API_DOMAIN`, `T3K_UPDATE_NOTICE`. CMake parses it at configure time
into `T3kConfig.h` (`plugin/ui/NativeUi.cmake`); `.env.example` documents
the keys.

### 5.17 Build
Every non-`HEADLESS` build compiles `plugin/ui/**` and `createEditor()`
returns `NativeEditor`. `JUCE_WEB_BROWSER=0` everywhere: no browser engine,
no WebView2 or WebKitGTK dependency on any platform. `T3K_BUILD_UI_TESTBED`
adds the testbed app to the plugin build and registers its self-tests with
ctest; the testbed also configures on its own (`cmake -S plugin/ui/testbed`).

## 6. Component map (React → C++)

Where each component came from. The React files no longer exist in the
tree; the map is the key to the `port of …` notes in the C++ headers.

| React | C++ (`plugin/ui/…`) | Notes |
| --- | --- | --- |
| `Plugin.tsx`, `App.tsx` | `views/PluginRoot` | screen switching, banner/hint choreography |
| `PluginHeader.tsx` | `views/PluginHeader` | |
| `Tone3000Logo.tsx`, `T3kMark.tsx` | `widgets/Tone3000Logo`, `widgets/T3kMark` | vector paint |
| `PresetBar.tsx` | `views/PresetBar` (+ `PresetBrowserPopover`) | rename text entry, MIDI PC badge |
| `AccountMenu.tsx` | `views/AccountMenu` | avatar image, popover |
| `StereoModeToggle.tsx` | `views/StereoModeToggle` | |
| `AppBanner.tsx` | `views/AppBanner` | slide-in, variants |
| `HintBar.tsx` | `views/HintBar` | |
| `Faceplate.tsx`, `KnobControl.tsx`, `KnobFace.tsx`, `KnobInner.tsx`, `knobScale.ts` | `views/Faceplate`, `widgets/Knob` | drag/fine/double-click type/alt reset; touch variants |
| `SpreadControls.tsx`, `AlignControls.tsx` | `views/SpreadControls`, `views/AlignControls` | |
| `ImageDeckPanel.tsx` | `views/ImageDeckPanel` | |
| `ChainView.tsx`, `GalleryLane.tsx`, `GalleryBlock.tsx`, `chainLayout.tsx` | `views/ChainView`, `views/GalleryLane`, `views/GalleryTile` | DnD §5.9 |
| `TileMenu.tsx` | `widgets/ContextMenu` | |
| `ChainBlock.tsx`, `BlockInfoPanel.tsx`, `ModelSelect.tsx` | `views/ChainBlockView`, `views/BlockInfoPanel`, `views/ModelSelect` | |
| `BlockEqView.tsx`, `EqSliders.tsx`, `SpectrumBackdrop.tsx`, `eqMath.ts`, `eqShared.ts` | `views/BlockEqView`, `views/EqSliders`, `views/SpectrumBackdrop`, `core/EqMath` | |
| `BlockLed.tsx`, `BlockMeter.tsx`, `DbMeter.tsx`, `meterColor.ts` | `widgets/BlockLed`, `widgets/BlockMeter`, `widgets/DbMeter` | |
| `GearIcon.tsx`, `FormatBadge.tsx`, `AvatarFallback.tsx`, `RetryLoadBadge.tsx`, `LoadingDots.tsx` | `widgets/GearGlyph`, `widgets/FormatBadge`, `widgets/Avatar`, `widgets/RetryLoadBadge`, `widgets/LoadingDots` | |
| `ToneBrowser.tsx` | `views/browser/ToneBrowser` (+ `FilterBar`, `FilterChip`, `FilterMenu`, `model/ToneQuery`) | The native Select tone screen: search box, gear / verified / profile chips, an expandable row for sort, format, tags, makes, creators, calibrated; paging, busy overlay. Replaces the web's stream tabs and Select-flow hand-off; the whole screen needs a session |
| `TunerView.tsx` | `views/TunerView` | |
| `Settings.tsx` | `views/settings/SettingsScreen` (header, tabs, scroller), `views/settings/PluginSettingsPage` | Full-window takeover owned by `PluginRoot::openSettings` |
| `SystemSettings.tsx`, `InputChannelPicker.tsx`, `MidiInputsSection.tsx` | `views/settings/SystemSettingsPage`, `InputChannelPicker`, `MidiInputsSection` | Standalone only; `update()` mutates rows in place so open dropdowns survive polls |
| `MidiMapSettings.tsx`, `midiCatalog.ts` | `views/settings/MidiMapSection`, `core/MidiCatalog` | `LearningRow` instances persist across rebuilds to keep the typed CC draft |
| `controls.tsx` (FieldRow, ToggleRow, RadioOption, SettingsGroup, TipRow, PillToggle, SegmentedControl, SelectField, AlertCard…) | `widgets/form/*` (`FormItem`/`FormStack` layout, `FormRows`, `FormControls`, `FormText`, `SelectField`, `AlertCard`, `FormStyle`) | One widget per React control; fractional heights per §5.4 |
| banner copy reused inline in settings | `views/settings/InlineBannerAlert`, `views/settings/InlineChrome` (LITE/FULL chip, inline icons) | Same `BannerRule`s as the top banner |
| `UpdateNotice.tsx`, `ConnectionModal.tsx`, `OAuthOverlay.tsx`, `Toast.tsx` | `views/modals/UpdateNotice`, `views/modals/ConnectionModal`, `views/modals/OAuthOverlay`, `views/ToastView` | on `widgets/ModalLayer`; remote HTML via `core/RichText` (`Html::toRichText`, formatting tags only) |
| `useConnectionGate.ts`, `useUpdateNotice.ts` | `services/ConnectionGate`, `services/UpdateCheck` | reachability and the version endpoint go through `ToneSession` |
| `controls.tsx`, `ChromeIconButton.tsx`, `IconButton.tsx` | `widgets/*` | |
| `ErrorBoundary.tsx`, `keyPassthrough.ts`, `index.html` watchdog, `JuceBackend.ts` | (none) | webview-only; nothing to port |
| hooks (`use*.ts`) | `services/*` | see §3 |
| `t3k/*` | `services/T3kClient`, `core/Format` (`formatCount`, `timeAgoShort`, `labels`) | |

## 7. Testbed and verification

During the port the reference side was a Playwright suite that rendered the
React app at 1024×578 against a JS mock backend and wrote 2× PNGs; the
native captures were diffed against those. That suite went with the React
tree. The testbed (`plugin/ui/testbed`) is what remains, and it is the UI's
own tool now: the fixtures it once imported are its own data, and captures
are diffed against earlier captures (before/after a change) rather than
against another renderer.

- `UiTestbed --capture <outDir> [--ref <beforeDir>] [scenarioFilter]` builds
  `PluginRoot` over `MockBackend` for each scenario, applies its drive steps
  (open settings, select block, hover…), snapshots at 2× and writes
  `<id>.png`; with `--ref` it also diffs each against the same id in an
  earlier run.
- `UiTestbed --scenario <id>` opens the window for interactive poking.
- `UiTestbed --compare <before.png> <after.png> [diff.png]` prints the global
  mismatch %, the densest 64px tile (a shifted control is invisible in the
  global figure but fills its tile) and writes a diff image.
- `UiTestbed --selftest` runs the `juce::UnitTest` cases in
  `testbed/SelfTests.cpp` for the pure logic a screenshot can't pin down
  (HTML → rich text, wrapping, version compare, the connection gate's state
  machine, tuner math, PKCE / callback parsing, the loopback server, token
  refresh and 401 retry, pagination). CI runs it through ctest next to the
  DSP tests (`-DT3K_BUILD_UI_TESTBED=ON`).
- `--bench` runs the UI under a synthetic load (moving meters, spectrum,
  tuner, scripted drags and scrolls) and reports CPU, paint rate, repainted
  area, stalls and memory per phase (`plugin/ui/README.md`).
- Drives behave like a user: `click` scrolls the target into its viewport
  first (`drive::scrollIntoView`), and controls are found by their help text.
- `testbed/fixtures/scenarios.json` is the scenario table: one mock backend
  state per screen, plus `hasDrive` for the ones that need interaction to
  reach. It is edited in place; a new screen gets a new entry.

Behavioural verification during the port: each phase ended with the
scenario set for that screen green in the testbed, then a manual pass in the
Standalone against the webview build side by side. Since the switch-over,
the plugin has been QA'd in hosts on every desktop platform and on iPad;
DSP tests (`test/`) run unchanged.

## 8. Execution phases (complete)

Each phase was a self-contained commit set; the plugin built and ran after
every phase (screens not yet ported rendered a placeholder in the native
build until Phase 11 flipped the default).

| # | Phase | Deliverable | Done when |
| --- | --- | --- | --- |
| 0 | Scaffold | `plugin/ui` tree, CMake option, `T3kConfig.h` from `.env`, `Theme`, `Fonts`, `Icons` + generator, `Design`, `Paint`, testbed app skeleton, fixture export, compare tool | testbed renders an empty 1024×578 root and compares a PNG |
| 1 | Model + backend | `model/*` parsers with unit checks against fixture JSON, `ui::Backend`, `ProcessorBackend`, `MockBackend` | all fixtures parse; mock answers every native function |
| 2 | Services | `ChainStore`, `MeterStore`, `PresetStore`, `AudioDeviceStore`, `MidiMapStore`, `UiPrefs`, `HintBus`, `HttpClient` | stores unit-tested against the mock |
| 3 | Chrome | `PluginRoot`, `PluginHeader`, logo, `PresetBar` (+popover, rename), `AccountMenu`, `StereoModeToggle`, `HintBar`, `NativeEditor` scaling/resize/persistence | `header-*`, `preset-*` scenarios ≤ budget |
| 4 | Faceplate | `Knob` family, `Faceplate`, `SpreadControls`, `AlignControls`, `ImageDeckPanel`, parameter attachments, touch paths | `faceplate-*` scenarios; knob interactions match `KnobControl.tsx` (drag, shift-fine, double-click entry, alt reset) |
| 5 | Meters | `DbMeter`, `DotMeter`, `BlockLed`, `BlockMeter`, `MeterStore` wiring, CPU readout | meter fixtures render identically; CPU of idle UI ≤ webview build |
| 6 | Chain gallery | `ChainView`, `GalleryLane`, `GalleryTile`, `ContextMenu`, `GearGlyph`, `FormatBadge`, `RetryLoadBadge`, `LoadingDots`, drag/drop, file drop | `main-*`, `gallery-*`, loading/error tile scenarios |
| 7 | Block detail | `ChainBlockView`, `BlockInfoPanel`, `ModelSelect`, `BlockEqView`, `EqSliders`, `SpectrumBackdrop`, EQ math | `block-*`, `eq-*` scenarios |
| 8 | Tuner + banners + toast + modals | `TunerView`, `AppBanner` choreography (window grows, then slides), `Toast`, `ModalLayer` + blur, `UpdateNotice`, `ConnectionModal`, `OAuthOverlay`, `ConnectionGate`, `UpdateCheck` | `main-tuner-*`, `banner-*`, `chrome-toast-*`, `load-offline-*`, `load-oauth-*`, `load-update-notice` scenarios; `--selftest` green |
| 9 | Settings | `widgets/form/*`, `SettingsScreen`, `PluginSettingsPage`, `SystemSettingsPage`, `MidiMapSection`, `MidiInputsSection`, `InputChannelPicker`, `core/MidiCatalog`, subpixel form layout (§5.4) | `settings-*` scenarios at the ~1 % glyph floor |
| 10 | TONE3000 | `Tone3000Client`, `Tone3000Session`, `ImageLoader`, PKCE `OAuth` + `LoopbackServer`, `SignInScreen` (copy link + device-flow fallbacks), `ToneBrowser`, `Avatar`, tone load flow (`ToneLoadFlow`) incl. retry/cancel/network-error paths | `browser-*`, `oauth-*` scenarios; live login + select against tone3000.com |
| 11 | Switch-over | `T3K_NATIVE_UI` default ON, CI builds both, `README`/`ui/README` updated, `plugin/ui/README.md` written, dead webview-only UI helpers removed from the native path | full scenario suite green on macOS; Windows/Linux/iOS builds pass |

Status: phases 0-11 are done, and so is §11. At switch-over the full
scenario suite sat at the glyph floor against the React references on macOS
(every id ≤ 1.9 % globally, worst 64px tile ≤ 24 %; the residue was CoreText
vs. Skia anti-aliasing, JPEG resampling and live meter animation),
`--selftest` was green, and the live sign-in and Select round trip against
tone3000.com worked through the loopback redirect (§10). The Windows, Linux
and iOS builds were then exercised on their own hardware, and the plugin was
QA'd against the webview build in hosts before the webview was removed.

## 9. Networking edge cases (must all survive)

From `useToneSession`, `useT3kSelect`, `useToneLoadFlow`, `useConnectionGate`,
`useUpdateNotice`, `tone3000-client.ts`, `ToneBrowser.tsx`:

- Token refresh on 401 with single-flight refresh; logout on refresh failure.
- Expired/missing session while offline: keep cached user, show offline UI.
- Login flow: cancel from tone3000, invalid `state`, exchange failure →
  error overlay with Retry; a `browse` intent lands in the tone browser.
- Tone load: model list fetch fails → retry badge; download failure per
  block; cancelled by user; block replaced mid-load (seq guard).
- Browser: pagination race (older page arriving after newer), taxonomy
  lookups superseded by a newer one (`AsyncScope::reset`), a profile filter
  parking the catalog-only controls without losing their values, the
  screen's state (`BrowserState`) surviving a trip back to the chain,
  favourite toggle optimistic + rollback, offline banner, empty states,
  avatar/artwork 404 → fallback glyph.
- Connection gate: probe cadence, exponential backoff, "Retry" affordance,
  standalone vs plugin differences.
- Update check: opt-in flag, `X-Device-Id`, `X-Plugin-Version`, dismissed-version memory.
- Editor closed mid-request: no callbacks into destroyed components.

## 10. Risks and open questions

- **Loopback redirect URI** must be accepted for the publishable key
  (`http://localhost:{port}/`). Verify on the first live run; if only fixed
  ports are allowed, pick from a small fixed list.
- **iOS loopback while backgrounded**: expected to work with background
  audio; fallback `ASWebAuthenticationSession` if not.
- **Font rasterisation** differs per platform; the diff budget accounts for
  it, positions do not move.
- **Linux drag-and-drop from file managers** now works natively; make sure
  it does not double-handle with the Standalone window.
- **Gallery scroll with many artwork tiles**: artwork is cached at display
  size, and tiles paint only their own bounds.
- **The macOS Metal layer on Intel**: JUCE marks the whole CPU buffer
  modified each frame under `MTLStorageModeManaged`, a full-frame upload on
  discrete-GPU Macs. If that measures worse than plain CoreGraphics, apply
  the define to the arm64 slice only (`-Xarch_arm64`).

## 11. After this plan (done)

The webview is gone: the `T3K_NATIVE_UI` option and its OFF path, `ui/`
(the React app, its Playwright suite and its npm tooling), the
`TONE3000Editor` / `EditorWebViewSetup` bridge, `WebViewCookies`,
`WindowMouseEvents.mm`, the WKWebView and WebKitGTK patches in the root
`CMakeLists.txt`, the WebView2 requirement on Windows, and the separate CI
job that kept the webview compiling. `JUCE_WEB_BROWSER` is a constant 0.
The testbed owns its fixtures and diffs captures against earlier captures.
What stayed on purpose:

- `UiPrefs` keys and the token store key keep the names the web UI used.
  Lineage only: the web UI stored those values in the webview's
  `localStorage` (a per-engine, on macOS per-host, browser store), and
  nothing reads it, so the first native run after a web-UI build starts
  with an empty prefs file: one sign-in, default toggles. Plugin state,
  `preferences.settings`, presets and the model cache are untouched by the
  port and carry over as they are. Importing the old tokens would have
  meant a SQLite reader (WebKit) plus a LevelDB reader (WebView2) for one
  event; a single re-login was the better trade.
- `loadLocalTone`'s base64 form, which the DSP tests drive.
- The `port of …` header notes and the map in §6, as the record of where
  each component's numbers came from.
