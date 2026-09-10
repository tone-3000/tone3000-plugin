import { useEffect, useState } from 'react';
import { useNativeFunction } from './useFunction';

export interface IrWaveform {
  mins: number[];
  maxs: number[];
}

/**
 * Static per-column min/max peaks for an IR block's waveform display.
 * Fetched once per loaded model (not a poll like useBlockSpectrum) - the
 * data is static per load. Refetches whenever `activeModelId` changes, not
 * just on mount/blockId change: a model switch via the dropdown or the
 * ModelSelect prev/next chevrons (ChainBlock.tsx's handleModelSelect ->
 * switchModel) keeps the same blockId and, per ToneBlock.loaded's own
 * contract, `loaded` stays true the whole time (the previous model keeps
 * playing until the new one is spliced in) - so without activeModelId in
 * the dependency list this would only ever refresh on remount. Clears
 * while `loaded` is false (mid-swap/reload) so a stale waveform never
 * lingers under a different tone.
 *
 * Also refetches on `modelLoading` transitions, and this part is load-
 * bearing, not belt-and-suspenders: `switchModel` (ProcessorChain.cpp) sets
 * `activeModelId` and `modelLoading=true` *synchronously*, before the new
 * model's file has even been read - `irWaveformPeaks` isn't replaced with
 * the new model's data until the background job finishes and reaches
 * `applyPreparedModelToChainBlock`, which is also where `modelLoading`
 * flips back to `false` (same atomic step, same lock). So a fetch triggered
 * by `activeModelId` alone races the actual prepare work: if it resolves
 * before that background job finishes (routine for anything beyond a
 * trivial file), `getIrWaveform` still returns the *previous* model's
 * peaks, mislabeled as the new one - and since neither `activeModelId` nor
 * `loaded` change again afterward, nothing was left to trigger a
 * correcting refetch once the real data actually landed. Depending on
 * `modelLoading`'s true->false edge closes that gap: by the time it flips,
 * `irWaveformPeaks` is guaranteed current. The existing `alive` guard below
 * already makes any number of overlapping fetches resolve safely regardless
 * of order, so this can't reintroduce a race of its own.
 */
export function useIrWaveform(
  blockId: string,
  loaded: boolean,
  activeModelId: number,
  modelLoading: boolean
): IrWaveform | null {
  const getIrWaveform = useNativeFunction<IrWaveform>('getIrWaveform');
  const [waveform, setWaveform] = useState<IrWaveform | null>(null);

  useEffect(() => {
    if (!loaded) {
      setWaveform(null);
      return;
    }
    let alive = true;
    getIrWaveform(blockId).then((res) => {
      if (alive && res && Array.isArray(res.mins) && Array.isArray(res.maxs)) {
        setWaveform(res);
      }
    });
    return () => {
      alive = false;
    };
  }, [blockId, loaded, activeModelId, modelLoading, getIrWaveform]);

  return waveform;
}
