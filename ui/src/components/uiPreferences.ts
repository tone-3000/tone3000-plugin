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

// Whether the faceplate tone stack exposes its optional PRE (pre-EQ) toggle.
// Off by default: the tone stack runs post-chain unless the user opts in.
const preEqControl = boolPref('t3k.showPreEqControl');
export const setPreEqControlEnabled = preEqControl.set;
export const usePreEqControlEnabled = preEqControl.useValue;

// Whether NAM block cards expose the (=) per-block normalization toggle.
// Off by default: every block simply stays normalized (the block flag itself
// defaults to on and lives in the chain state, not here).
const blockNormalizeControl = boolPref('t3k.showBlockNormalizeControl');
export const setBlockNormalizeControlEnabled = blockNormalizeControl.set;
export const useBlockNormalizeControlEnabled = blockNormalizeControl.useValue;
