import { useSyncExternalStore } from 'react';
import { IS_COARSE_POINTER } from '../hooks/useUiScale';

/**
 * Central help system: every control publishes a one-line hint here while
 * hovered (or mid-interaction), and the faceplate's pinned readout renders
 * whatever is current, Native Instruments style, instead of browser
 * tooltips. All copy lives in this file so wording stays consistent.
 *
 * Copy conventions:
 * - `Name: what it does.` then a shortcut legend of `key: effect` pairs
 *   joined with middots. Keep it terse: the bar shares its row with the
 *   CPU readout, and long lines get ellipsized.
 * - Modifier keys are OS-correct: glyphs on macOS (⇧ ⌥, hyphen-joined per
 *   Apple convention), spelled out with `+` elsewhere (Shift+drag).
 * - Toggles describe the control, not the current state (the control's own
 *   visual state already says which way it's set).
 */

// --- store -----------------------------------------------------------------

// Hover tracking is delegated: elements carry a data-help attribute and
// document-level mouseover + pointerdown listeners resolve the nearest hint
// under the pointer. Compared to per-element enter/leave handlers this
// survives nesting (button inside a hoverable tile) and elements unmounting
// mid-hover (removing a block never strands its hint on screen).
//
// `pinned` overrides hover for the duration of an interaction: a knob drag
// can wander off the knob without releasing, so its hint stays pinned until
// mouseup.

const HELP_ATTR = 'data-help';

let hoverText: string | null = null;
let pinned: string | null = null;
const listeners = new Set<() => void>();
const emit = () => listeners.forEach((listener) => listener());

/** Pin a hint for the duration of an interaction (drag/edit). */
export const pinHelp = (text: string) => {
  if (pinned === text) return;
  pinned = text;
  emit();
};

/** Release a pinned hint (no-op if something else pinned since). */
export const unpinHelp = (text: string) => {
  if (pinned !== text) return;
  pinned = null;
  emit();
};

let delegationInstalled = false;
const installDelegation = () => {
  if (delegationInstalled || typeof document === 'undefined') return;
  delegationInstalled = true;

  const update = (text: string | null) => {
    if (hoverText === text) return;
    hoverText = text;
    emit();
  };

  // Touch engines replay a mouse event pair (mouseover, mousemove,
  // mousedown, mouseup, click) after a tap, aimed at the element just
  // tapped. The replay lands *after* pointerup, so it would restore the hint
  // the release below has just cleared: the bar kept captioning the last
  // thing touched, exactly the behaviour the release is there to remove.
  // Ignoring it for a beat is narrower than dropping `mouseover` on touch
  // devices, which would also kill the genuine hover an iPad trackpad
  // produces.
  const MOUSE_REPLAY_MS = 700;
  let lastTouchRelease = -Infinity;

  const resolve = (e: Event) => {
    if (e.type === 'mouseover' && performance.now() - lastTouchRelease < MOUSE_REPLAY_MS) return;
    const el = e.target instanceof Element ? e.target.closest(`[${HELP_ATTR}]`) : null;
    update(el?.getAttribute(HELP_ATTR) ?? null);
  };

  document.addEventListener('mouseover', resolve);
  // Touch-only devices never hover, so pressing a control is the hint
  // trigger there (harmless for mouse users; press implies hover).
  document.addEventListener('pointerdown', resolve);
  // A touch press is the hover equivalent, so the release is the un-hover:
  // the bar shows the control's help for exactly as long as the finger is
  // down, then clears. Leaving stale help pinned to the last thing tapped
  // was the desktop behaviour of a stationary mouse, which on touch just
  // reads as a wrong caption. Mouse and pen releases are ignored, so
  // desktop hover is untouched.
  //
  // On `window` in the capture phase: a control that took pointer capture
  // (knobs, the tile lift) retargets its release, and a bubbling document
  // listener can miss it entirely.
  const releaseTouch = (e: PointerEvent) => {
    if (e.pointerType !== 'touch') return;
    lastTouchRelease = performance.now();
    update(null);
  };
  window.addEventListener('pointerup', releaseTouch, true);
  window.addEventListener('pointercancel', releaseTouch, true);
  // Pointer left the window entirely.
  document.addEventListener('mouseout', (e) => {
    if (e.relatedTarget === null) update(null);
  });
};

const subscribe = (listener: () => void) => {
  installDelegation();
  listeners.add(listener);
  return () => listeners.delete(listener);
};

const snapshot = () => pinned ?? hoverText;

/** Current help line for the pinned readout (null = nothing hovered). */
export const useHelpText = () => useSyncExternalStore(subscribe, snapshot);

/** Spread onto any element to drive the readout from hover. */
export const helpProps = (text: string) => ({ [HELP_ATTR]: text });

