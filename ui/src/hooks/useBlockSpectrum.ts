import { useEffect, useMemo, useState } from 'react';
import { useAudioBackend } from './useAudioBackend';

/**
 * Constants mirrored from plugin/include/BlockSpectrum.h: the native analyzer
 * returns SPECTRUM_NUM_BINS dB values (SPECTRUM_MIN_DB..0) log-spaced from
 * 20 Hz to 20 kHz — the same log axis the EQ graph uses, so bin index maps
 * linearly to x position.
 */
export const SPECTRUM_NUM_BINS = 64;
export const SPECTRUM_MIN_DB = -100;

const POLL_INTERVAL_MS = 33;

/**
 * Live spectrum of the audio leaving `blockId`. Subscribing enables the
 * native analyzer for that block (the audio thread does zero analyzer work
 * otherwise) and disables it again on unmount. Mount this hook in a leaf
 * component: it re-renders its consumer at ~30 fps while active.
 */
export function useBlockSpectrum(blockId: string): number[] {
  const backend = useAudioBackend();
  const native = useMemo(
    () => ({
      setEnabled: backend.getPluginFunction('setBlockSpectrumEnabled'),
      getSpectrum: backend.getPluginFunction('getBlockSpectrum'),
    }),
    [backend]
  );

  const [bins, setBins] = useState<number[]>([]);

  useEffect(() => {
    let alive = true;
    let polling = false;

    Promise.resolve(native.setEnabled(blockId, true)).catch((error) =>
      console.error('setBlockSpectrumEnabled failed:', error)
    );

    const interval = window.setInterval(async () => {
      if (polling) return; // never stack bridge calls if one runs long
      polling = true;
      try {
        const res = await native.getSpectrum(blockId);
        if (alive && Array.isArray(res)) {
          const next = res as number[];
          // Skip the ~30 fps re-render when the audio is idle and the bins
          // haven't moved since the last poll.
          setBins((prev) =>
            prev.length === next.length && prev.every((v, i) => v === next[i]) ? prev : next
          );
        }
      } catch (error) {
        console.error('getBlockSpectrum failed:', error);
      } finally {
        polling = false;
      }
    }, POLL_INTERVAL_MS);

    return () => {
      alive = false;
      window.clearInterval(interval);
      Promise.resolve(native.setEnabled(blockId, false)).catch(() => undefined);
    };
  }, [native, blockId]);

  return bins;
}
