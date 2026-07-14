import { createContext, useContext } from 'react';
import type { BlockParamName, ChainSide, EqBand, ToneBlock } from '../types/chain';

/**
 * Everything a chain block (gallery tile or detail card) can do, bundled
 * into one context so the tree doesn't thread a dozen callback props from
 * `Plugin` down through `ChainView` — and so leaf components can be
 * `React.memo`d without every parent re-render defeating it via fresh
 * lambdas.
 *
 * The provider value lives in `Plugin` and is memoized there; everything in
 * it is either a `useChainState` action (stable) or a stable callback.
 */
export interface ChainActions {
  /** Launch the Select flow, adding to the given lane's insert slot. */
  addModel: (side: ChainSide) => void;
  removeBlock: (blockId: string) => void;
  /** Launch the Select flow to replace this block's tone in place. */
  swapBlock: (blockId: string) => void;
  /** Copy the tone's TONE3000 URL; resolves true when it hit the clipboard. */
  shareBlock: (block: ToneBlock) => Promise<boolean>;
  /** Reorder one lane (full order including its insert slot). */
  reorderBlocks: (orderedIds: string[]) => void;
  /** Move a block into the other lane at the given index (stereo drag). */
  moveBlock: (blockId: string, side: ChainSide, index: number) => void;
  /** Swap the Left and Right chains wholesale (stereo only). */
  swapChains: () => void;
  switchModel: (blockId: string, modelId: number) => Promise<void>;
  /** Fire-and-forget per-block param setter (see useChainState). */
  setBlockParam: (blockId: string, param: BlockParamName, value: number | boolean) => void;
  /** Fire-and-forget whole-band EQ setter (see useChainState). */
  setBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => void;
  /** EQ power/bypass — band settings persist, processing is skipped. */
  setBlockEqEnabled: (blockId: string, enabled: boolean) => void;
  resetBlockEq: (blockId: string) => void;
}

const ChainActionsContext = createContext<ChainActions | null>(null);

export const ChainActionsProvider = ChainActionsContext.Provider;

export function useChainActions(): ChainActions {
  const actions = useContext(ChainActionsContext);
  if (!actions) throw new Error('useChainActions must be used inside a ChainActionsProvider');
  return actions;
}
