import { useSyncExternalStore } from 'react';

/**
 * Central help system: every control publishes a one-line hint here while
 * hovered (or mid-interaction), and the faceplate's pinned readout renders
 * whatever is current, Native Instruments style, instead of browser
 * tooltips. All copy lives in this file so wording stays consistent.
 *
 * Copy conventions:
 * - `Name: what it does.` then a shortcut legend of `key: effect` pairs
 *   joined with middots. Keep it terse: the bar shares its row with the
 *   A2-size toggle and CPU readout, and long lines get ellipsized.
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

  const resolve = (e: Event) => {
    const el = e.target instanceof Element ? e.target.closest(`[${HELP_ATTR}]`) : null;
    update(el?.getAttribute(HELP_ATTR) ?? null);
  };

  document.addEventListener('mouseover', resolve);
  // Touch-only devices never hover, so pressing a control is the hint
  // trigger there (harmless for mouse users; press implies hover). The
  // hint stays up after the tap until the next press lands elsewhere.
  document.addEventListener('pointerdown', resolve);
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

/** Shared legend for every KnobControl (they all support these gestures). */
const KNOB_KEYS = `${shift('drag')}: fine · double-click: type · ${alt('click')}: reset`;

export const knobHelp = (name: string, desc: string) => `${name}: ${desc} ${KNOB_KEYS}`;

export const HELP = {
  // Faceplate: gains
  inputLevel: knobHelp('Input', 'chain input level, ±24 dB.'),
  inputMode: 'Input Mode: source channels. Stereo: both · L/R: one. Click: choose.',
  outputLevel: knobHelp('Output', 'master output level, ±24 dB.'),
  outputBalance: knobHelp('Balance', 'level trim between chains, ±12 dB (pre-pan). Center: off.'),
  autoBalance: 'Auto Balance: click, play ~2 s to match chain levels. Click again: cancel.',

  // Faceplate: gate, tone stack, stereo image (spread / offset)
  gate: knobHelp('Gate', 'noise gate threshold, -100 to 0 dB.'),
  gatePower: 'Gate Power: noise gate on/off.',
  toneBass: knobHelp('Bass', 'tone stack lows, 0-10.'),
  toneMiddle: knobHelp('Middle', 'tone stack mids, 0-10.'),
  toneTreble: knobHelp('Treble', 'tone stack highs, 0-10.'),
  tonePower: 'Tone Stack Power: Bass/Middle/Treble on/off.',
  spreadOffset: knobHelp(
    'Offset',
    'double-track lag, ≤24 ms toward L or R. Center: off. Right-click: advanced.'
  ),
  spreadWobble: knobHelp('Wobble', 'humanizing delay drift, up to ±1.2 ms.'),
  spreadAdvert: 'Spread: mono-to-stereo double via a wobbling short lag. Click: enable.',
  spreadPower: 'Spread Power: spread off; collapses its controls. Right-click: advanced.',
  spreadCorrelation:
    'Mono safety: dim: safe · yellow: caution · red: cancellation on mono sum.',
  offsetTime: knobHelp('Offset', 'corrective chain alignment, ≤24 ms toward L or R. Center: off.'),
  offsetPower: 'Offset Power: corrective chain alignment on/off.',
  autoOffset: 'Auto Offset: click, play ~2 s to time-align chains. Click again: cancel.',

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
  presetRename: 'Rename: edit name. Enter: commit · Esc: cancel.',
  presetDelete: 'Delete: remove this preset.',
  presetReorder: 'Reorder: custom preset order. Prev/Next and MIDI follow it.',
  presetMoveUp: 'Move Up: one spot earlier.',
  presetMoveDown: 'Move Down: one spot later.',

  // Chain gallery
  addTile: 'Add Tone: browse TONE3000 for this slot. Right-click: paste · drag tile or grip: move.',
  closeToneBrowser: 'Close: back to the chain.',
  dragGrip: `Grip: drag to reorder · ${alt('drag')}: duplicate. Stereo: drop on the other lane to move.`,
  copyBlock: 'Copy: copy this block (tone, model and all settings).',
  pasteBlock: 'Paste: add a copy of the copied block in this slot.',
  blockPower: 'Power: bypass this block.',
  retryLoad: 'Retry: re-download this model.',
  swapTone: 'Swap: replace this tone, keeping its slot.',
  removeBlock: 'Remove: delete this block.',
  panLeft: knobHelp('Pan Left', 'Left chain, hard left ↔ center.'),
  panRight: knobHelp('Pan Right', 'Right chain, center ↔ hard right.'),
  panLink: 'Link Pans: mirror both pan knobs.',
  swapChains: 'Swap Chains: exchange Left/Right chains, pans included.',
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
  blockCalibrated: 'Calibration: active \u2014 levels set from this model\u2019s calibration data.',
  blockUncalibrated: 'Calibration: inactive \u2014 this model has no calibration data.',
  eqToggle: 'EQ: 6-band EQ editor. Outline: EQ shaping the sound.',
  eqSlidersView: 'Sliders: gain-only fader view.',
  eqCurveView: 'Curve: parametric freq/gain/Q editor.',
  eqReset: 'Reset EQ: all bands flat, position post.',
  eqPre: 'PRE: EQ before the model. Off: after the block.',
  eqPower: 'EQ Power: bypass EQ, keep settings.',
  shareTone: 'Share: copy TONE3000 link.',
  modelSelectSignedOut: 'Models: sign in to TONE3000 to switch models.',
  backToChain: 'Back: chain overview.',

  // EQ editor
  eqFader: `Band Fader: gain, ±15 dB. ${shift('drag')}: fine · double-click / ${alt(
    'click'
  )}: reset.`,
  eqFaderPass: 'Pass Band: no gain. Shape it in Curve view.',
  eqDot: `Band Dot: drag: freq + gain · scroll: Q · ${shift('drag')}: fine · ${alt(
    'click'
  )}: reset.`,
  eqFreqChip: 'Freq: click to type (\u201c800\u201d, \u201c1.2k\u201d). Enter: commit · Esc: cancel.',
  eqGainChip: 'Gain: click to type, ±15 dB. Enter: commit · Esc: cancel.',
  eqQChip: `Q: scroll the graph (${shift('scroll')}: fine) or click to type.`,

  // Meters
  clipDot: 'Clip: latches on clipping. Click: clear.',

  // The hint bar itself
  namSize: 'NAM Size: LITE saves CPU · FULL is highest quality. Applies to all NAM tones.',
  cpuLoad: 'CPU: audio engine load.',
  hideHints: 'Hide Info Bar: hide this bar. Re-enable in Settings.',
} as const;

/** Gallery tile: leads with the tone's own name. */
export const toneTileHelp = (title: string) =>
  `${title}. Click: open · drag: reorder · right-click: copy.`;

/** Curve-type selector buttons in the EQ editor. */
export const bandTypeHelp = (label: string) => `${label}: band curve shape.`;
