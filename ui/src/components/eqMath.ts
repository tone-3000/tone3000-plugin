import type { EqBand } from '../types/chain';
import { EQ_MAX_FREQ_HZ, EQ_MIN_FREQ_HZ, isEqBandActive } from '../types/chain';

/**
 * Exact TypeScript mirror of the native biquad math (plugin/src/BlockEq.cpp,
 * RBJ Audio EQ Cookbook with A = 10^(dB/40)) so the drawn curve is the audio
 * truth, not an approximation. Keep both sides in sync.
 */

interface BiquadCoeffs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

function computeCoeffs(band: EqBand, sampleRate: number): BiquadCoeffs {
  const freq = Math.min(
    Math.max(band.freqHz, EQ_MIN_FREQ_HZ),
    Math.min(EQ_MAX_FREQ_HZ, sampleRate * 0.49)
  );
  const A = Math.pow(10, band.gainDb / 40);
  const omega = (2 * Math.PI * freq) / sampleRate;
  const sn = Math.sin(omega);
  const cs = Math.cos(omega);
  const alpha = sn / (2 * band.q);
  const sqrtA = Math.sqrt(A);

  let b0 = 1;
  let b1 = 0;
  let b2 = 0;
  let a0 = 1;
  let a1 = 0;
  let a2 = 0;

  switch (band.type) {
    case 'lowcut': // highpass
      b0 = (1 + cs) * 0.5;
      b1 = -(1 + cs);
      b2 = (1 + cs) * 0.5;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    case 'highcut': // lowpass
      b0 = (1 - cs) * 0.5;
      b1 = 1 - cs;
      b2 = (1 - cs) * 0.5;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    case 'bell':
      b0 = 1 + alpha * A;
      b1 = -2 * cs;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cs;
      a2 = 1 - alpha / A;
      break;
    case 'lowshelf':
      b0 = A * (A + 1 - (A - 1) * cs + 2 * sqrtA * alpha);
      b1 = 2 * A * (A - 1 - (A + 1) * cs);
      b2 = A * (A + 1 - (A - 1) * cs - 2 * sqrtA * alpha);
      a0 = A + 1 + (A - 1) * cs + 2 * sqrtA * alpha;
      a1 = -2 * (A - 1 + (A + 1) * cs);
      a2 = A + 1 + (A - 1) * cs - 2 * sqrtA * alpha;
      break;
    case 'highshelf':
      b0 = A * (A + 1 + (A - 1) * cs + 2 * sqrtA * alpha);
      b1 = -2 * A * (A - 1 + (A + 1) * cs);
      b2 = A * (A + 1 + (A - 1) * cs - 2 * sqrtA * alpha);
      a0 = A + 1 - (A - 1) * cs + 2 * sqrtA * alpha;
      a1 = 2 * (A - 1 - (A + 1) * cs);
      a2 = A + 1 - (A - 1) * cs - 2 * sqrtA * alpha;
      break;
  }

  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

/** |H(e^jω)| in dB of a normalized biquad at a single frequency. */
function biquadMagnitudeDb(c: BiquadCoeffs, freqHz: number, sampleRate: number): number {
  const omega = (2 * Math.PI * freqHz) / sampleRate;
  const cosW = Math.cos(omega);
  const cos2W = Math.cos(2 * omega);
  const num =
    c.b0 * c.b0 +
    c.b1 * c.b1 +
    c.b2 * c.b2 +
    2 * (c.b0 * c.b1 + c.b1 * c.b2) * cosW +
    2 * c.b0 * c.b2 * cos2W;
  const den = 1 + c.a1 * c.a1 + c.a2 * c.a2 + 2 * (c.a1 + c.a1 * c.a2) * cosW + 2 * c.a2 * cos2W;
  const magSq = num / Math.max(den, 1e-24);
  return 10 * Math.log10(Math.max(magSq, 1e-24));
}

/**
 * Combined EQ magnitude response (dB) at each of `freqsHz`. Inert bands are
 * skipped, matching the audio thread, which doesn't process them either.
 */
export function eqResponseDb(bands: EqBand[], sampleRate: number, freqsHz: number[]): number[] {
  const active = bands.filter(isEqBandActive).map((band) => computeCoeffs(band, sampleRate));
  return freqsHz.map((f) => {
    let db = 0;
    for (const coeffs of active) db += biquadMagnitudeDb(coeffs, f, sampleRate);
    return db;
  });
}

const LOG_MIN = Math.log(EQ_MIN_FREQ_HZ);
const LOG_MAX = Math.log(EQ_MAX_FREQ_HZ);

/** Frequency → 0..1 position on the log-scaled x axis (20 Hz .. 20 kHz). */
export function freqToNorm(freqHz: number): number {
  const clamped = Math.min(Math.max(freqHz, EQ_MIN_FREQ_HZ), EQ_MAX_FREQ_HZ);
  return (Math.log(clamped) - LOG_MIN) / (LOG_MAX - LOG_MIN);
}

/** 0..1 x position → frequency (inverse of freqToNorm). */
export function normToFreq(norm: number): number {
  const t = Math.min(Math.max(norm, 0), 1);
  return Math.exp(LOG_MIN + t * (LOG_MAX - LOG_MIN));
}

/** "251 Hz" / "1.6k" style display. */
export function formatFreq(freqHz: number): string {
  if (freqHz >= 10000) return `${(freqHz / 1000).toFixed(1)}k`;
  if (freqHz >= 1000) return `${(freqHz / 1000).toFixed(2)}k`;
  return `${Math.round(freqHz)} Hz`;
}
