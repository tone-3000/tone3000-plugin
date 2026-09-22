import { useCallback } from 'react';
import type { useChainState } from './useChainState';
import {
  MAX_FOLDER_MODELS,
  MAX_LOCAL_FILE_BYTES,
  extensionOf,
  readDirectoryFiles,
  readFileBase64,
  stripExtension,
  toPayload,
} from './localFiles';
import type { BrowserTab } from '../components/browserTabs';
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
  /** The connection gate's action wrapper (see useConnectionGate). */
  requireConnection: (action: () => void | Promise<void>) => void;
  /** The gate's instant OS connectivity check; offline, the browse entry
      points land on the (local) library tab instead of the catalog. */
  isOnline: () => boolean;
  /** Open or close the in-plugin tone browser, optionally landing on a
      given tab (the library entry points open it straight on Library). */
  setShowToneBrowser: (show: boolean, tab?: BrowserTab) => void;
}

/**
 * The "get a tone into the chain" flow: the add (+) and swap entry points
 * that open the browser, and the landing handler that routes a picked tone
 * into the remembered target slot or block.
 */
export function useToneLoadFlow({
  actions,
  stereoEnabled,
  requireConnection,
  isOnline,
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

  // Open the browser on the library tab, remembering the same pending
  // target the catalog flows use. No connection gate, no account, nothing to
  // download: the library is the path to a tone that always works.
  const handleAddFromLibrary = useCallback(
    async (side: ChainSide, insertBlockId: string) => {
      sessionStorage.removeItem(SWAP_STORAGE_KEY);
      sessionStorage.setItem(INSERT_TARGET_STORAGE_KEY, insertBlockId);
      if (stereoEnabled) await actions.setActiveSide(side);
      setShowToneBrowser(true, 'library');
    },
    [actions, setShowToneBrowser, stereoEnabled]
  );

  // The swap sibling: the picked library tone replaces this block in place.
  const handleSwapFromLibrary = useCallback(
    (blockId: string) => {
      sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
      sessionStorage.setItem(SWAP_STORAGE_KEY, blockId);
      setShowToneBrowser(true, 'library');
    },
    [setShowToneBrowser]
  );

  // A library entry was picked. Consumes the same pending targets as
  // handleToneSelected (a library pick and a catalog pick are the same
  // gesture from the browser's point of view) and hands the path to native.
  // Resolves to a user-facing error message, or null on success.
  const handleLibraryPick = useCallback(
    async (itemPath: string): Promise<string | null> => {
      const swapBlockId = sessionStorage.getItem(SWAP_STORAGE_KEY);
      const insertBlockId = sessionStorage.getItem(INSERT_TARGET_STORAGE_KEY);
      const error = await actions.loadLibraryTone(itemPath, swapBlockId ?? insertBlockId ?? '');
      if (error) return error;

      // Only consumed once it actually landed, so a failed pick leaves the
      // target armed for the next one.
      sessionStorage.removeItem(SWAP_STORAGE_KEY);
      sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
      setShowToneBrowser(false);
      return null;
    },
    [actions, setShowToneBrowser]
  );

  // Add: remember the clicked insert slot, then open the browser. The active
  // side also goes to native state (it has to survive the OAuth redirect) as
  // the fallback for when the slot id goes stale, e.g. undone away mid-flow.
  const handleAddModel = useCallback(
    (side: ChainSide, insertBlockId: string) => {
      // Offline, the catalog is unreachable but the library isn't: land on
      // it instead of gating the + behind a modal. Tones the player filed
      // are exactly what should still work with the internet down.
      if (!isOnline()) {
        void handleAddFromLibrary(side, insertBlockId);
        return;
      }
      requireConnection(async () => {
        sessionStorage.removeItem(SWAP_STORAGE_KEY);
        sessionStorage.setItem(INSERT_TARGET_STORAGE_KEY, insertBlockId);
        if (stereoEnabled) await actions.setActiveSide(side);
        setShowToneBrowser(true);
      });
    },
    [actions, handleAddFromLibrary, isOnline, requireConnection, setShowToneBrowser, stereoEnabled]
  );

  // Swap: remember the target block, then run the same browse flow as add.
  // The pending swap id is consumed when the picked tone lands.
  const handleSwapBlock = useCallback(
    (blockId: string) => {
      if (!isOnline()) {
        handleSwapFromLibrary(blockId);
        return;
      }
      requireConnection(() => {
        sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
        sessionStorage.setItem(SWAP_STORAGE_KEY, blockId);
        setShowToneBrowser(true);
      });
    },
    [handleSwapFromLibrary, isOnline, requireConnection, setShowToneBrowser]
  );

  // Drop a local .nam/.wav (or a folder of them) on a tile: no browser, no
  // auth, the file bytes ride the bridge and native validates/loads them.
  // An insert slot adds a block; an existing tone tile swaps in place.
  // Resolves to a user-facing error message (the tile toasts it), or null
  // on success.
  const handleDropFile = useCallback(
    async (targetBlockId: string, item: DataTransferItem): Promise<string | null> => {
      // Synchronous reads: the DataTransferItem goes inert once the drop
      // handler yields (the entry/file objects stay usable).
      const entry = item.webkitGetAsEntry();
      const singleFile = entry?.isDirectory ? null : item.getAsFile();

      try {
        // A folder loads as one multi-model tone: title from the folder,
        // one model per file of its majority extension (.nam vs .wav, which
        // also decides NAM vs IR), everything else ignored.
        if (entry?.isDirectory) {
          const all = await readDirectoryFiles(entry as FileSystemDirectoryEntry);
          const nams = all.filter((f) => extensionOf(f.name) === 'nam');
          const wavs = all.filter((f) => extensionOf(f.name) === 'wav');
          const files = nams.length >= wavs.length ? nams : wavs;
          if (files.length === 0) return 'No .nam or .wav files in the folder';
          if (files.length > MAX_FOLDER_MODELS)
            return `Folder has too many files (max ${MAX_FOLDER_MODELS})`;
          if (files.some((f) => f.size > MAX_LOCAL_FILE_BYTES)) return 'A file is too large';

          // Directory read order is unspecified; toPayload puts the models
          // in natural name order ("amp 2" before "amp 10").
          return await actions.loadLocalTone(entry.name, await toPayload(files), targetBlockId);
        }

        if (!singleFile) return "Couldn't read the dropped file";
        const extension = extensionOf(singleFile.name);
        if (extension !== 'nam' && extension !== 'wav')
          return 'Only .nam and .wav files are supported';
        if (singleFile.size > MAX_LOCAL_FILE_BYTES) return 'File is too large';
        return await actions.loadLocalTone(
          stripExtension(singleFile.name),
          [{ name: singleFile.name, data: await readFileBase64(singleFile) }],
          targetBlockId
        );
      } catch (error) {
        console.error('Local file drop failed:', error);
        return "Couldn't read the dropped file";
      }
    },
    [actions]
  );

  // Abandon any pending targets (browser closed without picking, logout).
  const clearPendingTargets = useCallback(() => {
    sessionStorage.removeItem(SWAP_STORAGE_KEY);
    sessionStorage.removeItem(INSERT_TARGET_STORAGE_KEY);
  }, []);

  return {
    handleToneSelected,
    handleAddModel,
    handleSwapBlock,
    handleDropFile,
    handleAddFromLibrary,
    handleSwapFromLibrary,
    handleLibraryPick,
    clearPendingTargets,
  };
}