// --- visibility preference ---------------------------------------------------

// Whether the hint bar shows at all. A per-machine UI preference, not part
// of the plugin state, so it lives in webview localStorage rather than an
// APVTS parameter (presets/undo shouldn't touch it).
const HINTS_KEY = 't3k.showHints';

let hintsEnabled = (() => {
  try {
    return localStorage.getItem(HINTS_KEY) !== 'false';
  } catch {
    return true;
  }
})();

export const setHintsEnabled = (enabled: boolean) => {
  if (hintsEnabled === enabled) return;
  hintsEnabled = enabled;
  try {
    localStorage.setItem(HINTS_KEY, String(enabled));
  } catch {
    // Storage unavailable; the toggle still works for this session.
  }
  emit();
};

export const useHintsEnabled = () => useSyncExternalStore(subscribe, () => hintsEnabled);

// --- copy ------------------------------------------------------------------

/** OS-correct modifier chords: glyphs + hyphen on macOS (Apple convention),
    spelled out + plus elsewhere. */
const IS_MAC = /Mac|iP(hone|ad|od)/i.test(
  (navigator as { userAgentData?: { platform?: string } }).userAgentData?.platform ??
    navigator.platform
);
const chord = (macGlyph: string, name: string) => (gesture: string) =>
  IS_MAC ? `${macGlyph}-${gesture}` : `${name}+${gesture}`;
const shift = chord('\u21e7', 'Shift');
const alt = chord('\u2325', 'Alt');

/** Shared legend for every KnobControl (they all support these gestures).
    Touch has no modifier keys and no separate click button, so it gets the
    gestures it actually has (see KnobControl). */
const KNOB_KEYS = IS_COARSE_POINTER
  ? 'drag up or down: adjust · double tap: reset · tap the name: type'
  : `${shift('drag')}: fine · double-click: type · ${alt('click')}: reset`;

export const knobHelp = (name: string, desc: string) => `${name}: ${desc} ${KNOB_KEYS}`;

/**
 * Desktop copy. Touch devices re-word it through `touchify` below rather
 * than branching every line: only the entries whose *gesture* differs
 * (knobs, EQ faders and dots) are branched by hand, and everything else
 * differs only in the noun for "press this", which one pass can do without
 * letting the two wordings drift apart.
 */
