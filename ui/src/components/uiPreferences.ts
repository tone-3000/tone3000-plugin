import { useSyncExternalStore } from 'react';

/**
 * Per-machine UI preferences that aren't part of the plugin state (they must
 * not ride presets/undo or APVTS automation), so they live in the webview's
 * localStorage with a tiny external store for reactive reads.
 */

const listeners = new Set<() => void>();
const emit = () => listeners.forEach((listener) => listener());
const subscribe = (listener: () => void) => {
  listeners.add(listener);
  return () => listeners.delete(listener);
};

/** Boolean preference backed by localStorage (off by default). */
function boolPref(key: string) {
  let value = (() => {
    try {
      return localStorage.getItem(key) === 'true';
    } catch {
      return false;
    }
  })();

  const set = (enabled: boolean) => {
    if (value === enabled) return;
    value = enabled;
    try {
      localStorage.setItem(key, String(enabled));
    } catch {
      // Storage unavailable — the toggle still works for this session.
    }
    emit();
  };

  const useValue = () => useSyncExternalStore(subscribe, () => value);
  return { set, useValue };
}

// Whether NAM block cards expose the (=) per-block normalization toggle.
// Off by default: every block simply stays normalized (the block flag itself
// defaults to on and lives in the chain state, not here).
const blockNormalizeControl = boolPref('t3k.showBlockNormalizeControl');
export const setBlockNormalizeControlEnabled = blockNormalizeControl.set;
export const useBlockNormalizeControlEnabled = blockNormalizeControl.useValue;

// Default NAM A2 tier for tones loaded through the Select flow ('lite' by
// default). Only seeds *new* loads/swaps — a block's size then lives in the
// chain state and rides presets/undo, so changing this never touches
// existing chains.
export type NamA2Size = 'lite' | 'full';
const NAM_A2_SIZE_KEY = 't3k.defaultNamA2Size';

let namA2SizeValue: NamA2Size = (() => {
  try {
    return localStorage.getItem(NAM_A2_SIZE_KEY) === 'full' ? 'full' : 'lite';
  } catch {
    return 'lite';
  }
})();

export const setDefaultNamA2Size = (size: NamA2Size) => {
  if (namA2SizeValue === size) return;
  namA2SizeValue = size;
  try {
    localStorage.setItem(NAM_A2_SIZE_KEY, size);
  } catch {
    // Storage unavailable — the choice still works for this session.
  }
  emit();
};

export const useDefaultNamA2Size = () => useSyncExternalStore(subscribe, () => namA2SizeValue);

/** The preference as the numeric slimmable size loadTone/swapTone expect
    (0.0 = lite, 1.0 = full). Non-hook: read at call time by chain actions. */
export const getDefaultNamA2SlimmableSize = () => (namA2SizeValue === 'full' ? 1.0 : 0.0);
