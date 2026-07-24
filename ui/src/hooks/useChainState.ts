import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type {
  BlockParamName,
  ChainSide,
  ChainState,
  ChainStateResponse,
  EqBand,
  InputMode,
} from '../types/chain';
import { isUnchanged } from '../types/chain';
import { getDefaultNamA2SlimmableSize } from '../components/uiPreferences';

/**
 * Fallback poll cadence for chain state. The primary sync channel is the
 * native `chainChanged` push event (the editor watches the revision counter
 * and emits within ~50 ms of any mutation); this slow poll only exists as a
 * safety net in case an event is dropped (e.g. while the webview is hidden).
 */
const FALLBACK_POLL_INTERVAL_MS = 3000;

const EMPTY_STATE: ChainState = {
  revision: -1,
  canUndo: false,
  canRedo: false,
  stereoEnabled: false,
  activeSide: 'left',
  stereoInput: false,
  standalone: false,
  inputMode: 'stereo',
  sampleRate: 48000,
  chain: [],
};

/**
 * Single owner of the plugin chain state on the JS side.
 *
 * Sync model:
 * - Native is the source of truth; we hold a revision-tagged snapshot.
 * - Native pushes a `chainChanged` event on every revision bump; we resync on
 *   it (plus a slow fallback poll, and immediately after our own mutations).
 * - Continuous params (knob drags) go through `setBlockParam` fire-and-forget;
 *   the card keeps its own optimistic knob value, native defers the revision
 *   bump until the gesture settles, and the resulting push converges everyone.
 */
