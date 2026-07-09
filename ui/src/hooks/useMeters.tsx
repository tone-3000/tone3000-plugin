import React, {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useSyncExternalStore,
} from 'react';
import { useAudioBackend } from './useAudioBackend';
import type { MeterLevels } from '../types/chain';

/**
 * Meter transport: ONE native `getMeterLevels` call per animation frame feeds
 * every meter in the plugin (global input/output + per-block in/out). Each
 * meter component subscribes to its own id and only re-renders when its own
 * (quantized) value changes. The poll loop runs only while at least one meter
 * is mounted.
 */

export type MeterId = 'input' | 'output' | `block:${string}:in` | `block:${string}:out`;

export const meterId = {
  blockIn: (blockId: string): MeterId => `block:${blockId}:in`,
  blockOut: (blockId: string): MeterId => `block:${blockId}:out`,
};

const FLOOR_DB = -60;
/** Quantize to 0.5 dB so imperceptible changes don't cause re-renders. */
const QUANTIZE = 2;

class MeterStore {
  private levels = new Map<string, number>();
  private listeners = new Map<string, Set<() => void>>();
  private subscriberCount = 0;
  private running = false;
  private fetchLevels: () => Promise<MeterLevels | null>;

  constructor(fetchLevels: () => Promise<MeterLevels | null>) {
    this.fetchLevels = fetchLevels;
  }

  subscribe = (id: string, callback: () => void): (() => void) => {
    let set = this.listeners.get(id);
    if (!set) {
      set = new Set();
      this.listeners.set(id, set);
    }
    set.add(callback);
    this.subscriberCount += 1;
    if (!this.running) this.start();

    return () => {
      set.delete(callback);
      if (set.size === 0) this.listeners.delete(id);
      this.subscriberCount -= 1;
      if (this.subscriberCount <= 0) this.stop();
    };
  };

  get = (id: string): number => this.levels.get(id) ?? FLOOR_DB;

  private start() {
    this.running = true;
    const tick = async () => {
      if (!this.running) return;
      try {
        const res = await this.fetchLevels();
        if (res && this.running) this.apply(res);
      } catch {
        // Backend not ready yet; keep polling.
      }
      if (this.running) requestAnimationFrame(tick);
    };
    tick();
  }

  private stop() {
    this.running = false;
  }

  private apply(res: MeterLevels) {
    this.update('input', res.input);
    this.update('output', res.output);
    for (const [blockId, levels] of Object.entries(res.blocks ?? {})) {
      this.update(meterId.blockIn(blockId), levels.in);
      this.update(meterId.blockOut(blockId), levels.out);
    }
  }

  private update(id: string, raw: number) {
    const value =
      typeof raw === 'number' && Number.isFinite(raw)
        ? Math.round(Math.max(FLOOR_DB, raw) * QUANTIZE) / QUANTIZE
        : FLOOR_DB;
    if (this.levels.get(id) === value) return;
    this.levels.set(id, value);
    this.listeners.get(id)?.forEach((callback) => callback());
  }
}

const MeterStoreContext = createContext<MeterStore | null>(null);

export const MetersProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const backend = useAudioBackend();
  const store = useMemo(() => {
    const fn = backend.getPluginFunction('getMeterLevels');
    return new MeterStore(() => fn() as Promise<MeterLevels | null>);
  }, [backend]);

  return <MeterStoreContext.Provider value={store}>{children}</MeterStoreContext.Provider>;
};

/** Current level (dB, -60 floor) for a meter id. Re-renders per visible change. */
export function useMeter(id: MeterId | string): number {
  const store = useContext(MeterStoreContext);
  if (!store) throw new Error('useMeter must be used within a MetersProvider');

  const subscribe = useCallback(
    (callback: () => void) => store.subscribe(id, callback),
    [store, id]
  );
  const getSnapshot = useCallback(() => store.get(id), [store, id]);

  return useSyncExternalStore(subscribe, getSnapshot);
}
