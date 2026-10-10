// IR CPU vs the host's *promised* maximum block size (github issue #146).
//
// juce::dsp::Convolution sizes its FFT partition from the maximumBlockSize
// it is prepared with and runs a full FFT pair of that size on every
// process() call. Ardour promises 8192 to every LV2 plugin while running
// 64-sample cycles, so convolvers prepared from the host's promise made one
// IR block cost ~85% of a core. The processor now caps the convolver's
// prepared block at kIrConvolverMaxBlockSize and chunks larger blocks
// (ChainBlock.h), so an oversized promise can't inflate what a callback
// costs, while hosts at or below the cap keep exactly the partition they
// always had.
//
// Three guards:
//  - the sizing policy itself (irConvolverBlockSizeFor): identity up to the
//    cap, so honest small-buffer hosts see no change at all;
//  - CPU: the partition size is invisible from outside the processor and
//    CPU is its only symptom. The pinned gap is ~45x (promise 8192 vs 64
//    before the fix) against ~1.7x after it (a 256 partition fed 64 does a
//    512-point FFT pair per call where a 64 partition does 256-point), so a
//    3x bound leaves margin both ways, and taking the fastest of several
//    runs keeps a busy machine from inflating either side;
//  - sound: the whole processor (island, chunking, normalization) must
//    render the same audio whatever block size the host promises or
//    actually delivers.
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr int kRealBlock = 64;

// A processor with one IR block loaded and ready, prepared as a host that
// promised `promisedBlock` would prepare it.
std::unique_ptr<ChainTestProcessor> makeIrProcessor(const char* irFile, const juce::String& gear,
                                                    int promisedBlock) {
  auto proc = std::make_unique<ChainTestProcessor>();
  proc->setPlayConfigDetails(2, 2, kFs, promisedBlock);
  proc->prepareToPlay(kFs, promisedBlock);

  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree("blk-ir", 1, 100, irFile, gear), nullptr);
  state.appendChild(lane, nullptr);
  proc->restoreFromTree(state);
  EXPECT_TRUE(waitForChainLoaded(*proc)) << irFile << " never finished loading";
  return proc;
}

// Left-channel output of `proc` fed `in` on both channels in `callbackBlock`
// frame callbacks (the last one partial if the length doesn't divide).
std::vector<float> renderLeft(TONE3000Processor& proc, const std::vector<float>& in,
                              int callbackBlock) {
  const int total = static_cast<int>(in.size());
  std::vector<float> out(in.size(), 0.0f);
  juce::AudioBuffer<float> buffer(2, callbackBlock);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += callbackBlock) {
    const int frames = std::min(callbackBlock, total - off);
    juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), 2, frames);
    view.copyFrom(0, 0, in.data() + off, frames);
    view.copyFrom(1, 0, in.data() + off, frames);
    proc.processBlock(view, midi);
    std::copy(view.getReadPointer(0), view.getReadPointer(0) + frames,
              out.begin() + off);
  }
  return out;
}

// Wall time to push `seconds` of audio through a one-IR-block chain in
// kRealBlock callbacks, after the host prepared it with `promisedBlock`.
double secondsToProcess(const char* irFile, const juce::String& gear, int promisedBlock,
                        double seconds) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, promisedBlock);
  proc.prepareToPlay(kFs, promisedBlock);

  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree("blk-ir", 1, 100, irFile, gear), nullptr);
  state.appendChild(lane, nullptr);
  proc.restoreFromTree(state);
  EXPECT_TRUE(waitForChainLoaded(proc)) << irFile << " never finished loading";

  const auto noise = makeNoise(kRealBlock, 99, 0.25f);
  juce::AudioBuffer<float> buffer(2, kRealBlock);
  juce::MidiBuffer midi;
  auto pump = [&](int calls) {
    for (int i = 0; i < calls; ++i) {
      buffer.copyFrom(0, 0, noise.data(), kRealBlock);
      buffer.copyFrom(1, 0, noise.data(), kRealBlock);
      proc.processBlock(buffer, midi);
    }
  };

  pump(static_cast<int>(0.25 * kFs / kRealBlock));  // warm-up
  const auto start = std::chrono::steady_clock::now();
  pump(static_cast<int>(seconds * kFs / kRealBlock));
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

  float peak = 0.0f;
  for (int ch = 0; ch < 2; ++ch)
    peak = std::max(peak, buffer.getMagnitude(ch, 0, kRealBlock));
  EXPECT_GT(peak, 1e-4f) << irFile << ": chain output is silent";
  return elapsed.count();
}

