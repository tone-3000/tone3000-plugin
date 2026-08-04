import { useCallback } from 'react';
import type { useChainState } from './useChainState';
import type { ChainSide } from '../types/chain';
import type { Model, Tone } from '../types/tone';

type ChainStateActions = ReturnType<typeof useChainState>['actions'];

// Swap/insert targets must survive the Select flow's full-page OAuth
// redirect (the webview navigates to tone3000.com and back, remounting
// React), so they live in sessionStorage rather than component state.
// Native falls back gracefully when an id went stale.
const SWAP_STORAGE_KEY = 't3k.pendingSwapBlockId';
const INSERT_TARGET_STORAGE_KEY = 't3k.pendingInsertBlockId';

interface UseToneLoadFlowOptions {
  actions: ChainStateActions;
  stereoEnabled: boolean;
  /** The internet gate's action wrapper (see useInternetGate). */
  requireInternet: (action: () => void | Promise<void>) => void;
  /** Open or close the in-plugin tone browser. */
  setShowToneBrowser: (show: boolean) => void;
}

/**
 * The "get a tone into the chain" flow: the add (+) and swap entry points
 * that open the browser, and the landing handler that routes a picked tone
 * into the remembered target slot or block.
 */
export function useToneLoadFlow({
  actions,
  stereoEnabled,
  requireInternet,
  setShowToneBrowser,
}: UseToneLoadFlowOptions) {
  // A fully-resolved tone landed (Select callback or a browser card pick).
  // If a swap was pending, replace that block in place; otherwise add the
  // tone at the remembered insert slot.
  const handleToneSelected = useCallback(
    async (tone: Tone & { models: Model[] }) => {
      if (!tone.models || tone.models.length === 0) {
        console.error('Tone has no models');
        return;
      }

      // Consume the pending targets up front so they can never leak into a
      // later selection. (Each flow clears the other's key before starting.)
      const swapBlockId = sessionStorage.getItem(SWAP_STORAGE_KEY);
      sessionStorage.removeItem(SWAP_STORAGE_KEY);
      const insertBlockId = sessionStorage.getItem(INSERT_TARGET_STORAGE_KEY);
      sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);

      const toneJson = JSON.stringify(tone);
      setShowToneBrowser(false);

      if (swapBlockId) {
        const swapped = await actions.swapTone(swapBlockId, toneJson);
        if (swapped) return;
        console.warn('Swap target no longer exists; adding tone as a new block');
      }

      const blockId = await actions.loadTone(toneJson, insertBlockId ?? undefined);
      if (!blockId) console.error('Failed to load tone');
    },
    [actions, setShowToneBrowser]
  );

  // Add: remember the clicked insert slot, then open the browser. The active
  // side also goes to native state (it has to survive the OAuth redirect) as
  // the fallback for when the slot id goes stale, e.g. undone away mid-flow.
  const handleAddModel = useCallback(
    (side: ChainSide, insertBlockId: string) => {
      requireInternet(async () => {
        sessionStorage.removeItem(SWAP_STORAGE_KEY);
        sessionStorage.setItem(INSERT_TARGET_STORAGE_KEY, insertBlockId);
        if (stereoEnabled) await actions.setActiveSide(side);
        setShowToneBrowser(true);
      });
    },
    [actions, requireInternet, setShowToneBrowser, stereoEnabled]
  );

  // Swap: remember the target block, then run the same browse flow as add.
  // The pending swap id is consumed when the picked tone lands.
  const handleSwapBlock = useCallback(
    (blockId: string) => {
      requireInternet(() => {
        sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
        sessionStorage.setItem(SWAP_STORAGE_KEY, blockId);
        setShowToneBrowser(true);
      });
    },
    [requireInternet, setShowToneBrowser]
  );

  // Abandon any pending targets (browser closed without picking, logout).
  const clearPendingTargets = useCallback(() => {
    sessionStorage.removeItem(SWAP_STORAGE_KEY);
    sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
  }, []);

  return { handleToneSelected, handleAddModel, handleSwapBlock, clearPendingTargets };
}
