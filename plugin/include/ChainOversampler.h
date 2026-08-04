#pragma once

#include <array>
#include <cassert>
#include <vector>

// Chain oversampler: power-of-two rate raiser around the chain stage
// (design overview in plugin/docs/oversampling.md).
//
// Wraps the entire chain stage (see ChainDomain.h) in a ×2/×4/×8 oversampling
// round trip: interpolate up, run the chain at kChainBaseSampleRate × factor,
// anti-alias filter and decimate back down. Nonlinear blocks (NAM models)
// generate harmonics into the widened band where they no longer fold back as
// aliasing; the decimation filter removes everything above the base Nyquist.
//
// Each doubling is one polyphase half-band stage built from cascaded
// first-order allpass sections (the classic HIIR structure):
//
//   H(z) = (a + z⁻¹) / (1 + a z⁻¹)        y[n] = a·x[n] + x[n-1] - a·y[n-1]
//
//   upsample:   y[2n] = A(x[n]),  y[2n+1] = B(x[n])
//   decimate:   y[n]  = ½·(B(x[2n]) + A(x[2n+1]))
//
// where A and B are the two allpass branch cascades (6 sections each). This
// is a minimum-phase design: zero reported latency (group delay is a fraction
// of a sample across the audible band), very cheap (24 mul/adds per branch
// pair), and >90 dB stopband rejection. The phase warp near the base Nyquist
// is inaudible, so I keep the filter fixed rather than user-selectable; a
// quality knob here would change nothing anyone can hear.
//
// Coefficients adapted from DLC86/NAM-Oversampler's AudioDSPTools fork (MIT),
// itself following the HIIR half-band allpass tables.
//
// Frame counts stay exact: every input frame becomes exactly `factor` chain
// frames, so downstream phase-interleaved NAM processing (see NamEngine)
// always sees buffers divisible by the factor and the decimator never carries
// a partial pair across blocks.
//
// Two entry points, one per direction:
//   process()             base-rate caller, oversampled section inside: the
//                         chain-stage wrapper (up → chainFn → down).
//   processBaseRateIsland() oversampled caller, base-rate section inside:
//                         per-IR-block islands (down → baseFn → up). IR
//                         convolution is linear, so running it above the base
//                         rate buys nothing and costs ~quadratically (kernel
//                         length × rate); islands pin it at the base rate.
// An instance serves exactly one role (the filter state is shared between
// directions), so the chain wrapper and each IR block own their own.
class ChainOversampler {
public:
  static constexpr int kMaxFactor = 8;

  /** Configure for `factor` (1, 2, 4 or 8; 1 = bypass) and the largest
      base-rate frame count per process() call. Allocates; not RT-safe. */
  void prepare(int newFactor, int maxBaseFrames) {
    assert(newFactor == 1 || newFactor == 2 || newFactor == 4 || newFactor == 8);
    factor = newFactor;
    stages = 0;
    for (int f = factor; f > 1; f /= 2)
      ++stages;

    const size_t osCapacity = static_cast<size_t>(maxBaseFrames) * static_cast<size_t>(factor);
    osCapacityPerChannel = osCapacity;
    for (auto& buf : workBuffers) {
      buf.samples.assign(kChannels * osCapacity, 0.0f);
      buf.pointers = {buf.samples.data(), buf.samples.data() + osCapacity};
    }

    reset();
  }

  /** Clear all filter state (call on transport resets / chain re-prepares). */
  void reset() {
    upA.fill({});
    upB.fill({});
    downEven.fill({});
    downOdd.fill({});
  }

  int getFactor() const { return factor; }
  bool isActive() const { return factor > 1; }

  /** Minimum-phase IIR half-bands: no reported latency. */
  int getLatencySamples() const { return 0; }

  /** Run `chainFn(channels, channels, frames × factor)` at the oversampled
      rate. `inputs`/`outputs` are kChannels base-rate channel pointers; the
      chain function processes in place on the pointers it is given (the same
      contract as the direct path). RT-safe after prepare(). */
  template <typename Fn>
  void process(float** inputs, float** outputs, int numFrames, Fn&& chainFn) {
    if (factor <= 1) {
      chainFn(inputs, outputs, numFrames);
      return;
    }
    assert(static_cast<size_t>(numFrames) * static_cast<size_t>(factor) <= osCapacityPerChannel);

    float** osPointers = workBuffers[0].pointers.data();
    runUpCascade(inputs, osPointers, kChannels, numFrames);
    chainFn(osPointers, osPointers, numFrames * factor);
    runDownCascade(osPointers, outputs, kChannels, numFrames * factor);
  }