double fastestOf(int runs, const char* irFile, const juce::String& gear, int promisedBlock) {
  double best = 1e9;
  for (int i = 0; i < runs; ++i)
    best = std::min(best, secondsToProcess(irFile, gear, promisedBlock, 1.0));
  return best;
}

}  // namespace

TEST(IrBlockSizeTest, ConvolverBlockSizeIsHostBlockCappedAtMax) {
  // The no-regression promise for honest hosts: at or below the cap the
  // convolver is prepared at exactly the host's base block, as it always
  // was. Only above the cap does the size change.
  for (int hostBlock : {1, 32, 64, 70, 128, 255, 256})
    EXPECT_EQ(irConvolverBlockSizeFor(hostBlock), hostBlock) << "host block " << hostBlock;
  for (int hostBlock : {257, 512, 1000, 1024, 4096, 8192})
    EXPECT_EQ(irConvolverBlockSizeFor(hostBlock), kIrConvolverMaxBlockSize)
        << "host block " << hostBlock;
  EXPECT_EQ(irConvolverBlockSizeFor(0), 1) << "never prepare(0)";
}

TEST(IrBlockSizeTest, IrCpuDoesNotScaleWithPromisedMaxBlockSize) {
  struct Ir {
    const char* file;
    const char* gear;
  };
  // Both engines the loader builds: uniform (cab) and NonUniform (reverb).
  for (const Ir ir : {Ir{"cab-ir-test.wav", "cab"}, Ir{"reverb-ir-mono-test.wav", "space"}}) {
    const double honest = fastestOf(3, ir.file, ir.gear, kRealBlock);
    const double ardour = fastestOf(3, ir.file, ir.gear, 8192);
    EXPECT_LT(ardour, honest * 3.0)
        << ir.file << ": 64-sample callbacks took " << ardour << " s when the host promised 8192 vs "
        << honest << " s when it promised 64; convolvers are being sized from the promise again";
  }
}

TEST(IrBlockSizeTest, IrSoundIsIndependentOfPromisedAndDeliveredBlockSize) {
  // The sound-side half of the fixed-partition change. Through the *whole*
  // processor (the IR block's base-rate island, processConvolverInChunks,
  // unit-energy normalization), the rendered audio must not depend on the
  // block size the host promised at prepare, nor on how many frames each
  // callback actually carries. Reference: a host that promises 64 and
  // delivers 64 (Reaper-style). Against it: Ardour's 8192 promise with 64
  // delivered; a 512 host (two 256 chunks per call); a 1000 host (chunks
  // straddle the 256 partition, the awkward case); and an 8192 host that
  // really delivers 8192 (32 chunks per call).
  //
  // Bound: -80 dB relative to peak. A different partition or delivered block
  // size changes the order float FFT convolution accumulates in, and that
  // alone measures ~1.5e-5 relative here (the same spread the pre-fix code
  // had between hosts of different block sizes, so it isn't something this
  // change added). A real bug (a dropped chunk, a wrong partition, a
  // misaligned overlap) is at the 1e-1 level.
  struct Host {
    int promised;
    int delivered;
  };
  const Host reference{kRealBlock, kRealBlock};
  const Host others[] = {{8192, kRealBlock}, {512, 512}, {1000, 1000}, {8192, 8192}};

  const int total = 2 * static_cast<int>(kFs);
  const auto in = makeNoise(total, 2026, 0.25f);
  // Skip the block's install wet-fade and the normalization smoother.
  const size_t settled = 16384;

  for (const char* irFile : {"cab-ir-test.wav", "reverb-ir-mono-test.wav"}) {
    const juce::String gear = juce::String(irFile).startsWith("cab") ? "cab" : "space";
    auto refProc = makeIrProcessor(irFile, gear, reference.promised);
    const auto expected = renderLeft(*refProc, in, reference.delivered);

    float peak = 0.0f;
    for (size_t i = settled; i < expected.size(); ++i)
      peak = std::max(peak, std::abs(expected[i]));
    ASSERT_GT(peak, 1e-3f) << irFile << ": reference render is silent";

    for (const Host host : others) {
      auto proc = makeIrProcessor(irFile, gear, host.promised);
      const auto actual = renderLeft(*proc, in, host.delivered);
      ASSERT_EQ(actual.size(), expected.size());

      float maxDiff = 0.0f;
      for (size_t i = settled; i < expected.size(); ++i)
        maxDiff = std::max(maxDiff, std::abs(actual[i] - expected[i]));
      EXPECT_LT(maxDiff, peak * 1e-4f)
          << irFile << ": host promising " << host.promised << " and delivering "
          << host.delivered << " frames renders differently from 64/64 (max diff " << maxDiff
          << ", peak " << peak << ")";
    }
  }
}
