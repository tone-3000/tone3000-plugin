/**
 * Chain state model — mirrors the native `getChainState` payload.
 *
 * Design notes:
 * - Tone metadata is *nested* under `tone` (never spread into the block), so
 *   runtime fields can't collide with API fields and the tone object stays a
 *   verbatim copy of what TONE3000 returned.
 * - User-editable settings live under `params`, separate from runtime status
 *   (`loaded`, `namSlimmable`). A future shareable chain preset is just
 *   `{ tone, activeModelId, params }` per block.
 * - `revision` is a monotonic counter bumped by native on every mutation;
 *   pollers pass it back to `getChainState` and get a tiny
 *   `{ revision, unchanged: true }` reply when nothing changed.
 */

/** Which chain is being edited in stereo mode. */
export type ChainSide = 'left' | 'right';

/**
 * Per-block post EQ (runs after the block's output gain + mix).
 * Band curve types match BlockEq::BandType on the native side.
 */
export type EqBandType = 'lowcut' | 'lowshelf' | 'bell' | 'highshelf' | 'highcut';

export interface EqBand {
  type: EqBandType;
  freqHz: number;
  gainDb: number;
  q: number;
}

export interface BlockEqParams {
  /** EQ power/bypass. Band settings persist while disabled. */
  enabled: boolean;
  bands: EqBand[];
}

export const EQ_NUM_BANDS = 6;
export const EQ_MIN_FREQ_HZ = 20;
export const EQ_MAX_FREQ_HZ = 20000;
export const EQ_MAX_ABS_GAIN_DB = 15;
export const EQ_MIN_Q = 0.1;
export const EQ_MAX_Q = 10;

/** A bell/shelf band at ~0 dB is inert; cuts shape by nature. */
export function isEqBandActive(band: EqBand): boolean {
  if (band.type === 'lowcut' || band.type === 'highcut') return true;
  return Math.abs(band.gainDb) >= 0.05;
}

/**
 * Fixed channel-strip band roles (enforced natively too): the first band is
 * a low cut or low shelf, the last a high cut or high shelf, everything in
 * between is a bell.
 */
export function eqBandTypeOptions(index: number): EqBandType[] {
  if (index === 0) return ['lowcut', 'lowshelf'];
  if (index === EQ_NUM_BANDS - 1) return ['highshelf', 'highcut'];
  return ['bell'];
}

/** True when the EQ has no audible effect (all bands inert). */
export function isEqFlat(eq: BlockEqParams): boolean {
  return !eq.bands.some(isEqBandActive);
}

/** Per-block user-editable parameters (all persisted with the plugin state). */
export interface BlockParams {
  /** Block participates in processing (per-block on/off). */
  enabled: boolean;
  /** Normalized 0..1; 0.5 = unity, ±24 dB. Drives the block's DSP. */
  inputGain: number;
  /** Normalized 0..1; 0.5 = unity, ±24 dB. */
  outputGain: number;
  /** Dry/wet: 0 = dry, 1 = wet. */
  mix: number;
  /** NAM slimmable size: 0.0 = lite, 1.0 = full (tier boundaries live in
      the native mapper — see ChainBlock's LITE/FULL toggle). */
  namSlimmableSize: number;
  /** Post-block 6-band EQ. Flat = skipped entirely on the audio thread. */
  eq: BlockEqParams;
}

/**
 * An insert placeholder (pass-through slot where new tones are added).
 * Native keeps each lane at its minimum slot layout — at least 5 tiles and
 * always one trailing insert once every minimum slot holds a tone — so a
 * lane can carry several of these, each independently reorderable.
 */
export interface InsertSlot {
  blockId: string;
  kind: 'insert';
}

/**
 * Slim tone projection shipped by native (see makeToneSummary in
 * ProcessorChain.cpp). Only what the UI renders — the full API payload
 * (model URLs, tags, counts, …) stays native-side.
 */
export interface ToneSummary {
  id: number;
  title: string;
  format?: string;
  gear?: string;
  /** First image only (block artwork). */
  images?: string[];
  user?: { username: string; avatar_url: string };
  /** Only the active model — the picker pages the catalog from the API. */
  models: { id: number; name: string }[];
  /** Catalog totals (picker count). NAM uses `a2_models_count` — the plugin
      only loads v2 architectures. */
  models_count: number;
  a2_models_count: number;
}

/** A real tone block in the chain. */
export interface ToneBlock {
  blockId: string;
  kind: 'tone';
  /** Tone metadata for rendering (slim projection of the API tone). */
  tone: ToneSummary;
  activeModelId: number;
  /** True when the active model is downloaded, prepared and processing. */
  loaded: boolean;
  /** True when the last download/prepare of the active model failed (network
      down, TONE3000 unreachable). The block renders a retry affordance
      instead of loading dots; retry re-queues via `retryModelLoad`. */
  loadFailed: boolean;
  /** Capability flag from native (model is a SlimmableContainer). UI no longer
      gates on this — architecture=2 NAM tones always show LITE/FULL. */
  namSlimmable: boolean;
  params: BlockParams;
}

export type ChainItem = InsertSlot | ToneBlock;

export function isInsertSlot(item: ChainItem): item is InsertSlot {
  return item.kind === 'insert';
}

/** One entry in the native preset store (see getPresetList). */
export interface PresetInfo {
  id: string;
  name: string;
  /** Bundled TONE3000 preset — read-only (no rename/delete). */
  factory: boolean;
}

/** The preset shown in the top-bar pill. */
export interface ActivePreset {
  id: string;
  name: string;
}

export interface ChainState {
  revision: number;
  /** Chain edit history (undo/redo). Native flips these together with a
      revision bump, so pollers always see them fresh. */
  canUndo: boolean;
  canRedo: boolean;
  /** Active preset, absent when none is loaded. Changes with revision bumps. */
  preset?: ActivePreset;
  stereoEnabled: boolean;
  activeSide: ChainSide;
  /** True when a real stereo source feeds the plugin (stereo host bus or a
      stereo standalone input device). Drives the dual input meter/gain UI. */
  stereoInput: boolean;
  /** True in the standalone app — gates standalone-only settings (input mode). */
  standalone: boolean;
  /** Standalone input channel mode. Interfaces expose stereo pairs even with
      one jack plugged in, so the user picks what actually carries signal. */
  inputMode: InputMode;
  /** The chain-domain processing rate (fixed 48000 — the whole chain runs at
      48 kHz behind one resampling boundary). The EQ curve math needs it to
      mirror the audio exactly. */
  sampleRate: number;
  /** Left lane (the only lane in mono mode). */
  chain: ChainItem[];
  /** Right lane — present only while stereo mode is on. */
  chainRight?: ChainItem[];
}

/** Standalone input channel mode (mirrors Processor::InputMode). */
export type InputMode = 'input1' | 'input2' | 'stereo';

/** Minimal reply when the caller's revision is still current. */
export interface ChainStateUnchanged {
  revision: number;
  unchanged: true;
}

export type ChainStateResponse = ChainState | ChainStateUnchanged;

export function isUnchanged(res: ChainStateResponse): res is ChainStateUnchanged {
  return 'unchanged' in res && res.unchanged === true;
}

/** Param names accepted by the native `setBlockParam` function. */
export type BlockParamName = 'enabled' | 'inputGain' | 'outputGain' | 'mix' | 'namSlimmableSize';

/** Payload of the native `getMeterLevels` function (all values dB, -60 floor).
    Main meters ship as [L, R] pairs; mono sources report L == R. */
export interface MeterLevels {
  input: [number, number];
  output: [number, number];
  blocks: Record<string, { in: number; out: number }>;
}
