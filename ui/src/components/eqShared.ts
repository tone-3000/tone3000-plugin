import type { EqBandType } from '../types/chain';
import { EQ_MAX_ABS_GAIN_DB } from '../types/chain';
import { CARD_WIDTH, CARD_HEIGHT } from './chainLayout';

/**
 * Geometry and glyphs shared by the EQ editor views (BlockEqView's graph,
 * EqSliders, SpectrumBackdrop). Both views draw into the same card-body
 * coordinate space so the spectrum backdrop lines up in either one.
 */

// Full card body: card width/height minus the 2px border, minus the 40px header.
export const GRAPH_W = CARD_WIDTH - 2;
export const GRAPH_H = CARD_HEIGHT - 2 - 40;
export const GRAPH_PAD_Y = 12; // keep dots inside the frame at ±15 dB

export const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), hi);

export const gainToY = (gainDb: number) =>
  GRAPH_H / 2 - (gainDb / EQ_MAX_ABS_GAIN_DB) * (GRAPH_H / 2 - GRAPH_PAD_Y);
export const yToGain = (y: number) =>
  ((GRAPH_H / 2 - y) / (GRAPH_H / 2 - GRAPH_PAD_Y)) * EQ_MAX_ABS_GAIN_DB;

export const hasGain = (type: EqBandType) =>
  type === 'bell' || type === 'lowshelf' || type === 'highshelf';

export const BAND_TYPE_LABELS: Record<EqBandType, string> = {
  lowcut: 'Low Cut',
  lowshelf: 'Low Shelf',
  bell: 'Bell',
  highshelf: 'High Shelf',
  highcut: 'High Cut',
};

/** 16x14 curve glyphs for the type selector / band labels. */
export const TYPE_GLYPHS: Record<EqBandType, string> = {
  lowshelf: 'M1 11 C5 11 6 3 10 3 L15 3',
  bell: 'M1 11 C4 11 5 3 8 3 C11 3 12 11 15 11',
  highshelf: 'M1 3 C5 3 6 11 10 11 L15 11',
  lowcut: 'M1 13 C4 13 5 3 9 3 L15 3',
  highcut: 'M1 3 L7 3 C11 3 12 13 15 13',
};
