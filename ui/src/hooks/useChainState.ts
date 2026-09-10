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
import { isUnchanged, SLIM_SIZE_LITE } from '../types/chain';

/**
 * Fallback poll cadence for chain state. The primary sync channel is the
 * native `chainChanged` push event (the editor watches the revision counter
 * and emits within ~50 ms of any mutation); I keep this slow poll purely as
 * a safety net in case an event is dropped (e.g. while the webview is
 * hidden). Unchanged revisions short-circuit natively, so it's near free.
 */
const FALLBACK_POLL_INTERVAL_MS = 3000;

const EMPTY_STATE: ChainState = {
  revision: -1,
  canUndo: false,
  canRedo: false,
  atDefault: true,
  stereoEnabled: false,
  activeSide: 'left',
  stereoInput: false,
  stereoOutput: true,
  standalone: false,
  inputMode: 'stereo',
  namSlimSizeDefault: SLIM_SIZE_LITE,
  multiCore: true,
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
      loadLocalTone: backend.getPluginFunction('loadLocalTone'),
      swapTone: backend.getPluginFunction('swapTone'),
      refreshToneMetadata: backend.getPluginFunction('refreshToneMetadata'),
      switchModel: backend.getPluginFunction('switchModel'),
      retryModelLoad: backend.getPluginFunction('retryModelLoad'),
      removeChainBlock: backend.getPluginFunction('removeChainBlock'),
      reorderChainBlocks: backend.getPluginFunction('reorderChainBlocks'),
      moveBlockToChain: backend.getPluginFunction('moveBlockToChain'),
      duplicateChainBlock: backend.getPluginFunction('duplicateChainBlock'),
      copyChainBlock: backend.getPluginFunction('copyChainBlock'),
      pasteChainBlock: backend.getPluginFunction('pasteChainBlock'),
      setBlockParam: backend.getPluginFunction('setBlockParam'),
      setBlockEqBand: backend.getPluginFunction('setBlockEqBand'),
      setBlockEqEnabled: backend.getPluginFunction('setBlockEqEnabled'),
      setBlockEqPre: backend.getPluginFunction('setBlockEqPre'),
      resetBlockEq: backend.getPluginFunction('resetBlockEq'),
      setStereoMode: backend.getPluginFunction('setStereoMode'),
      setInputMode: backend.getPluginFunction('setInputMode'),
      setBlockSlimSize: backend.getPluginFunction('setBlockSlimSize'),
      setBlockIrCategory: backend.getPluginFunction('setBlockIrCategory'),
      setNamSlimSizeDefault: backend.getPluginFunction('setNamSlimSizeDefault'),
      setMultiCore: backend.getPluginFunction('setMultiCore'),
      setActiveEditChain: backend.getPluginFunction('setActiveEditChain'),
      swapChains: backend.getPluginFunction('swapChains'),
      setChainBranch: backend.getPluginFunction('setChainBranch'),
      clearChainBranch: backend.getPluginFunction('clearChainBranch'),
      undoChain: backend.getPluginFunction('undoChain'),
      redoChain: backend.getPluginFunction('redoChain'),
      resetToDefault: backend.getPluginFunction('resetToDefault'),
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

  /** Run a mutation, then resync from native regardless of outcome. The
      native bridge is untyped, so T asserts each call's known return shape. */
  const run = useCallback(
    async <T>(label: string, fn: () => Promise<unknown>): Promise<T | null> => {
      let result: T | null = null;
      try {
        result = (await fn()) as T;
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
      /** Add a tone at an insert slot (the one the user clicked, when given;
          stale/absent ids land at the active lane's first insert). Resolves
          to the new blockId ('' on failure). */
      loadTone: (toneJson: string, targetInsertId?: string) =>
        run<string>('loadTone', () => native.loadTone(toneJson, targetInsertId ?? '')),
      /** Load dropped local file(s) as one block (a single .nam/.wav, or a
          folder's files; bytes as base64). `targetInsertId` is an insert
          slot (adds) or an existing tone block (swaps in place). Native
          validates each file (NAM must be A2). Resolves to a user-facing
          error message, or null on success. */
      loadLocalTone: async (
        title: string,
        files: { name: string; data: string }[],
        targetInsertId: string
      ) => {
        const res = await run<{ blockId?: string; error?: string } | null>('loadLocalTone', () =>
          native.loadLocalTone(title, files, targetInsertId)
        );
        if (res?.blockId) return null;
        return res?.error ?? "Couldn't load the file";
      },
      /** Replace an existing block's tone in place (keeps position + params). */
      swapTone: (blockId: string, toneJson: string) =>
        run<boolean>('swapTone', () => native.swapTone(blockId, toneJson)),
      /** Best-effort metadata re-sync from a fresh /tones/{id} payload:
          native merges it into every block holding that tone (stored models
          preserved). Metadata only, not undoable; no-op when unchanged. */
      refreshToneMetadata: (toneJson: string) =>
        run<boolean>('refreshToneMetadata', () => native.refreshToneMetadata(toneJson)),
      /** `modelJson` is the full model object (id/name/model_url); native
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
      /** Clone a live tone block (all settings + model) into `side` at
          `index` (alt-drag duplicate). Landing on an insert slot fills it;
          anywhere else splices in. Resolves to the new blockId ('' on
          failure). */
      duplicateBlock: (sourceBlockId: string, side: ChainSide, index: number) =>
        run<string>('duplicateChainBlock', () =>
          native.duplicateChainBlock(sourceBlockId, side, index)
        ),
      /** Snapshot a block (tone + settings + model bytes) into the native
          block clipboard. Self-contained: paste keeps working after preset
          switches or deleting the source. `canPaste` flips via the resync. */
      copyBlock: (blockId: string) =>
        run<boolean>('copyChainBlock', () => native.copyChainBlock(blockId)),
      /** Rebuild the copied block into `side` at `index` (an insert slot
          there is filled). Resolves to the new blockId ('' on failure). */
      pasteBlock: (side: ChainSide, index: number) =>
        run<string>('pasteChainBlock', () => native.pasteChainBlock(side, index)),
      setStereoMode: (enabled: boolean) =>
        run('setStereoMode', () => native.setStereoMode(enabled)),
      /** Which channels of a stereo source feed the plugin (faceplate button). */
      setInputMode: (mode: InputMode) => run('setInputMode', () => native.setInputMode(mode)),
      /** The block's NAM A2 size (0 = lite, 1 = full; see BlockParams.
          slimSize). Retiers the loaded engine natively under a short fade;
          part of the chain state, so it lands in presets and undo. */
      setBlockSlimSize: (blockId: string, slimSize: number) =>
        run<boolean>('setBlockSlimSize', () => native.setBlockSlimSize(blockId, slimSize)),
      /** Explicit IR content category (see ToneBlock.irCategory); resets Mix
          (and the -18 dB cab pad) to the new category's fixed default. */
      setBlockIrCategory: (blockId: string, category: 'cab' | 'irPlayer') =>
        run<boolean>('setBlockIrCategory', () => native.setBlockIrCategory(blockId, category)),
      /** Default NAM A2 size for newly added blocks (machine-wide; existing
          blocks keep their own size). Persists on disk. */
      setNamSlimSizeDefault: (slimSize: number) =>
        run('setNamSlimSizeDefault', () => native.setNamSlimSizeDefault(slimSize)),
      /** Multi-core processing (machine-wide). Pure scheduling: applies
          instantly and persists on disk. */
      setMultiCore: (enabled: boolean) => run('setMultiCore', () => native.setMultiCore(enabled)),
      setActiveSide: (side: ChainSide) =>
        run('setActiveEditChain', () => native.setActiveEditChain(side)),
      /** Swap the Left and Right chains wholesale (stereo only). Undoable. */
      swapChains: () => run<boolean>('swapChains', () => native.swapChains()),
      /** Branch the other lane off `side` after one of its tone blocks
          (stereo only). The other lane's input becomes the tapped signal.
          Undoable; native forces the input mode off "stereo". */
      setBranch: (side: ChainSide, afterBlockId: string) =>
        run<boolean>('setChainBranch', () => native.setChainBranch(side, afterBlockId)),
      /** Revert to two fully independent chains. Undoable. */
      clearBranch: () => run<boolean>('clearChainBranch', () => native.clearChainBranch()),
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
       * band object is the atomic mutation unit, clean for undo/redo later.
       */
      setBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => {
        Promise.resolve(native.setBlockEqBand(blockId, bandIndex, band)).catch((error) =>
          console.error('setBlockEqBand failed:', error)
        );
      },
      /** EQ power/bypass: band settings persist, processing is skipped. */
      setBlockEqEnabled: (blockId: string, enabled: boolean) =>
        run<boolean>('setBlockEqEnabled', () => native.setBlockEqEnabled(blockId, enabled)),
      /** EQ position: pre = before the block's model, off = after the model (wet only). */
      setBlockEqPre: (blockId: string, pre: boolean) =>
        run<boolean>('setBlockEqPre', () => native.setBlockEqPre(blockId, pre)),
      /** Back to flat defaults (and native skips EQ processing again). */
      resetBlockEq: (blockId: string) =>
        run<boolean>('resetBlockEq', () => native.resetBlockEq(blockId)),
      /** Step the chain edit history. No-ops (false) at the stack ends. */
      undo: () => run<boolean>('undoChain', () => native.undoChain()),
      redo: () => run<boolean>('redoChain', () => native.redoChain()),
      /** Back to the factory-default state: empty mono chain, faceplate
          params at defaults, no active preset. Undoable (chain part). */
      resetToDefault: () => run<boolean>('resetToDefault', () => native.resetToDefault()),
    }),
    [native, run]
  );

  return {
    chain: state.chain,
    chainRight: state.chainRight ?? null,
    branch: state.branch ?? null,
    canUndo: state.canUndo ?? false,
    canRedo: state.canRedo ?? false,
    canPaste: state.canPasteBlock ?? false,
    atDefault: state.atDefault,
    activePreset: state.preset ?? null,
    stereoEnabled: state.stereoEnabled,
    stereoInput: state.stereoInput ?? false,
    stereoOutput: state.stereoOutput ?? true,
    inputMode: state.inputMode ?? 'stereo',
    namSlimSizeDefault: state.namSlimSizeDefault ?? SLIM_SIZE_LITE,
    multiCore: state.multiCore ?? true,
    standalone: state.standalone ?? false,
    sampleRate: state.sampleRate || 48000,
    refresh,
    actions,
  };
}
