import { createContext, useContext } from 'react';
import type { BlockParamName, ChainSide, EqBand, ToneBlock } from '../types/chain';
import type { Model, Tone } from '../types/tone';

/**
 * Everything a chain block (gallery tile or detail card) can do, bundled
 * into one context so the tree doesn't thread a dozen callback props from
 * `Plugin` down through `ChainView`, and so leaf components can be
 * `React.memo`d without every parent re-render defeating it via fresh
 * lambdas.
 *
 * The provider value lives in `Plugin` and is memoized there; everything in
 * it is either a `useChainState` action (stable) or a stable callback.
 */
export interface ChainActions {
  /** Launch the Select flow, adding into the clicked insert slot. */
  addModel: (side: ChainSide, insertBlockId: string) => void;
  /** Load a drop on a tile: a .nam / .wav file (NAM must be A2), or a folder
      of them (one block, one model per file). An insert slot adds; an
      existing tone tile swaps in place. Resolves to a user-facing error
      message, or null on success. */
  loadLocalFile: (targetBlockId: string, item: DataTransferItem) => Promise<string | null>;
  /** Menu-driven sibling of loadLocalFile: native opens its OS file picker
      and loads the pick (a .nam/.wav file, or a folder of them) from its
      path. Same targeting rules; the reliable route on Linux, where OS file
      drags never reach the embedded webview. Resolves to a user-facing
      error message, or null on success or when the dialog is cancelled. */
  pickLocalFile: (targetBlockId: string, kind: 'file' | 'folder') => Promise<string | null>;
  removeBlock: (blockId: string) => void;
  /** Launch the Select flow to replace this block's tone in place. */
  swapBlock: (blockId: string) => void;
  /** Copy the tone's TONE3000 URL; resolves true when it hit the clipboard. */
  shareBlock: (block: ToneBlock) => Promise<boolean>;
  /** Reorder one lane (full order including its insert slot). */
  reorderBlocks: (orderedIds: string[]) => void;
  /** Move a block into the other lane at the given index (stereo drag). */
  moveBlock: (blockId: string, side: ChainSide, index: number) => void;
  /** Clone a live tone block (all settings + model) into `side` at `index`
      (alt-drag duplicate). An insert slot there is filled, otherwise the
      clone splices in. */
  duplicateBlock: (sourceBlockId: string, side: ChainSide, index: number) => void;
  /** Copy a block into the native block clipboard (tone + settings + model
      bytes). The snapshot is self-contained, so pasting keeps working after
      preset switches or deleting the source block. */
  copyBlock: (blockId: string) => void;
  /** Paste the copied block into `side` at `index` (the insert slot there
      is filled). Gate on `canPaste` from useChainState. */
  pasteBlock: (side: ChainSide, index: number) => void;
  /** Swap the Left and Right chains wholesale (stereo only). */
  swapChains: () => void;
  /** Branch the other lane off `side` after one of its tone blocks (stereo
      only); the other lane's input becomes the tapped signal. */
  setBranch: (side: ChainSide, afterBlockId: string) => void;
  /** Revert to two fully independent chains. */
  clearBranch: () => void;
  /** Native only stores the active model, so the switch always carries the
      model object (paged in from the API by the picker, or a local tone's
      own model list); id/name/model_url is all native needs. */
  switchModel: (
    blockId: string,
    modelId: number,
    model: Pick<Model, 'id' | 'name' | 'model_url'>
  ) => Promise<void>;
  /** Retry a failed model download (`block.loadFailed`); re-queues the
      block's active model through the native background loader. */
  retryLoad: (blockId: string) => void;
  /**
   * Fetch a tone's full model catalog (tones max out at 300 models; NAM is
   * architecture-filtered). Backs the detail card's model picker, as the
   * persisted block only carries the active model.
   */
  listToneModels: (toneId: number, format: string | undefined) => Promise<Model[]>;
  /**
   * Fetch a tone's full catalog metadata (description, makes, tags, url).
   * Backs the detail card's info panel; not written into saved state.
   */
  getTone: (toneId: number) => Promise<Tone>;
  /** Favorite / unfavorite a tone for the signed-in user (idempotent). */
  setToneFavorite: (toneId: number, favorite: boolean) => Promise<void>;
  /** Push a fresh /tones/{id} payload into native, which merges it into
      every block holding that tone (stored models preserved). Best-effort
      background sync: metadata only, not undoable, no-op when unchanged. */
  refreshToneMetadata: (toneJson: string) => void;
  /** Fire-and-forget per-block param setter (see useChainState). */
  setBlockParam: (blockId: string, param: BlockParamName, value: number | boolean) => void;
  /** The block's NAM A2 size (0 = lite, 1 = full); retiers the loaded
      engine natively. Backs the header LITE/FULL toggle. */
  setBlockSlimSize: (blockId: string, slimSize: number) => void;
  /** IR envelope: a 2-segment Attack/Decay shape (see BlockParams.initLevel/
      attackLength/attackCurve/decayLength/decayLevel/decayCurve). Not
      fire-and-forget-safe at knob-drag rates - rebuilds the convolver
      off-thread, so callers should debounce (see ChainBlock.tsx's shaping
      row). All six values arrive together (like setBlockEqBand's
      whole-band updates) so a drag on one can't clobber another's in-flight
      value. */
  setBlockIrDecay: (
    blockId: string,
    initLevelNormalized: number,
    attackLengthNormalized: number,
    attackCurveNormalized: number,
    decayLengthNormalized: number,
    decayLevelNormalized: number,
    decayCurveNormalized: number
  ) => void;
  /** Fire-and-forget whole-band EQ setter (see useChainState). */
  setBlockEqBand: (blockId: string, bandIndex: number, band: EqBand) => void;
  /** EQ power/bypass: band settings persist, processing is skipped. */
  setBlockEqEnabled: (blockId: string, enabled: boolean) => void;
  /** EQ position: pre = before the block's model, off = after the model (wet only). */
  setBlockEqPre: (blockId: string, pre: boolean) => void;
  resetBlockEq: (blockId: string) => void;
  /**
   * Whether a TONE3000 session is present. Auth-dependent block actions
   * (model switching, where native re-downloads the model with a Bearer token)
   * disable themselves when signed out.
   */
  authenticated: boolean;
  /** Kick off TONE3000 login (connection-gated); used by the info panel CTA. */
  login: () => void;
}

const ChainActionsContext = createContext<ChainActions | null>(null);

export const ChainActionsProvider = ChainActionsContext.Provider;

export function useChainActions(): ChainActions {
  const actions = useContext(ChainActionsContext);
  if (!actions) throw new Error('useChainActions must be used inside a ChainActionsProvider');
  return actions;
}
