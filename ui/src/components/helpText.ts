import { useSyncExternalStore } from 'react';

/**
 * Central help system: every control publishes a one-line hint here while
 * hovered (or mid-interaction), and the faceplate's pinned readout renders
 * whatever is current — Native Instruments style, instead of browser
 * tooltips. All copy lives in this file so wording stays consistent.
 *
 * Copy conventions:
 * - `Name: what it does.` then shortcut legend, `Key: effect` pairs joined
 *   with middots.
 * - Toggles describe the control, not the current state (the control's own
 *   visual state already says which way it's set).
 */

// --- store -----------------------------------------------------------------

// Hover tracking is delegated: elements carry a data-help attribute and one
// document-level mouseover listener resolves the nearest hint under the
// pointer. Compared to per-element enter/leave handlers this survives
// nesting (button inside a hoverable tile) and elements unmounting
// mid-hover (removing a block never strands its hint on screen).
//
// `pinned` overrides hover for the duration of an interaction — a knob drag
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

  document.addEventListener('mouseover', (e) => {
    const el = e.target instanceof Element ? e.target.closest(`[${HELP_ATTR}]`) : null;
    update(el?.getAttribute(HELP_ATTR) ?? null);
  });
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
    // Storage unavailable — the toggle still works for this session.
  }
  emit();
};

export const useHintsEnabled = () => useSyncExternalStore(subscribe, () => hintsEnabled);

// --- copy ------------------------------------------------------------------

/** Shared legend for every KnobControl (they all support these gestures). */
const KNOB_KEYS = 'Drag: adjust · Shift: fine · Double-click: type value · Alt-click: reset';

export const knobHelp = (name: string, desc: string) => `${name}: ${desc} — ${KNOB_KEYS}`;

export const HELP = {
  // Faceplate — gains
  inputLevel: knobHelp('Input', 'level into the chain, ±24 dB.'),
  outputLevel: knobHelp('Output', 'master level after the chain, ±24 dB.'),
  inputBalance: knobHelp(
    'Input Balance',
    'trims input L/R against each other, ±12 dB. Center: off.'
  ),
  outputBalance: knobHelp(
    'Output Balance',
    'trims output L/R against each other, ±12 dB. Center: off.'
  ),
  autoBalance:
    'Auto Balance: matches L/R output level. Click: arm, then play ~2 s · Click again: cancel.',

  // Faceplate — gate, tone stack, spread
  gate: knobHelp('Gate', 'noise gate threshold, −100 to 0 dB.'),
  gatePower: 'Gate Power: toggles the noise gate on/off.',
  toneBass: knobHelp('Bass', 'tone stack lows, 0–10.'),
  toneMiddle: knobHelp('Middle', 'tone stack mids, 0–10.'),
  toneTreble: knobHelp('Treble', 'tone stack highs, 0–10.'),
  tonePower: 'Tone Stack Power: toggles the Bass/Middle/Treble EQ on/off.',
  tonePre: 'PRE: runs the tone stack before the chain. Off: after the chain.',
  spread: knobHelp(
    'Spread',
    'delays one side up to 24 ms for width. Center: off · Left/right of center: delays L/R.'
  ),
  jitter: knobHelp('Jitter', 'adds up to 4 ms of random drift to the spread delay.'),
  spreadPower: 'Spread Power: toggles the stereo spread on/off.',

  // Top bar
  tuner: 'Tuner: opens the chromatic tuner. Click again: back to the chain.',
  undo: 'Undo: reverts the last chain edit.',
  redo: 'Redo: re-applies the last undone edit.',
  settings: 'Settings: audio input mode and advanced options.',
  account: 'Account: settings and TONE3000 sign-out.',
  monoMode: 'Mono: one chain feeds both outputs.',
  stereoMode: 'Stereo: independent Left and Right chains.',

  // Presets
  presetPrev: 'Previous Preset: steps backward through the preset list.',
  presetNext: 'Next Preset: steps forward through the preset list.',
  presetBrowse: 'Presets: opens the browser — factory and user presets, with search.',
  presetSave: 'Save Preset: stores the current chain. Same name: overwrites in place.',
  presetRename: 'Rename: edits this preset\u2019s name. Enter: commit · Esc: cancel.',
  presetDelete: 'Delete: removes this preset.',

  // Chain gallery
  addTile: 'Add Tone: opens the TONE3000 tone browser for this slot. Drag grip: move the insert point.',
  closeToneBrowser: 'Close: back to the chain without picking a tone.',
  dragGrip: 'Grip: drag to reorder. In stereo, drop on the other lane to move chains.',
  blockPower: 'Power: bypasses this block.',
  retryLoad: 'Retry: downloads this tone\u2019s model again.',
  swapTone: 'Swap: replaces this tone with a new TONE3000 pick, keeping its slot.',
  removeBlock: 'Remove: deletes this block from the chain.',
  panLeft: knobHelp('Pan L', 'pans the Left chain between hard left and center.'),
  panRight: knobHelp('Pan R', 'pans the Right chain between center and hard right.'),
  panLink: 'Link Pans: mirrors both pan knobs so width changes stay symmetric.',
  swapChains: 'Swap Chains: exchanges the Left and Right chains, pans included.',

  // Block card
  blockIn: knobHelp('In', 'gain into this block, ±24 dB.'),
  blockOut: knobHelp('Out', 'gain out of this block, ±24 dB.'),
  blockOutIr: knobHelp('Out', 'gain out of this block, ±24 dB (IR level is pre-trimmed 18 dB).'),
  blockMix: knobHelp('Mix', 'dry/wet blend — 100%: fully processed.'),
  namLite: 'LITE: half-size NAM model — lighter on CPU, slightly less detail.',
  namFull: 'FULL: full-size NAM model — best quality.',
  eqToggle: 'EQ: opens the 6-band EQ editor. Outline: EQ is shaping the sound.',
  eqSlidersView: 'Sliders View: graphic-EQ faders, gain only.',
  eqCurveView: 'Curve View: parametric editor — frequency, gain, Q and band type.',
  eqReset: 'Reset EQ: returns all six bands to flat.',
  eqPower: 'EQ Power: bypasses the block EQ without losing its settings.',
  shareTone: 'Share: copies this tone\u2019s TONE3000 link to the clipboard.',
  modelSelectSignedOut: 'Models: sign in to TONE3000 (account menu) to switch models.',
  backToChain: 'Back: returns to the chain overview.',

  // EQ editor
  eqFader: 'Band Fader: gain, ±15 dB. Shift: fine · Double-click / Alt-click: reset flat.',
  eqFaderPass: 'Pass Band: no gain to set — shape it in the Curve view.',
  eqDot:
    'Band Dot: drag for freq + gain (cuts: vertical sets Q). Scroll: Q · Shift: fine · Alt-click: reset.',
  eqFreqChip:
    'Freq: click to type — accepts \u201c800\u201d or \u201c1.2k\u201d. Enter: commit · Esc: cancel.',
  eqGainChip: 'Gain: click to type, ±15 dB. Enter: commit · Esc: cancel.',
  eqQChip: 'Q: scroll over the graph (Shift: fine) or click to type.',

  // Meters
  clipDot: 'Clip: latches when the signal clips. Click: clear.',

  // The hint bar itself
  hideHints: 'Hide Hints: turns this hint bar off. Re-enable it in Settings.',
} as const;

/** Gallery tile: leads with the tone's own name. */
export const toneTileHelp = (title: string) => `${title} — Click: open editor.`;

/** Curve-type selector buttons in the EQ editor. */
export const bandTypeHelp = (label: string) => `${label}: sets this band\u2019s curve shape.`;
