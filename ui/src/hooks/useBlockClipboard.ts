import { useSyncExternalStore } from 'react';

/**
 * In-app clipboard for chain blocks (Copy on a tone tile, Paste on an insert
 * slot). Only the block *id* is held. Native resolves the actual data when
 * the paste lands, so a stale copy (block deleted since) simply disables the
 * paste instead of cloning a ghost.
 *
 * A module store rather than context/props: the copy gesture lives deep
 * inside memoized tiles, and threading setters through the lane would defeat
 * React.memo on every tile for a value almost none of them read.
 */

let copiedBlockId: string | null = null;

const listeners = new Set<() => void>();
const emit = () => listeners.forEach((listener) => listener());
const subscribe = (listener: () => void) => {
  listeners.add(listener);
  return () => listeners.delete(listener);
};

export const copyBlock = (blockId: string) => {
  if (copiedBlockId === blockId) return;
  copiedBlockId = blockId;
  emit();
};

/** The copied block id (may be stale; validate against the live chain). */
export const useCopiedBlockId = () =>
  useSyncExternalStore(subscribe, () => copiedBlockId);
