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

export type MeterId =
  | 'input'
  | 'output'
  | `${'input' | 'output'}:${'l' | 'r'}`
  | `block:${string}:in`
  | `block:${string}:out`;

export const meterId = {
  /** Per-channel main meter ('input'/'output' alone = max of both channels). */
  main: (type: 'input' | 'output', channel: 'l' | 'r'): MeterId => `${type}:${channel}`,
  blockIn: (blockId: string): MeterId => `block:${blockId}:in`,
  blockOut: (blockId: string): MeterId => `block:${blockId}:out`,
};

const FLOOR_DB = -60;
/** Levels at/above this latch the meter's clip indicator until cleared. */
const CLIP_DB = 0;
/** Quantize to 0.5 dB so imperceptible changes don't cause re-renders. */
const QUANTIZE = 2;
/**
 * Minimum time between bridge fetches. rAF fires at display refresh (120 Hz on
 * ProMotion), but ~30 Hz is indistinguishable for meter ballistics — this caps
 * bridge traffic without changing perceived smoothness.
 */
const MIN_FETCH_INTERVAL_MS = 33;

class MeterStore {
  private levels = new Map<string, number>();
  /** Meter ids that have hit CLIP_DB; latched until clearClip(). */
  private clips = new Set<string>();
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

  getClip = (id: string): boolean => this.clips.has(id);

  clearClip = (id: string): void => {
    if (!this.clips.delete(id)) return;
    this.listeners.get(id)?.forEach((callback) => callback());
  };

  private start() {
    this.running = true;
    let lastFetch = 0;
    const tick = async () => {
      if (!this.running) return;
      const now = performance.now();
      if (now - lastFetch >= MIN_FETCH_INTERVAL_MS) {
        lastFetch = now;
        try {
          const res = await this.fetchLevels();
          if (res && this.running) this.apply(res);
        } catch {
          // Backend not ready yet; keep polling.
        }
      }
      if (this.running) requestAnimationFrame(tick);
    };
    tick();
  }

  private stop() {
    this.running = false;
  }

  private apply(res: MeterLevels) {
    this.applyMain('input', res.input);
    this.applyMain('output', res.output);
    for (const [blockId, levels] of Object.entries(res.blocks ?? {})) {
      this.update(meterId.blockIn(blockId), levels.in);
      this.update(meterId.blockOut(blockId), levels.out);
    }
  }

  /** Fan an [L, R] pair out to :l / :r ids plus the combined mono id. */
  private applyMain(type: 'input' | 'output', pair: [number, number]) {
    const l = Array.isArray(pair) ? pair[0] : pair;
    const r = Array.isArray(pair) ? pair[1] : pair;
    this.update(meterId.main(type, 'l'), l);
    this.update(meterId.main(type, 'r'), r);
    this.update(type, Math.max(l, r));
  }

  private update(id: string, raw: number) {
    const finite = typeof raw === 'number' && Number.isFinite(raw);

    // Latch clips on the raw value so a one-frame overshoot can't slip past
    // the quantized/steady-value short-circuit below.
    const clipped = finite && raw >= CLIP_DB && !this.clips.has(id);
    if (clipped) this.clips.add(id);

    const value = finite
      ? Math.round(Math.max(FLOOR_DB, raw) * QUANTIZE) / QUANTIZE
      : FLOOR_DB;
    if (this.levels.get(id) === value && !clipped) return;
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

/**
 * Latched clip state for a meter id (level hit 0 dBFS since last clear).
 * Latching happens in the store on raw poll values, so clips register even if
 * the level has already fallen by the next render. Returns [clipped, clear].
 */
export function useMeterClip(id: MeterId | string): [boolean, () => void] {
  const store = useContext(MeterStoreContext);
  if (!store) throw new Error('useMeterClip must be used within a MetersProvider');

  const subscribe = useCallback(
    (callback: () => void) => store.subscribe(id, callback),
    [store, id]
  );
  const getSnapshot = useCallback(() => store.getClip(id), [store, id]);
  const clipped = useSyncExternalStore(subscribe, getSnapshot);
  const clear = useCallback(() => store.clearClip(id), [store, id]);

  return [clipped, clear];
}