const HELP_DESKTOP = {
  // Faceplate: gains
  inputLevel: knobHelp('Input', 'chain input level, ±24 dB.'),
  inputMode: 'Input Mode: source channels. Stereo: both · L/R: one. Click: choose.',
  outputLevel: knobHelp('Output', 'master output level, ±24 dB.'),
  outputBalance: knobHelp('Balance', 'level trim between chains, ±12 dB (pre-pan). Center: off.'),
  autoBalance: 'Auto Balance: click, play ~2 s to match chain levels. Click again: cancel.',

  // Faceplate: gate, tone stack, stereo image (spread / align)
  gate: knobHelp('Gate', 'noise gate threshold, -100 to 0 dB.'),
  gatePower: 'Gate Power: noise gate on/off.',
  toneBass: knobHelp('Bass', 'tone stack lows, 0-10: ±20 dB shelf at 150 Hz.'),
  toneMiddle: knobHelp('Middle', 'tone stack mids, 0-10: ±15 dB bell at 425 Hz.'),
  toneTreble: knobHelp('Treble', 'tone stack highs, 0-10: ±10 dB shelf at 1.8 kHz.'),
  tonePower: 'Tone Stack Power: Bass/Middle/Treble on/off.',
  spreadOffset: knobHelp(
    'Offset',
    'double-track lag, ≤24 ms toward L or R. Center: off. Right-click: advanced.'
  ),
  spreadWobble: knobHelp('Wobble', 'humanizing delay drift, up to ±1.2 ms.'),
  spreadWobblePower: 'Wobble Power: delay drift on/off. Off: a static, more comb-like double.',
  spreadCrossover: knobHelp('Crossover', 'lows below the cutoff stay dual-mono, 33-520 Hz.'),
  spreadCrossoverPower: 'Crossover Power: off doubles the full band (lows lose mono safety).',
  spreadDiffuse: 'Diffuse Power: phase-decorrelates the lagged side. Off: a pure delay.',
  spreadAdvert: 'Spread: mono-to-stereo double via a wobbling short lag. Click: enable.',
  // On touch the advanced deck answers a hold on the Offset knob only (see
  // SpreadControls / AlignControls), so the power rows drop the tail that
  // touchify would otherwise turn into a false "touch and hold" promise.
  spreadPower: IS_COARSE_POINTER
    ? 'Spread Power: spread off; collapses its controls.'
    : 'Spread Power: spread off; collapses its controls. Right-click: advanced.',
  imageCorrelation: 'Mono safety: dim: safe · yellow: caution · red: cancellation on mono sum.',
  spreadMonoOutput:
    'Spread: unavailable, the output is mono (mono track or one-channel output device).',
  alignOffset: knobHelp(
    'Offset',
    'corrective chain alignment, ≤24 ms toward L or R. Center: off. Right-click: advanced.'
  ),
  alignWobble: knobHelp('Wobble', 'humanizing drift of the align delay, up to ±1.2 ms.'),
  alignWobblePower: 'Wobble Power: drifts the delayed chain like an ADT double-track.',
  alignCrossover: knobHelp('Crossover', 'lows below the cutoff skip the deck, 33-520 Hz.'),
  alignCrossoverPower: 'Crossover Power: on keeps lows out of the delay and diffusion.',
  alignDiffuse: 'Diffuse Power: phase-decorrelates the delayed chain for width.',
  alignAdvert: 'Align: corrective chain time alignment. Click: enable.',
  alignPower: IS_COARSE_POINTER
    ? 'Align Power: align off; collapses its controls.'
    : 'Align Power: align off; collapses its controls. Right-click: advanced.',
  autoAlign:
    'Auto Align: a ½ s internal sweep time-aligns the chains and fixes inverted polarity. Click again: cancel.',

  // Top bar
  tuner: 'Tuner: chromatic tuner. Click again: back.',
  undo: 'Undo: revert last chain edit.',
  redo: 'Redo: re-apply undone edit.',
  settings: 'Settings: plugin and audio options.',
  account: 'Account: settings and TONE3000 sign-out.',
  monoMode: 'Mono: one chain, both outputs.',
  stereoMode: 'Stereo: independent Left/Right chains.',

  // Presets
  presetPrev: 'Previous Preset: step back through the list.',
  presetNext: 'Next Preset: step forward through the list.',
  presetBrowse: 'Presets: browse factory and user presets.',
  presetSave: 'Save Preset: store the current chain. Same name: overwrite.',
  presetNew: 'New: clear the chain and reset every control to its default.',
  presetRename: 'Rename: edit name. Enter: commit · Esc: cancel.',
  presetDelete: 'Delete: remove this preset.',
  presetReorder: 'Reorder: drag presets into a custom order. Prev/Next and MIDI follow it.',
  presetDrag: 'Drag: move this preset within its section.',
  presetPcToggle:
    'MIDI PC: show each preset\u2019s program change number. Prev/Next and PC follow the list order.',
  presetPc: 'PC: the MIDI program change number that loads this preset.',

  // Chain gallery
  addTile:
    'Add Tone: browse TONE3000 for this slot, or drop a .nam or IR .wav file (or a folder of them). Right-click: paste / load file · drag: move.',
  closeToneBrowser: 'Close: back to the chain.',
  copyBlock: 'Copy: copy this block (tone, model and all settings).',
  pasteBlock: 'Paste: add a copy of the copied block in this slot.',
  loadFileTile: 'Load File: pick a local .nam or IR .wav file to load here. No account needed.',
  loadFolderTile:
    'Load Folder: pick a folder of .nam or .wav files; loads as one multi-model block.',
  blockPower: 'Power: bypass this block.',
  retryLoad: 'Retry: re-download this model.',
  swapTone: 'Swap: replace this tone, keeping its slot.',
  removeBlock: 'Remove: delete this block.',
  panLeft: knobHelp('Pan L', 'Left chain, hard left ↔ center.'),
  panRight: knobHelp('Pan R', 'Right chain, center ↔ hard right.'),
  panLink: 'Link Pans: mirror both pan knobs.',
  monoSum:
    'Mono output: both chains summed to one channel. Bal/S/Ø still shape the blend; pans are off.',
  panMonoSum: 'Pan: unavailable, the output is mono. The chains are summed instead (see MONO).',
  soloLeft: 'Solo L: hear the Left chain alone.',
  soloRight: 'Solo R: hear the Right chain alone.',
  invertLeft: 'Invert L: flip the Left chain polarity. Fixes chains that hollow out or cancel.',
  invertRight: 'Invert R: flip the Right chain polarity. Fixes chains that hollow out or cancel.',
  swapChains: 'Swap Chains: exchange Left/Right chains.',
  branchGap: 'Branch: feed the other chain from this point in this chain.',
  branchJunction: 'Branch Point: the other chain starts here. Click: make chains independent.',

  // Block card
  blockIn: knobHelp('In', 'block input gain, ±24 dB.'),
  blockOut: knobHelp('Out', 'block output gain, ±24 dB.'),
  blockOutIr: knobHelp('Out', 'block output gain, ±24 dB (IR pre-trimmed -18 dB).'),
  blockMix: knobHelp('Mix', 'dry/wet blend.'),
  blockNormalize: 'Normalize: level this block\u2019s loudness. Off: raw capture level.',
  blockNormalizeOverridden:
    'Normalize: overridden \u2014 calibration hands this model\u2019s true output level to the next NAM block.',
  blockSize: 'NAM Size: LITE saves CPU · FULL is highest quality. Sets this block only.',
  blockSizeChip:
    'NAM Size: this block\u2019s size differs from your default. To choose per block, enable it in Settings.',
  blockCalibrated: 'Calibration data: present \u2014 turn Calibration on to set levels from it.',
  blockUncalibrated:
    'Calibration data: none \u2014 this capture never recorded its input level, so Calibration can\u2019t help it. Set the drive with the In knob.',
  eqToggle: 'EQ: 6-band EQ editor. Outline: EQ shaping the sound.',
  toneInfo: 'Info: tone description, makes, and tags from TONE3000.',
  toneInfoLogin: 'Log In: sign in to TONE3000 to see tone details.',
  viewOnT3k: 'View on TONE3000: open this tone in your browser.',
  favoriteTone: 'Favorite: save this tone to your TONE3000 favorites.',
  unfavoriteTone: 'Favorited: click to remove from your TONE3000 favorites.',
  eqSlidersView: 'Sliders: gain-only fader view.',
  eqCurveView: 'Curve: parametric freq/gain/Q editor.',
  eqReset: 'Reset EQ: all bands flat, position post.',
  eqPre: 'PRE: EQ before the model. Off: after the model (wet only).',
  eqPower: 'EQ Power: bypass EQ, keep settings.',
  shareTone: 'Share: copy TONE3000 link.',
  modelSelectSignedOut: 'Models: sign in to TONE3000 to switch models.',
  backToChain: 'Back: chain overview.',

  // EQ editor
  eqFader: IS_COARSE_POINTER
    ? 'Band Fader: gain, ±15 dB. drag: adjust · double tap: reset.'
    : `Band Fader: gain, ±15 dB. ${shift('drag')}: fine · double-click / ${alt('click')}: reset.`,
  eqFaderPass: 'Pass Band: no gain. Shape it in Curve view.',
  eqDot: IS_COARSE_POINTER
    ? 'Band Dot: drag: freq + gain · double tap: reset. Q: use the Q chip.'
    : `Band Dot: drag: freq + gain · scroll: Q · ${shift('drag')}: fine · ${alt('click')}: reset.`,
  eqFreqChip:
    'Freq: click to type (\u201c800\u201d, \u201c1.2k\u201d). Enter: commit · Esc: cancel.',
  eqGainChip: 'Gain: click to type, ±15 dB. Enter: commit · Esc: cancel.',
  eqQChip: IS_COARSE_POINTER
    ? 'Q: tap to type. Enter: commit · Esc: cancel.'
    : `Q: scroll the graph (${shift('scroll')}: fine) or click to type.`,

  // Meters
  clipDot: 'Clip: latches on clipping. Click: clear.',

  // The hint bar itself
  cpuLoad: 'CPU: audio engine load.',
  hideHints: 'Hide Info Bar: hide this bar. Re-enable in Settings.',
} as const;

