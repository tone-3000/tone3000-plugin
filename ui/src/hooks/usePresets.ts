import { useCallback, useEffect, useMemo, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { PresetInfo } from '../types/chain';

/**
 * Internal preset store access.
 *
 * Split of responsibilities with useChainState:
 * - The preset *list* lives here, fetched on demand (mount + after every
 *   mutation), never polled. It's a handful of names, and native rescans the
 *   shared presets folder on each call so other plugin instances' saves show
 *   up too.
 * - The *active* preset rides the chain state (it only changes together with
 *   a revision bump), so it stays out of this hook entirely.
 *
 * `onChanged` is called after any mutation that affects chain/param state or
 * the active preset. The owner wires it to useChainState's refresh so the
 * UI converges immediately instead of waiting for the next poll.
 */
export function usePresets(onChanged?: () => void) {
  const backend = useAudioBackend();

  const native = useMemo(
    () => ({
      getPresetList: backend.getPluginFunction('getPresetList'),
      savePreset: backend.getPluginFunction('savePreset'),
      loadPreset: backend.getPluginFunction('loadPreset'),
      renamePreset: backend.getPluginFunction('renamePreset'),
      deletePreset: backend.getPluginFunction('deletePreset'),
      movePreset: backend.getPluginFunction('movePreset'),
    }),
    [backend]
  );

  const [presets, setPresets] = useState<PresetInfo[]>([]);

  const refreshList = useCallback(async () => {
    try {
      const res = (await native.getPresetList()) as { presets?: PresetInfo[] } | null;
      setPresets(res?.presets ?? []);
    } catch (error) {
      console.error('Error loading preset list:', error);
    }
  }, [native]);

  useEffect(() => {
    refreshList();
  }, [refreshList]);

  /** Run a preset mutation, then resync the list and the chain state. The
      native bridge is untyped, so T asserts each call's known return shape. */
  const run = useCallback(
    async <T>(label: string, fn: () => Promise<unknown>): Promise<T | null> => {
      let result: T | null = null;
      try {
        result = (await fn()) as T;
      } catch (error) {
        console.error(`Preset action failed (${label}):`, error);
      }
      await refreshList();
      onChanged?.();
      return result;
    },
    [refreshList, onChanged]
  );

  const actions = useMemo(
    () => ({
      /** Save current state under `name` (same-name user preset is
          overwritten). Resolves to the new { id, name } or null. */
      save: (name: string) =>
        run<{ id: string; name: string } | null>('savePreset', () => native.savePreset(name)),
      load: (id: string) => run<boolean>('loadPreset', () => native.loadPreset(id)),
      rename: (id: string, name: string) =>
        run<boolean>('renamePreset', () => native.renamePreset(id, name)),
      remove: (id: string) => run<boolean>('deletePreset', () => native.deletePreset(id)),
      /** One step up (-1) / down (+1) within the preset's section. The order
          persists and drives prev/next and MIDI program-change numbers. */
      move: (id: string, delta: -1 | 1) =>
        run<boolean>('movePreset', () => native.movePreset(id, delta)),
    }),
    [native, run]
  );

  return { presets, refreshList, actions };
}