export function useChainState() {
  const backend = useAudioBackend();

  const native = useMemo(
    () => ({
      getChainState: backend.getPluginFunction('getChainState'),
      loadTone: backend.getPluginFunction('loadTone'),
      swapTone: backend.getPluginFunction('swapTone'),
      switchModel: backend.getPluginFunction('switchModel'),
      retryModelLoad: backend.getPluginFunction('retryModelLoad'),
      removeChainBlock: backend.getPluginFunction('removeChainBlock'),
      reorderChainBlocks: backend.getPluginFunction('reorderChainBlocks'),
      moveBlockToChain: backend.getPluginFunction('moveBlockToChain'),
      setBlockParam: backend.getPluginFunction('setBlockParam'),
      setBlockEqBand: backend.getPluginFunction('setBlockEqBand'),
      setBlockEqEnabled: backend.getPluginFunction('setBlockEqEnabled'),
      setBlockEqPre: backend.getPluginFunction('setBlockEqPre'),
      resetBlockEq: backend.getPluginFunction('resetBlockEq'),
      setStereoMode: backend.getPluginFunction('setStereoMode'),
      setInputMode: backend.getPluginFunction('setInputMode'),
      setActiveEditChain: backend.getPluginFunction('setActiveEditChain'),
      swapChains: backend.getPluginFunction('swapChains'),
      undoChain: backend.getPluginFunction('undoChain'),
      redoChain: backend.getPluginFunction('redoChain'),
    }),
    [backend]
  );

  const [state, setState] = useState<ChainState>(EMPTY_STATE);
  const revisionRef = useRef(-1);

  const refresh = useCallback(
    async (force = false) => {
      try {
        const res = (await native.getChainState(
          force ? -1 : revisionRef.current
        )) as ChainStateResponse | null;
        if (!res || typeof res.revision !== 'number') return;
        if (isUnchanged(res)) return;
        revisionRef.current = res.revision;
        setState(res);
      } catch (error) {
        console.error('Error loading chain state:', error);
      }
    },
    [native]
  );

  useEffect(() => {
    refresh(true);
    const unsubscribe = backend.addEventListener('chainChanged', () => refresh());
    const interval = setInterval(() => refresh(), FALLBACK_POLL_INTERVAL_MS);
    return () => {
      unsubscribe();
      clearInterval(interval);
    };
  }, [backend, refresh]);

  /** Run a mutation, then resync from native regardless of outcome. */
  const run = useCallback(
    async <T>(label: string, fn: () => Promise<T>): Promise<T | null> => {
      let result: T | null = null;
      try {
        result = await fn();
      } catch (error) {
        console.error(`Chain mutation failed (${label}):`, error);
      }
      await refresh();
      return result;
    },
    [refresh]
  );

  const actions = useMemo(
    () => ({
      /** Add a tone at an insert slot (the one the user clicked, when given —
          stale/absent ids land at the active lane's first insert). Resolves
          to the new blockId ('' on failure). The user's "Default NAM A2 Size"
          preference rides along to seed the block's lite/full tier. */
      loadTone: (toneJson: string, targetInsertId?: string) =>
        run<string>('loadTone', () =>
          native.loadTone(toneJson, targetInsertId ?? '', getDefaultNamA2SlimmableSize())
        ),
      /** Replace an existing block's tone in place (keeps position + params;
          the incoming tone starts at the preferred A2 tier). */
      swapTone: (blockId: string, toneJson: string) =>
        run<boolean>('swapTone', () =>
          native.swapTone(blockId, toneJson, getDefaultNamA2SlimmableSize())
        ),
      /** `modelJson` is the full model object (id/name/model_url) — native
          only stores the active model and resolves the switch from this. */
      switchModel: (blockId: string, modelId: number, modelJson: string) =>
        run<boolean>('switchModel', () => native.switchModel(blockId, modelId, modelJson)),
      /** Retry a failed model download (block.loadFailed). */
      retryModelLoad: (blockId: string) =>
        run<boolean>('retryModelLoad', () => native.retryModelLoad(blockId)),
      removeBlock: (blockId: string) =>
        run('removeChainBlock', () => native.removeChainBlock(blockId)),
      reorderBlocks: (orderedIds: string[]) =>
        run('reorderChainBlocks', () => native.reorderChainBlocks(orderedIds)),
      /** Move a block into the other lane at the given index (stereo drag). */
      moveBlockToChain: (blockId: string, side: ChainSide, index: number) =>
        run<boolean>('moveBlockToChain', () => native.moveBlockToChain(blockId, side, index)),
      setStereoMode: (enabled: boolean) =>
        run('setStereoMode', () => native.setStereoMode(enabled)),
      /** Which channels of a stereo source feed the plugin (faceplate button). */
      setInputMode: (mode: InputMode) => run('setInputMode', () => native.setInputMode(mode)),
      setActiveSide: (side: ChainSide) =>
        run('setActiveEditChain', () => native.setActiveEditChain(side)),
      /** Swap the Left and Right chains wholesale (stereo only). Undoable. */
      swapChains: () => run<boolean>('swapChains', () => native.swapChains()),
      /**
       * Fire-and-forget param setter (safe at knob-drag rates). Booleans are
       * sent as 0/1; the revision bump on native makes pollers converge.
       */
      setBlockParam: (blockId: string, param: BlockParamName, value: number | boolean) => {
        const numeric = typeof value === 'boolean' ? (value ? 1 : 0) : value;
        Promise.resolve(native.setBlockParam(blockId, param, numeric)).catch((error) =>
          console.error(`setBlockParam(${param}) failed:`, error)
        );
      },
      /**
       * Fire-and-forget whole-band EQ update (safe at dot-drag rates). The
       * band object is the atomic mutation unit — clean for undo/redo later.
       */
      setBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => {
        Promise.resolve(native.setBlockEqBand(blockId, bandIndex, band)).catch((error) =>
          console.error('setBlockEqBand failed:', error)
        );
      },
      /** EQ power/bypass — band settings persist, processing is skipped. */
      setBlockEqEnabled: (blockId: string, enabled: boolean) =>
        run<boolean>('setBlockEqEnabled', () => native.setBlockEqEnabled(blockId, enabled)),
      /** EQ position — pre = before the block's model, off = after the block. */
      setBlockEqPre: (blockId: string, pre: boolean) =>
        run<boolean>('setBlockEqPre', () => native.setBlockEqPre(blockId, pre)),
      /** Back to flat defaults (and native skips EQ processing again). */
      resetBlockEq: (blockId: string) =>
        run<boolean>('resetBlockEq', () => native.resetBlockEq(blockId)),
      /** Step the chain edit history. No-ops (false) at the stack ends. */
      undo: () => run<boolean>('undoChain', () => native.undoChain()),
      redo: () => run<boolean>('redoChain', () => native.redoChain()),
    }),
    [native, run]
  );

  return {
    chain: state.chain,
    chainRight: state.chainRight ?? null,
    canUndo: state.canUndo ?? false,
    canRedo: state.canRedo ?? false,
    activePreset: state.preset ?? null,
    stereoEnabled: state.stereoEnabled,
    stereoInput: state.stereoInput ?? false,
    inputMode: state.inputMode ?? 'stereo',
    standalone: state.standalone ?? false,
    sampleRate: state.sampleRate || 48000,
    refresh,
    actions,
  };
}
