import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';
import type {
  BlockParamName,
  ChainSide,
  ChainState,
  ChainStateResponse,
  EqBand,
} from '../types/chain';
import { isUnchanged } from '../types/chain';

/**
 * Poll cadence for chain state. Cheap because native short-circuits with a
 * tiny { revision, unchanged } reply whenever nothing changed since our last
 * sync — the full state only crosses the bridge after an actual mutation
 * (including background model loads finishing, which bump the revision).
 */
const POLL_INTERVAL_MS = 500;

const EMPTY_STATE: ChainState = {
  revision: -1,
  stereoEnabled: false,
  activeSide: 'left',
  sampleRate: 48000,
  chain: [],
};

/**
 * Single owner of the plugin chain state on the JS side.
 *
 * Sync model:
 * - Native is the source of truth; we hold a revision-tagged snapshot.
 * - Mutations call native, then resync immediately (no optimistic chain
 *   surgery — the roundtrip is one bridge call).
 * - Continuous params (knob drags) go through `setBlockParam` fire-and-forget;
 *   the card keeps its own optimistic knob value and the revision bump makes
 *   the next poll converge everyone else.
 */
export function useChainState() {
  const backend = useAudioBackend();

  const native = useMemo(
    () => ({
      getChainState: backend.getPluginFunction('getChainState'),
      loadTone: backend.getPluginFunction('loadTone'),
      swapTone: backend.getPluginFunction('swapTone'),
      switchModel: backend.getPluginFunction('switchModel'),
      removeChainBlock: backend.getPluginFunction('removeChainBlock'),
      reorderChainBlocks: backend.getPluginFunction('reorderChainBlocks'),
      setBlockParam: backend.getPluginFunction('setBlockParam'),
      setBlockEqBand: backend.getPluginFunction('setBlockEqBand'),
      setBlockEqEnabled: backend.getPluginFunction('setBlockEqEnabled'),
      resetBlockEq: backend.getPluginFunction('resetBlockEq'),
      setStereoMode: backend.getPluginFunction('setStereoMode'),
      setActiveEditChain: backend.getPluginFunction('setActiveEditChain'),
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
    const interval = setInterval(() => refresh(), POLL_INTERVAL_MS);
    return () => clearInterval(interval);
  }, [refresh]);

  /** Run a mutation, then resync from native regardless of outcome. */
  const run = useCallback(
    async <T,>(label: string, fn: () => Promise<T>): Promise<T | null> => {
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
      /** Add a tone at the insert slot. Resolves to the new blockId ('' on failure). */
      loadTone: (toneJson: string) =>
        run<string>('loadTone', () => native.loadTone(toneJson)),
      /** Replace an existing block's tone in place (keeps position + params). */
      swapTone: (blockId: string, toneJson: string) =>
        run<boolean>('swapTone', () => native.swapTone(blockId, toneJson)),
      switchModel: (blockId: string, modelId: number) =>
        run<boolean>('switchModel', () => native.switchModel(blockId, modelId)),
      removeBlock: (blockId: string) =>
        run('removeChainBlock', () => native.removeChainBlock(blockId)),
      reorderBlocks: (orderedIds: string[]) =>
        run('reorderChainBlocks', () => native.reorderChainBlocks(orderedIds)),
      setStereoMode: (enabled: boolean) =>
        run('setStereoMode', () => native.setStereoMode(enabled)),
      setActiveSide: (side: ChainSide) =>
        run('setActiveEditChain', () => native.setActiveEditChain(side)),
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
      /** Back to flat defaults (and native skips EQ processing again). */
      resetBlockEq: (blockId: string) =>
        run<boolean>('resetBlockEq', () => native.resetBlockEq(blockId)),
    }),
    [native, run]
  );

  return {
    chain: state.chain,
    stereoEnabled: state.stereoEnabled,
    activeSide: state.activeSide,
    sampleRate: state.sampleRate || 48000,
    refresh,
    actions,
  };
}
