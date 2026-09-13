import type { EqBandType } from '../types/chain';
import { EQ_MAX_ABS_GAIN_DB } from '../types/chain';
import { CARD_WIDTH, CARD_HEIGHT, HEADER_HEIGHT } from './chainLayout';

/**
 * Geometry and glyphs shared by the EQ editor views (BlockEqView's graph,
 * EqSliders, SpectrumBackdrop). Both views draw into the same card-body
 * coordinate space so the spectrum backdrop lines up in either one.
 */

// Full card body: outer card minus 1px border each side and the chrome
// header. Spectrum/grid bleed edge-to-edge; interactive chrome is inset
// separately by BODY_PADDING.
export const GRAPH_W = CARD_WIDTH - 2;
export const GRAPH_H = CARD_HEIGHT - 2 - HEADER_HEIGHT;
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

/** Touch double tap window and slop, same as KnobControl's recognizer. */
const DOUBLE_TAP_MS = 300;
const DOUBLE_TAP_SLOP_PX = 24;

/**
 * Pointer-stream double tap for the touch resets (fader caps and curve
 * dots). Detected from pointerdown pairs, not `dblclick`, which WKWebView
 * ties to its own double-tap handling. Keyed by band index so both taps
 * must land on the same control.
 */
export const createDoubleTapDetector = () => {
  let last: { key: number; at: number; x: number; y: number } | null = null;
  return {
    /** Feed each touch pointerdown; true when it completes a double tap. */
    tap: (key: number, e: { timeStamp: number; clientX: number; clientY: number }): boolean => {
      const prev = last;
      const isDouble =
        prev !== null &&
        prev.key === key &&
        e.timeStamp - prev.at < DOUBLE_TAP_MS &&
        Math.abs(e.clientX - prev.x) < DOUBLE_TAP_SLOP_PX &&
        Math.abs(e.clientY - prev.y) < DOUBLE_TAP_SLOP_PX;
      // A third tap starts a fresh pair, it is not another reset.
      last = isDouble ? null : { key, at: e.timeStamp, x: e.clientX, y: e.clientY };
      return isDouble;
    },
    /** Feed touch pointermoves: a press that travels is a drag, not the
        first half of a double tap, so its candidate is withdrawn. */
    move: (e: { clientX: number; clientY: number }): void => {
      if (
        last !== null &&
        (Math.abs(e.clientX - last.x) > DOUBLE_TAP_SLOP_PX ||
          Math.abs(e.clientY - last.y) > DOUBLE_TAP_SLOP_PX)
      )
        last = null;
    },
  };
};
