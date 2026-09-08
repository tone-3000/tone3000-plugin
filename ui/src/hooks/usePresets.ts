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
      addPresetCategory: backend.getPluginFunction('addPresetCategory'),
      deletePresetCategory: backend.getPluginFunction('deletePresetCategory'),
      setPresetCategory: backend.getPluginFunction('setPresetCategory'),
      movePresetsToCategory: backend.getPluginFunction('movePresetsToCategory'),
      setPresetFavorite: backend.getPluginFunction('setPresetFavorite'),
      setPresetsFavorite: backend.getPluginFunction('setPresetsFavorite'),
      duplicatePresets: backend.getPluginFunction('duplicatePresets'),
      deletePresets: backend.getPluginFunction('deletePresets'),
    }),
    [backend]
  );

  const [presets, setPresets] = useState<PresetInfo[]>([]);
  const [categories, setCategories] = useState<string[]>([]);

  const refreshList = useCallback(async () => {
    try {
      const res = (await native.getPresetList()) as {
        presets?: PresetInfo[];
        categories?: string[];
      } | null;
      setPresets(res?.presets ?? []);
      setCategories(res?.categories ?? []);
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
      /** N steps within the preset's section (negative = earlier). The order
          persists and drives prev/next and MIDI program-change numbers. */
      move: (id: string, delta: number) =>
        run<boolean>('movePreset', () => native.movePreset(id, delta)),
      /** Create a new user category. */
      addCategory: (name: string) =>
        run<boolean>('addPresetCategory', () => native.addPresetCategory(name)),
      /** Delete a user category (presets inside move to root). */
      deleteCategory: (name: string) =>
        run<boolean>('deletePresetCategory', () => native.deletePresetCategory(name)),
      /** Assign a preset to a category (or "" for root). */
      setCategory: (id: string, category: string) =>
        run<boolean>('setPresetCategory', () => native.setPresetCategory(id, category)),
      /** Move multiple presets to a category (or "" for root). */
      movePresetsToCategory: (ids: string[], category: string) =>
        run<boolean>('movePresetsToCategory', () => native.movePresetsToCategory(ids, category)),
      /** Toggle favourite / star status. */
      setFavorite: (id: string, isFavorite: boolean) =>
        run<boolean>('setPresetFavorite', () => native.setPresetFavorite(id, isFavorite)),
      /** Set favourite / star status for multiple presets. */
      setFavorites: (ids: string[], isFavorite: boolean) =>
        run<boolean>('setPresetsFavorite', () => native.setPresetsFavorite(ids, isFavorite)),
      /** Duplicate one or more presets (prepends "Copy-"). */
      duplicatePresets: (ids: string[]) =>
        run<PresetInfo[]>('duplicatePresets', () => native.duplicatePresets(ids)),
      /** Delete multiple presets. */
      deletePresets: (ids: string[]) =>
        run<boolean>('deletePresets', () => native.deletePresets(ids)),
    }),
    [native, run]
  );

  return { presets, categories, refreshList, actions };
}
