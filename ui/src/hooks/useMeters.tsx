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

/** Store key for the CPU readout (a %, not a dB level — see useCpuPercent). */
const CPU_ID = 'cpu';

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

/**
 * CPU readout: average samples into the existing ~30 Hz meter poll (no extra
 * bridge traffic), then publish a whole percent at most this often. Instantaneous
 * load jumps every callback; without this the digit flickers unreadable.
 */
const CPU_UI_INTERVAL_MS = 400;
/** EMA weight toward each new sample (~0.5 s time-constant at 30 Hz). */
const CPU_EMA_ALPHA = 0.15;

/**
 * Ids whose clip latches clear together. The main meters' mono and L/R ids
 * are three views of one physical signal (mono = max(L, R)), and which view
 * is on screen changes with stereo mode (e.g. toggling Spread) — so clearing
 * a clip on any of them clears all three. A stale latch would otherwise
 * survive on the hidden variant and reappear on the next mode switch.
 * Block meters have no channel variants; they clear individually.
 */
const clipGroup = (id: string): string[] => {
  const base = id.split(':')[0];
  if (base === 'input' || base === 'output') return [base, `${base}:l`, `${base}:r`];
  return [id];
};

class MeterStore {
  private levels = new Map<string, number>();
  /** Displayed CPU % (one decimal), published at CPU_UI_INTERVAL_MS after EMA smoothing. */
  private cpuPercent = 0;
  private cpuEma = 0;
  private cpuSeeded = false;
  private lastCpuPublishMs = 0;
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

  getCpu = (): number => this.cpuPercent;

  getClip = (id: string): boolean => this.clips.has(id);

  clearClip = (id: string): void => {
    for (const groupId of clipGroup(id)) {
      if (!this.clips.delete(groupId)) continue;
      this.listeners.get(groupId)?.forEach((callback) => callback());
    }
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
    this.applyCpu(res.cpu);
  }

  /**
   * Smooth into an EMA every poll, publish a rounded % on a slow cadence.
   * No extra native calls — `cpu` already rides getMeterLevels.
   */
  private applyCpu(raw: number | undefined) {
    const sample =
      typeof raw === 'number' && Number.isFinite(raw) ? Math.max(0, raw) : 0;
    if (!this.cpuSeeded) {
      this.cpuEma = sample;
      this.cpuSeeded = true;
    } else {
      this.cpuEma += (sample - this.cpuEma) * CPU_EMA_ALPHA;
    }

    const now = performance.now();
    if (now - this.lastCpuPublishMs < CPU_UI_INTERVAL_MS) return;
    this.lastCpuPublishMs = now;

    const percent = Math.round(this.cpuEma * 1000) / 10;
    if (this.cpuPercent === percent) return;
    this.cpuPercent = percent;
    this.listeners.get(CPU_ID)?.forEach((callback) => callback());
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

    const value = finite ? Math.round(Math.max(FLOOR_DB, raw) * QUANTIZE) / QUANTIZE : FLOOR_DB;
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

/** Audio-callback load as a percent (0–100+, one decimal), for the hint-bar readout. */
export function useCpuPercent(): number {
  const store = useContext(MeterStoreContext);
  if (!store) throw new Error('useCpuPercent must be used within a MetersProvider');

  const subscribe = useCallback(
    (callback: () => void) => store.subscribe(CPU_ID, callback),
    [store]
  );
  return useSyncExternalStore(subscribe, store.getCpu);
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