  /** The inverse wrap: decimate an oversampled-rate section down to the base
      rate, run `baseFn(channels, frames / factor)` there, and interpolate the
      result back, in place on `channels` (numChannels ≤ 2 oversampled-rate
      pointers, `numFrames` divisible by the factor; guaranteed for chain
      buffers, see process()). RT-safe after prepare(). */
  template <typename Fn>
  void processBaseRateIsland(float* const* channels, int numChannels, int numFrames, Fn&& baseFn) {
    if (factor <= 1) {
      baseFn(channels, numFrames);
      return;
    }
    assert(numChannels >= 1 && numChannels <= kChannels);
    assert(numFrames % factor == 0);
    assert(static_cast<size_t>(numFrames) <= osCapacityPerChannel);

    float** basePointers = workBuffers[0].pointers.data();
    runDownCascade(channels, basePointers, numChannels, numFrames);
    baseFn(basePointers, numFrames / factor);
    runUpCascade(basePointers, channels, numChannels, numFrames / factor);
  }

private:
  static constexpr int kChannels = 2;
  static constexpr int kMaxStages = 3;  // log2(kMaxFactor)
  static constexpr int kSections = 6;

  // Half-band allpass branch coefficients (HIIR tables; see header comment).
  static constexpr std::array<float, kSections> kCoeffsA = {
      0.036681502163648017f, 0.2746317593794541f, 0.56109896978791948f,
      0.769741833862266f,    0.8922608180038789f, 0.962094548378084f};
  static constexpr std::array<float, kSections> kCoeffsB = {
      0.13654762463195771f, 0.42313861743656667f, 0.6775400499741616f,
      0.839889624849638f,   0.9315419599631839f,  0.9878163707328971f};

  struct AllpassState {
    float x1 = 0.0f;
    float y1 = 0.0f;
  };
  using BranchStates = std::array<AllpassState, kMaxStages * kChannels * kSections>;

  static float processAllpass(float x, float a, AllpassState& s) {
    const float y = a * x + s.x1 - a * s.y1;
    s.x1 = x;
    s.y1 = y;
    return y;
  }

  // Cascade runners shared by both public entry points. `finalOut` receives
  // the last stage's result; intermediate stages alternate between the two
  // scratch work buffers ([1]/[2]), so `finalOut` may safely be workBuffers[0]
  // or any external buffer. The staging never collides: each stage reads one
  // buffer and writes another, and [0] is only ever a cascade endpoint.

  /** Base-rate `in` → ×factor `finalOut`, doubling per stage. */
  void runUpCascade(float* const* in, float* const* finalOut, int numChannels, int numFrames) {
    int frames = numFrames;
    float* const* stageIn = in;
    for (int stage = 0; stage < stages; ++stage) {
      float* const* stageOut = stage == stages - 1 ? finalOut
                                                   : workBuffers[1 + stage % 2].pointers.data();
      for (int ch = 0; ch < numChannels; ++ch)
        upsampleStage(stage, ch, stageIn[ch], stageOut[ch], frames);
      stageIn = stageOut;
      frames *= 2;
    }
  }

  /** ×factor `in` → base-rate `finalOut`, halving per stage. */
  void runDownCascade(float* const* in, float* const* finalOut, int numChannels, int numFrames) {
    int frames = numFrames;
    float* const* stageIn = in;
    for (int stage = stages - 1; stage >= 0; --stage) {
      float* const* stageOut =
          stage == 0 ? finalOut : workBuffers[1 + stage % 2].pointers.data();
      for (int ch = 0; ch < numChannels; ++ch)
        decimateStage(stage, ch, stageIn[ch], stageOut[ch], frames);
      stageIn = stageOut;
      frames /= 2;
    }
  }

  static float processBranch(float x, const std::array<float, kSections>& coeffs,
                             BranchStates& states, int stage, int channel) {
    AllpassState* s = &states[(static_cast<size_t>(stage) * kChannels + channel) * kSections];
    float y = x;
    for (int i = 0; i < kSections; ++i)
      y = processAllpass(y, coeffs[static_cast<size_t>(i)], s[i]);
    return y;
  }

  void upsampleStage(int stage, int channel, const float* in, float* out, int numFrames) {
    for (int n = 0; n < numFrames; ++n) {
      const float x = in[n];
      out[2 * n] = processBranch(x, kCoeffsA, upA, stage, channel);
      out[2 * n + 1] = processBranch(x, kCoeffsB, upB, stage, channel);
    }
  }

  void decimateStage(int stage, int channel, const float* in, float* out, int numFrames) {
    assert(numFrames % 2 == 0);
    for (int n = 0; n < numFrames / 2; ++n) {
      const float even = processBranch(in[2 * n], kCoeffsB, downEven, stage, channel);
      const float odd = processBranch(in[2 * n + 1], kCoeffsA, downOdd, stage, channel);
      out[n] = 0.5f * (even + odd);
    }
  }

  int factor = 1;
  int stages = 0;
  size_t osCapacityPerChannel = 0;

  // Work buffers, channel-major (kChannels × osCapacityPerChannel), each with
  // its own stable channel-pointer pair. [0] is the inner-section buffer (the
  // oversampled block for process(), the base-rate block for islands); [1]
  // and [2] hold intermediate cascade stages (×4 needs one, ×8 needs both).
  struct WorkBuffer {
    std::vector<float> samples;
    std::array<float*, kChannels> pointers{};
  };
  std::array<WorkBuffer, 3> workBuffers;

  BranchStates upA{};
  BranchStates upB{};
  BranchStates downEven{};
  BranchStates downOdd{};
};
