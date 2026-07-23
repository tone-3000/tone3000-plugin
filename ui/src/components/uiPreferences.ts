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

/**
 * How the Advanced "Default NAM A2 Size" radios behave:
 *   lite / full — global default; block cards hide the LITE/FULL control
 *                 (a static label appears only when a loaded block differs)
 *   perTone     — LITE/FULL toggle on each NAM block; Set Default picks the
 *                 size used for new Select Tone loads
 *
 * Migrates from the older size-only preference: if the user had Full saved
 * and no mode yet, land on the Full radio (not Choose per tone).
 */
export type NamA2SizeMode = 'lite' | 'full' | 'perTone';
const NAM_A2_MODE_KEY = 't3k.namA2SizeMode';

const parseNamA2SizeMode = (raw: string | null): NamA2SizeMode | null => {
  if (raw === 'lite' || raw === 'full' || raw === 'perTone') return raw;
  return null;
};

let namA2SizeModeValue: NamA2SizeMode = (() => {
  try {
    const stored = parseNamA2SizeMode(localStorage.getItem(NAM_A2_MODE_KEY));
    if (stored) return stored;
  } catch {
    // fall through
  }
  // No mode yet — mirror the already-loaded size preference.
  return namA2SizeValue;
})();

export const setNamA2SizeMode = (mode: NamA2SizeMode) => {
  if (namA2SizeModeValue === mode) return;
  namA2SizeModeValue = mode;
  try {
    localStorage.setItem(NAM_A2_MODE_KEY, mode);
  } catch {
    // Storage unavailable — the choice still works for this session.
  }
  // Picking A2-Lite / A2-Full also sets the Select Tone default so the two
  // stay aligned; Choose per tone keeps the existing Set Default value.
  if (mode === 'lite' || mode === 'full') setDefaultNamA2Size(mode);
  emit();
};

export const useNamA2SizeMode = () => useSyncExternalStore(subscribe, () => namA2SizeModeValue);

/** True when Advanced is set to "Choose per tone" (interactive LITE/FULL). */
export const useNamA2ChoosePerTone = () =>
  useSyncExternalStore(subscribe, () => namA2SizeModeValue === 'perTone');