/**
 * Desktop pointer vocabulary rewritten for touch. `Right-click` first, since
 * it contains `click`; everything a right-click reaches (context menus, the
 * advanced Spread/Align decks) answers a touch and hold on a touch screen.
 */
const TOUCH_WORDING: readonly (readonly [RegExp, string])[] = [
  [/Right-click/g, 'Touch and hold'],
  [/right-click/g, 'touch and hold'],
  [/Click/g, 'Tap'],
  [/click/g, 'tap'],
];

const touchify = (copy: Record<string, string>): Record<string, string> =>
  Object.fromEntries(
    Object.entries(copy).map(([key, text]) => [
      key,
      TOUCH_WORDING.reduce(
        (acc, [pattern, replacement]) => acc.replace(pattern, replacement),
        text
      ),
    ])
  );

export const HELP = (IS_COARSE_POINTER ? touchify(HELP_DESKTOP) : HELP_DESKTOP) as Record<
  keyof typeof HELP_DESKTOP,
  string
>;

/** Gallery tile: leads with the tone's own name. */
export const toneTileHelp = (title: string) =>
  IS_COARSE_POINTER
    ? `${title}. Tap: open · drag: reorder · touch and hold: menu.`
    : `${title}. Click: open · drag: reorder · ${alt('drag')}: duplicate · right-click: copy / load file.`;

/** Curve-type selector buttons in the EQ editor. */
export const bandTypeHelp = (label: string) => `${label}: band curve shape.`;
