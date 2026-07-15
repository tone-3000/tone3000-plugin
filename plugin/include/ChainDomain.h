#pragma once

// ── The chain domain: everything between the input stage and the post-chain
// stereo stages runs at a fixed 48 kHz, always ──
//
// NAM A2 models are trained at 48 kHz and IRs are loaded at whatever rate the
// convolver is prepared with, so instead of resampling per block (and paying
// the round trip between every NAM at a non-48k host), the whole chain stage
// is wrapped in ONE stereo resampling boundary:
//
//   host rate ─▶ [boundary in] ─▶ lanes at 48 kHz ─▶ [boundary out] ─▶ host rate
//
// Consequences, all deliberate:
//  - At a 48 kHz host the boundary is skipped entirely: zero latency, zero
//    cost, bit-identical to processing directly.
//  - At any other host rate the boundary engages regardless of chain contents
//    (even empty), so reported latency is a constant per host rate and adding
//    or removing blocks never triggers a PDC change mid-session.
//  - Per-block dry/wet mix blends signals with identical alignment (no comb
//    filtering from per-block resampler latency), and stereo lanes are always
//    sample-aligned with each other.
//
// The Lanczos ResamplingContainer comes from AudioDSPTools — the same engine
// the official NAM plugin uses for this exact job.

// AudioDSPTools compatibility defines (it originally came from iPlug2).
#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 1024
#endif

namespace iplug {
constexpr double PI = 3.14159265358979323846;
}

#include "dsp/ResamplingContainer/ResamplingContainer.h"

/** The fixed internal rate of the block chains. */
constexpr double kChainSampleRate = 48000.0;

/** Stereo Lanczos boundary (filter size 12) between the host rate and the
    chain domain. Only instantiated when the host rate differs from 48 kHz. */
using ChainBoundaryResampler = dsp::ResamplingContainer<float, 2, 12>;
