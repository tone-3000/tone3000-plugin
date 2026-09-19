import { useCallback, useEffect, useRef, useState } from 'react';
import { useChainActions } from './useChainActions';
import type { ToneBlock } from '../types/chain';
import type { Model } from '../types/tone';

/**
 * A tone block's models and the switch between them, shared by the expanded
 * card's picker and the gallery tile's arrows.
 *
 * Native persists only the block's *active* model; the full catalog (tones
 * max out at 300 models) is fetched client-side in one call per tone. A local
 * tone owns its model list and needs no fetch, and signed out the catalog
 * can't be fetched at all (the API needs the token).
 *
 * `eager` fetches on mount, tone change and auth arrival (the card, which is
 * only mounted when someone is looking at it). Without it nothing is fetched
 * until `load` is called, so a lane of tiles costs no requests until one is
 * hovered.
 */
export function useToneModels(block: ToneBlock, { eager }: { eager: boolean }) {
  const actions = useChainActions();
  const { blockId, tone } = block;
  const isLocal = tone.local === true;

  const [models, setModels] = useState<Model[]>([]);
  const [loading, setLoading] = useState(false);
  const [switching, setSwitching] = useState(false);

  // Only the newest request may touch state (retries reuse this fetch, so a
  // flag can't cover it).
  const fetchSeq = useRef(0);
  const fetchModels = useCallback(async () => {
    if (isLocal || !actions.authenticated) return;
    const seq = ++fetchSeq.current;
    setLoading(true);
    try {
      const list = await actions.listToneModels(tone.id, tone.format);
      if (seq === fetchSeq.current) setModels(list);
    } catch (err) {
      // No error UI: the picker keeps the stored model, and `load` retries,
      // so a transient failure never sticks.
      console.error('Failed to load models', err);
    } finally {
      if (seq === fetchSeq.current) setLoading(false);
    }
  }, [actions, isLocal, tone.format, tone.id]);

  // A new tone or an auth change drops the old list; `eager` refills it.
  useEffect(() => {
    setModels([]);
    if (eager) void fetchModels();
    return () => {
      // Bumping the counter orphans whatever fetch is in flight — reading its
      // latest value here is the point, not a stale-closure bug.
      // eslint-disable-next-line react-hooks/exhaustive-deps
      fetchSeq.current++;
    };
  }, [eager, fetchModels]);

  /** Fetch the catalog if it isn't loaded yet. A failed fetch leaves just the
      stored model, so opening the picker (or hovering the tile) retries. */
  const load = useCallback(() => {
    if (!loading && models.length === 0) void fetchModels();
  }, [fetchModels, loading, models.length]);

  // Local tones own their model list; catalog tones show the full catalog
  // once loaded, just the active model until then.
  const options = isLocal ? tone.models : models.length ? models : tone.models;

  const select = async (modelId: number) => {
    if (switching) return;
    if (isNaN(modelId) || modelId === block.activeModelId) return;

    // Native only stores the active model, so the switch call carries the
    // model object: from the fetched catalog, or the local tone's own list
    // (whose entries ship their stash model_url).
    const model = (isLocal ? tone.models : models).find((m) => m.id === modelId);
    if (!model?.model_url) return;

    setSwitching(true);
    try {
      await actions.switchModel(blockId, modelId, { ...model, model_url: model.model_url });
    } finally {
      setSwitching(false);
    }
  };

  return { options, loading, switching, load, select };
}
