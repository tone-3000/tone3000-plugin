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

// Whether the faceplate tone stack exposes its optional PRE (pre-EQ) toggle.
// Off by default: the tone stack runs post-chain unless the user opts in.
const PRE_EQ_CONTROL_KEY = 't3k.showPreEqControl';

let preEqControlEnabled = (() => {
  try {
    return localStorage.getItem(PRE_EQ_CONTROL_KEY) === 'true';
  } catch {
    return false;
  }
})();

export const setPreEqControlEnabled = (enabled: boolean) => {
  if (preEqControlEnabled === enabled) return;
  preEqControlEnabled = enabled;
  try {
    localStorage.setItem(PRE_EQ_CONTROL_KEY, String(enabled));
  } catch {
    // Storage unavailable — the toggle still works for this session.
  }
  emit();
};

export const usePreEqControlEnabled = () =>
  useSyncExternalStore(subscribe, () => preEqControlEnabled);
