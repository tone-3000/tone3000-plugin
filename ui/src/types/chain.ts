import type { Tone } from './tone';

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

/** True when the EQ is actually shaping audio (enabled and not flat). */
export function isEqShaping(eq: BlockEqParams): boolean {
  return eq.enabled && !isEqFlat(eq);
}

/** Per-block user-editable parameters (all persisted with the plugin state). */
export interface BlockParams {
  /** Block participates in processing (per-block on/off). */
  enabled: boolean;
  /** Normalized 0..1; 0.5 = unity, ±12 dB. Drives the block's DSP. */
  inputGain: number;
  /** Normalized 0..1; 0.5 = unity, ±12 dB. */
  outputGain: number;
  /** Dry/wet: 0 = dry, 1 = wet. */
  mix: number;
  /** NAM slimmable size: 0.5 = lite, 1.0 = full. */
  namSlimmableSize: number;
  /** Post-block 6-band EQ. Flat = skipped entirely on the audio thread. */
  eq: BlockEqParams;
}

/** The insert placeholder (pass-through slot where new tones are added). */
export interface InsertSlot {
  blockId: string;
  kind: 'insert';
}

/** A real tone block in the chain. */
export interface ToneBlock {
  blockId: string;
  kind: 'tone';
  /** Full TONE3000 tone JSON as stored by native. */
  tone: Tone;
  activeModelId: number;
  /** True when the active model is downloaded, prepared and processing. */
  loaded: boolean;
  /** Capability: NAM slimmable (A2) model loaded — enables the LITE/FULL control. */
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
  /** Host sample rate — the EQ curve math needs it to mirror the audio exactly. */
  sampleRate: number;
  chain: ChainItem[];
}

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
