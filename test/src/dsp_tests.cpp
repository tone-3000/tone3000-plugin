// ── TONE3000 DSP test suite ──
//
// Verifies the load-bearing DSP properties of the chain against the real
// test assets in test/files (A2 NAM captures + cab/reverb IRs):
//
//   ChainOversamplerTest  the ×2/4/8 wrapper and the IR base-rate island:
//                         null tests, frame-count contracts, transparency,
//                         group delay, alias reduction, phase-interleave math.
//   NamEngineTest         real A2 WaveNet models: load/run sanity, slicing
//                         invariance, exactness of the phase-interleaved
//                         oversampled mode against a hand-built reference,
//                         and the headline claim — oversampling reduces a
//                         real amp's aliasing.
//   IrConvolutionTest     real IR files through juce::dsp::Convolution: the
//                         short/long classification cutoff, true-stereo vs
//                         mono kernels, and the island guarantee — an IR
//                         inside the ×8 oversampled chain matches base-rate
//                         convolution.
//
// Run locally:  ./script/test-dsp.sh          (configures/builds/runs)
// or manually:  cmake --build build --target DspTests
//               ctest --test-dir build -R Dsp --output-on-failure
#include "ChainOversampler.h"
#include "NamEngine.h"

#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "NAM/get_dsp.h"
#include "json.hpp"

#include <cmath>
#include <complex>
#include <memory>
#include <random>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;  // == kChainBaseSampleRate

// ── Shared helpers ──

juce::File testFile(const char* name) {
  const auto f = juce::File(T3K_TEST_FILES_DIR).getChildFile(name);
  EXPECT_TRUE(f.existsAsFile()) << "missing test asset: " << f.getFullPathName().toStdString();
  return f;
}

// Hann-windowed DFT magnitude² at one frequency (relative to kFs). A
// full-scale sine reads 1.0 → 0 dB.
double goertzelPower(const float* x, size_t n, double freq) {
  std::complex<double> acc{0.0, 0.0};
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    const double ph = -2.0 * kPi * freq / kFs * static_cast<double>(i);
    acc += w * static_cast<double>(x[i]) * std::complex<double>(std::cos(ph), std::sin(ph));
  }
  const double mag = std::abs(acc) / (0.25 * static_cast<double>(n));
  return mag * mag;
}

double goertzelPower(const std::vector<float>& x, double freq) {
  return goertzelPower(x.data(), x.size(), freq);
}

double db(double power) { return 10.0 * std::log10(std::max(power, 1e-30)); }

// Frequencies every harmonic h·f0 above the base Nyquist folds back to.
double foldFrequency(double f) {
  f = std::fmod(f, kFs);
  return f > kFs / 2 ? kFs - f : f;
}

// Streams `in` through a chain-role ChainOversampler in `blockSize` chunks;
// `chainFn(channels, frames)` processes the oversampled stereo block in
// place. Returns the base-rate output (channel 0).
template <typename Fn>
std::vector<float> runOversampledChain(const std::vector<float>& in, int factor, int blockSize,
                                       Fn&& chainFn) {
  ChainOversampler os;
  os.prepare(factor, blockSize);
  std::vector<float> out(in.size(), 0.0f);
  std::vector<float> silentIn(in.size(), 0.0f), silentOut(in.size(), 0.0f);
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(blockSize)) {
    const int frames = static_cast<int>(std::min<size_t>(blockSize, in.size() - off));
    float* ins[2] = {const_cast<float*>(in.data()) + off, silentIn.data() + off};
    float* outs[2] = {out.data() + off, silentOut.data() + off};
    os.process(ins, outs, frames, [&](float** ci, float** co, int n) {
      for (int ch = 0; ch < 2; ++ch)
        if (co[ch] != ci[ch])
          std::copy(ci[ch], ci[ch] + n, co[ch]);
      chainFn(co, n);
    });
  }
  return out;
}

std::vector<float> makeSine(int frames, double freq, float amplitude = 1.0f) {
  std::vector<float> x(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i)
    x[static_cast<size_t>(i)] =
        amplitude * static_cast<float>(std::sin(2.0 * kPi * freq * i / kFs));
  return x;
}

std::vector<float> makeNoise(int frames, unsigned seed, float amplitude = 1.0f) {
  std::vector<float> x(static_cast<size_t>(frames));
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-amplitude, amplitude);
  for (auto& s : x)
    s = dist(rng);
  return x;
}

// ═════════════════════════ ChainOversampler ═════════════════════════

TEST(ChainOversamplerTest, Factor1IsBitExactPassthrough) {
  const int blockSize = 512, total = 93 * 512;
  const auto in = makeNoise(total, 42);
  const auto out = runOversampledChain(in, 1, blockSize, [](float**, int) {});
  EXPECT_EQ(out, in);  // the OS-off null test: not one bit may move
}

TEST(ChainOversamplerTest, ChainStageFrameCountContract) {
  // Every base frame must become exactly `factor` chain frames per call —
  // NamEngine's phase interleave and the decimator's pairing depend on it.
  const int blockSize = 512, total = 24 * 512;
  const std::vector<float> in(static_cast<size_t>(total), 0.0f);
  for (int factor : {2, 4, 8}) {
    int calls = 0;
    bool framesOk = true;
    runOversampledChain(in, factor, blockSize, [&](float**, int n) {
      framesOk = framesOk && n == blockSize * factor;
      ++calls;
    });
    EXPECT_TRUE(framesOk) << "factor " << factor;
    EXPECT_EQ(calls, total / blockSize) << "factor " << factor;
  }
}

TEST(ChainOversamplerTest, PassbandTransparentWithTinyGroupDelay) {
  const int blockSize = 512, total = 93 * 512;
  const double f0 = 997.0;
  const auto in = makeSine(total, f0);
  for (int factor : {2, 4, 8}) {
    const auto out = runOversampledChain(in, factor, blockSize, [](float**, int) {});

    std::vector<float> tail(out.begin() + 8192, out.begin() + 8192 + 16384);
    const double gainDb = db(goertzelPower(tail, f0));
    EXPECT_NEAR(gainDb, 0.0, 0.1) << "factor " << factor << ": passband gain moved";

    // Min-phase halfbands report zero PDC latency but carry a few samples of
    // physical group delay — it must stay tiny and not blow up with factor.
    int bestLag = -1;
    double bestCorr = -1e30;
    for (int lag = 0; lag < 32; ++lag) {
      double corr = 0.0;
      for (int i = 8192; i < 8192 + 4096; ++i)
        corr += static_cast<double>(out[static_cast<size_t>(i)]) *
                static_cast<double>(in[static_cast<size_t>(i - lag)]);
      if (corr > bestCorr) {
        bestCorr = corr;
        bestLag = lag;
      }
    }
    EXPECT_LE(bestLag, 8) << "factor " << factor << ": group delay grew";
  }
}

TEST(ChainOversamplerTest, HardClipperAliasingDropsWithFactor) {
  // A clipped 4998 Hz sine has harmonics far above the base Nyquist; at
  // factor 1 they fold back to known inharmonic frequencies. Oversampling
  // must push those folded components down (until the float noise floor).
  const int blockSize = 512, total = 93 * 512;
  const double f0 = 4998.046875;
  const auto in = makeSine(total, f0, 2.5f);

  auto clipper = [](float** ch, int n) {
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < n; ++i)
        ch[c][i] = std::max(-1.0f, std::min(1.0f, ch[c][i]));
  };

  std::vector<double> aliasFreqs;
  for (int h = 5; h <= 15; h += 2)  // clipped sine: odd harmonics only
    aliasFreqs.push_back(foldFrequency(h * f0));

  double aliasDb1 = 0.0, prevAlias = 0.0;
  for (int factor : {1, 2, 4, 8}) {
    const auto out = runOversampledChain(in, factor, blockSize, clipper);
    std::vector<float> tail(out.begin() + 8192, out.begin() + 8192 + 16384);

    double aliasPower = 0.0;
    for (double f : aliasFreqs)
      aliasPower += goertzelPower(tail, f);
    const double aliasRatioDb = db(aliasPower) - db(goertzelPower(tail, f0));
    std::printf("  clipper factor %d: folded harmonics vs fundamental = %+.1f dB\n", factor,
                aliasRatioDb);

    if (factor == 1)
      aliasDb1 = aliasRatioDb;
    else
      EXPECT_TRUE(aliasRatioDb < prevAlias - 3.0 || aliasRatioDb < -110.0)
          << "factor " << factor << " did not improve on factor " << factor / 2;
    prevAlias = aliasRatioDb;
  }
  EXPECT_LT(prevAlias, aliasDb1 - 20.0) << "factor 8 should cut aliasing by >20 dB";
}

TEST(ChainOversamplerTest, PhaseInterleavedDilatedConvIsExact) {
  // The math NamEngine's oversampled mode rests on: N phase copies of a
  // dilated convolution at the native rate == one dilation-scaled
  // convolution at the oversampled rate. Exact, not approximate.
  const int factorN = 4, dilation = 3, taps = 4, len = 4096;
  std::vector<double> x(len), kernel = {0.6, -0.3, 0.2, 0.15};
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  for (auto& s : x)
    s = dist(rng);

  std::vector<double> highRate(len, 0.0), phased(len, 0.0);
  for (int i = 0; i < len; ++i)
    for (int t = 0; t < taps; ++t) {
      const int j = i - t * dilation * factorN;
      if (j >= 0)
        highRate[static_cast<size_t>(i)] +=
            kernel[static_cast<size_t>(t)] * x[static_cast<size_t>(j)];
    }
  for (int p = 0; p < factorN; ++p)
    for (int i = p; i < len; i += factorN)
      for (int t = 0; t < taps; ++t) {
        const int j = i - t * dilation * factorN;
        if (j >= 0)
          phased[static_cast<size_t>(i)] +=
              kernel[static_cast<size_t>(t)] * x[static_cast<size_t>(j)];
      }

  for (int i = 0; i < len; ++i)
    ASSERT_EQ(highRate[static_cast<size_t>(i)], phased[static_cast<size_t>(i)]) << "at " << i;
}

TEST(ChainOversamplerTest, IslandFactor1IsBitExact) {
  const int baseBlock = 512, total = 93 * 512;
  auto in = makeNoise(total, 99);
  const auto ref = in;
  std::vector<float> right(in.size(), 0.0f);

  ChainOversampler island;
  island.prepare(1, baseBlock);
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(baseBlock)) {
    float* chans[2] = {in.data() + off, right.data() + off};
    island.processBaseRateIsland(chans, 2, baseBlock, [](float* const*, int) {});
  }
  EXPECT_EQ(in, ref);
}

TEST(ChainOversamplerTest, IslandFrameCountsAndSignalPath) {
  // The inner function must see exactly osFrames/factor base frames, and
  // whatever it does must reach the output (marker gain ×2 → +6.02 dB).
  const int baseBlock = 512;
  const double f0 = 997.0;
  for (int factor : {2, 4, 8}) {
    const int osBlock = baseBlock * factor;
    const int osTotal = 93 * osBlock;
    auto sig = makeSine(osTotal, f0);
    std::vector<float> right(sig.size(), 0.0f);

    ChainOversampler island;
    island.prepare(factor, baseBlock);
    bool framesOk = true;
    int calls = 0;
    for (int off = 0; off < osTotal; off += osBlock) {
      float* chans[2] = {sig.data() + off, right.data() + off};
      island.processBaseRateIsland(chans, 2, osBlock, [&](float* const* ch, int n) {
        framesOk = framesOk && n == osBlock / factor;
        ++calls;
        for (int i = 0; i < n; ++i)
          ch[0][i] *= 2.0f;
      });
    }
    EXPECT_TRUE(framesOk) << "factor " << factor;
    EXPECT_EQ(calls, osTotal / osBlock) << "factor " << factor;

    std::vector<float> tail(sig.begin() + 8192, sig.begin() + 8192 + 16384);
    const double gainDb = db(goertzelPower(tail, f0));
    EXPECT_NEAR(gainDb, 20.0 * std::log10(2.0), 0.1) << "factor " << factor;
  }
}

TEST(ChainOversamplerTest, IslandHandlesMonoBuffers) {
  const int baseBlock = 512;
  ChainOversampler island;
  island.prepare(8, baseBlock);
  std::vector<float> mono(static_cast<size_t>(baseBlock * 8), 0.5f);
  float* chans[1] = {mono.data()};
  island.processBaseRateIsland(chans, 1, baseBlock * 8, [](float* const*, int) {});
  for (float s : mono)
    ASSERT_TRUE(std::isfinite(s));
}

// ═════════════════════════ NamEngine (real A2 models) ═════════════════════════

// Builds phase instances from a .nam file exactly the way the model loader
// does (nam::get_dsp per phase, all from the same parsed config).
std::unique_ptr<NamEngine> makeNamEngine(const juce::File& modelFile, int oversampleFactor,
                                         int instanceCount) {
  juce::MemoryBlock bytes;
  EXPECT_TRUE(modelFile.loadFileAsData(bytes));
  const auto* begin = static_cast<const char*>(bytes.getData());
  const nlohmann::json config = nlohmann::json::parse(begin, begin + bytes.getSize());

  std::vector<std::unique_ptr<nam::DSP>> instances;
  for (int i = 0; i < instanceCount; ++i) {
    auto dsp = nam::get_dsp(config);
    EXPECT_NE(dsp, nullptr);
    instances.push_back(std::move(dsp));
  }
  return std::make_unique<NamEngine>(std::move(instances), oversampleFactor);
}

struct NamModelParam {
  const char* file;
  const char* label;
};

class NamEngineTest : public ::testing::TestWithParam<NamModelParam> {};

INSTANTIATE_TEST_SUITE_P(Models, NamEngineTest,
                         ::testing::Values(NamModelParam{"a2-amp-cab-test.nam", "AmpCab"},
                                           NamModelParam{"a2-amp-test.nam", "AmpHead"}),
                         [](const auto& info) { return info.param.label; });

TEST_P(NamEngineTest, LoadsAndProducesAmpedAudio) {
  auto engine = makeNamEngine(testFile(GetParam().file), 1, 1);
  EXPECT_NEAR(engine->getModelSampleRate(), kFs, 0.1) << "A2 captures are 48 kHz";

  engine->prepare(512);
  juce::AudioBuffer<float> buffer(2, 512);
  const auto in = makeSine(512, 220.0, 0.25f);
  buffer.copyFrom(0, 0, in.data(), 512);
  buffer.copyFrom(1, 0, in.data(), 512);
  engine->process(buffer);

  float peak = 0.0f, diff = 0.0f;
  for (int i = 0; i < 512; ++i) {
    const float s = buffer.getSample(0, i);
    ASSERT_TRUE(std::isfinite(s)) << "at " << i;
    peak = std::max(peak, std::abs(s));
    diff = std::max(diff, std::abs(s - in[static_cast<size_t>(i)]));
    // Mono model fans out: both channels identical.
    ASSERT_EQ(s, buffer.getSample(1, i)) << "at " << i;
  }
  EXPECT_GT(peak, 1e-4f) << "model produced silence";
  EXPECT_GT(diff, 1e-4f) << "model passed audio through untouched";
}

TEST_P(NamEngineTest, SlicedProcessingMatchesOneShot) {
  // NAM models stream statefully, so process() slicing (engine prepared
  // smaller than the buffer it receives) must be inaudible — this is the
  // defensive path that keeps blocks alive when a startup prepare raced the
  // host's real block size.
  const int total = 4096;
  const auto in = makeNoise(total, 1234, 0.5f);

  auto oneShot = makeNamEngine(testFile(GetParam().file), 1, 1);
  oneShot->prepare(total);
  juce::AudioBuffer<float> bufferA(1, total);
  bufferA.copyFrom(0, 0, in.data(), total);
  oneShot->process(bufferA);

  auto sliced = makeNamEngine(testFile(GetParam().file), 1, 1);
  sliced->prepare(512);  // forces internal 512-frame slicing of the same 4096
  juce::AudioBuffer<float> bufferB(1, total);
  bufferB.copyFrom(0, 0, in.data(), total);
  sliced->process(bufferB);

  float maxDiff = 0.0f;
  for (int i = 0; i < total; ++i)
    maxDiff = std::max(maxDiff,
                       std::abs(bufferA.getSample(0, i) - bufferB.getSample(0, i)));
  EXPECT_LE(maxDiff, 2e-6f) << "slicing changed the output";
}

TEST_P(NamEngineTest, PhaseInterleaveMatchesManualPhaseReference) {
  // The oversampled engine (N instances fed every Nth sample) must compute
  // exactly what N hand-driven copies of the model compute on hand-split
  // phase streams — this pins the de-interleave/re-interleave bookkeeping.
  const int factor = 4, total = 4096;
  const auto in = makeNoise(total, 4321, 0.5f);

  auto engine = makeNamEngine(testFile(GetParam().file), factor, factor);
  engine->prepare(total);
  juce::AudioBuffer<float> buffer(1, total);
  buffer.copyFrom(0, 0, in.data(), total);
  engine->process(buffer);

  // Reference: fresh instances, same prewarm geometry as NamEngine::prepare
  // (per-phase capacity total/factor + 1 at the base rate).
  juce::MemoryBlock bytes;
  ASSERT_TRUE(testFile(GetParam().file).loadFileAsData(bytes));
  const auto* begin = static_cast<const char*>(bytes.getData());
  const nlohmann::json config = nlohmann::json::parse(begin, begin + bytes.getSize());

  const int perPhase = total / factor;
  std::vector<float> reference(static_cast<size_t>(total), 0.0f);
  for (int p = 0; p < factor; ++p) {
    auto dsp = nam::get_dsp(config);
    ASSERT_NE(dsp, nullptr);
    dsp->ResetAndPrewarm(kFs, perPhase + 1);

    std::vector<double> phaseIn(static_cast<size_t>(perPhase)),
        phaseOut(static_cast<size_t>(perPhase));
    for (int i = 0; i < perPhase; ++i)
      phaseIn[static_cast<size_t>(i)] = static_cast<double>(in[static_cast<size_t>(i * factor + p)]);
    NAM_SAMPLE* ins[] = {phaseIn.data()};
    NAM_SAMPLE* outs[] = {phaseOut.data()};
    dsp->process(ins, outs, perPhase);
    for (int i = 0; i < perPhase; ++i)
      reference[static_cast<size_t>(i * factor + p)] =
          static_cast<float>(phaseOut[static_cast<size_t>(i)]);
  }

  for (int i = 0; i < total; ++i)
    ASSERT_EQ(buffer.getSample(0, i), reference[static_cast<size_t>(i)]) << "at " << i;
}

TEST(NamEngineAliasingTest, OversamplingReducesRealAmpAliasing) {
  // The headline claim, end to end with a real amp capture (the head-only
  // model — a cab's rolloff would bury the measurement): drive it with a hot
  // ~5 kHz tone and compare the energy at the fold-back frequencies of
  // harmonics 5..15 between factor 1 and factor 8.
  const juce::File model = testFile("a2-amp-test.nam");
  const int blockSize = 512, total = 93 * 512;
  const double f0 = 4998.046875;
  const auto in = makeSine(total, f0, 0.8f);

  std::vector<double> aliasFreqs;
  for (int h = 5; h <= 15; ++h)  // an amp makes even + odd harmonics
    aliasFreqs.push_back(foldFrequency(h * f0));

  auto aliasRatioAtFactor = [&](int factor) {
    auto engine = makeNamEngine(model, factor, factor);
    engine->prepare(blockSize * factor);
    const auto out = runOversampledChain(in, factor, blockSize, [&](float** ch, int n) {
      juce::AudioBuffer<float> block(ch, 2, n);
      engine->process(block);
    });
    std::vector<float> tail(out.begin() + 8192, out.begin() + 8192 + 16384);
    double aliasPower = 0.0;
    for (double f : aliasFreqs)
      aliasPower += goertzelPower(tail, f);
    return db(aliasPower) - db(goertzelPower(tail, f0));
  };

  const double alias1 = aliasRatioAtFactor(1);
  const double alias8 = aliasRatioAtFactor(8);
  std::printf("  real amp: folded harmonics vs fundamental — factor 1: %+.1f dB, factor 8: "
              "%+.1f dB (%.1f dB reduction)\n",
              alias1, alias8, alias1 - alias8);
  EXPECT_LT(alias8, alias1 - 10.0) << "8x oversampling should cut real-amp aliasing by >10 dB";
}

// ═════════════════════════ IR convolution (real IR files) ═════════════════════════

// Loads an IR the way the model loader does: Trim::yes, Normalise::no,
// engine picked by the same 1 s short/long cutoff, prepared at the base rate.
std::unique_ptr<juce::dsp::Convolution> makeConvolver(const juce::File& irFile,
                                                      juce::dsp::Convolution::Stereo stereo,
                                                      int baseBlockSize) {
  constexpr int kShortIrMaxBaseSamples = 48000;    // 1 s at 48 kHz
  constexpr int kIrNonUniformHeadSamples = 8192;   // same as the loader

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));
  EXPECT_NE(reader, nullptr);
  const bool longIr = reader != nullptr &&
                      reader->lengthInSamples * kFs / reader->sampleRate > kShortIrMaxBaseSamples;

  auto convolver = longIr
                       ? std::make_unique<juce::dsp::Convolution>(
                             juce::dsp::Convolution::NonUniform{kIrNonUniformHeadSamples})
                       : std::make_unique<juce::dsp::Convolution>();
  convolver->loadImpulseResponse(irFile, stereo, juce::dsp::Convolution::Trim::yes, 0,
                                 juce::dsp::Convolution::Normalise::no);
  convolver->prepare({kFs, static_cast<juce::uint32>(baseBlockSize), 2});
  return convolver;
}

TEST(IrConvolutionTest, CabClassifiesShortReverbsClassifyLong) {
  // The 1 s cutoff drives the −18 dB cab pad and the default mix — a
  // misclassification is instantly audible. Kernel lengths are read off the
  // built engines, post trim + resample, exactly like the loader.
  auto cab = makeConvolver(testFile("cab-ir-test.wav"), juce::dsp::Convolution::Stereo::no, 512);
  auto reverbMono =
      makeConvolver(testFile("reverb-ir-mono-test.wav"), juce::dsp::Convolution::Stereo::no, 512);
  auto reverbStereo = makeConvolver(testFile("reverb-ir-stereo-test.wav"),
                                    juce::dsp::Convolution::Stereo::yes, 512);

  EXPECT_GT(cab->getCurrentIRSize(), 0);
  EXPECT_LT(cab->getCurrentIRSize(), 48000) << "cab IR must classify short";
  EXPECT_GT(reverbMono->getCurrentIRSize(), 48000) << "mono reverb IR must classify long";
  EXPECT_GT(reverbStereo->getCurrentIRSize(), 48000) << "stereo reverb IR must classify long";
}

TEST(IrConvolutionTest, TrueStereoDecorrelatesMonoFanoutDoesNot) {
  const int blockSize = 512, total = 48000;
  const auto noise = makeNoise(total, 555, 0.25f);

  auto processThrough = [&](juce::dsp::Convolution& convolver) {
    juce::AudioBuffer<float> buffer(2, total);
    buffer.copyFrom(0, 0, noise.data(), total);
    buffer.copyFrom(1, 0, noise.data(), total);
    for (int off = 0; off < total; off += blockSize) {
      juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(), 2,
                                         static_cast<size_t>(off),
                                         static_cast<size_t>(blockSize));
      convolver.process(juce::dsp::ProcessContextReplacing<float>(block));
    }
    return buffer;
  };

  // JUCE crossfades a freshly installed engine in from dry over the first
  // samples (and not symmetrically across channels), so the contract only
  // holds once that settles — in the plugin the block's wet fade-in masks
  // exactly this window. Assert on the settled region.
  const int settled = 8192;

  // Stereo::yes on a true-stereo IR: L and R convolve different kernels, so
  // identical inputs must come out different (and both non-silent).
  auto stereo = makeConvolver(testFile("reverb-ir-stereo-test.wav"),
                              juce::dsp::Convolution::Stereo::yes, blockSize);
  const auto stereoOut = processThrough(*stereo);
  float maxL = 0.0f, maxDiff = 0.0f;
  for (int i = settled; i < total; ++i) {
    maxL = std::max(maxL, std::abs(stereoOut.getSample(0, i)));
    maxDiff = std::max(maxDiff,
                       std::abs(stereoOut.getSample(0, i) - stereoOut.getSample(1, i)));
  }
  EXPECT_GT(maxL, 1e-4f);
  EXPECT_GT(maxDiff, 1e-4f) << "true-stereo IR produced identical channels";

  // Stereo::no on the same file: kernel 0 fans out to every channel — the
  // mono-fallback contract the RT path relies on upstream of NAM blocks.
  // Within float noise, not bit-exact: JUCE's per-channel install-fade gains
  // can settle a ULP apart (≈ −120 dB — meaningless, but not zero).
  auto mono = makeConvolver(testFile("reverb-ir-stereo-test.wav"),
                            juce::dsp::Convolution::Stereo::no, blockSize);
  const auto monoOut = processThrough(*mono);
  for (int i = settled; i < total; ++i)
    ASSERT_NEAR(monoOut.getSample(0, i), monoOut.getSample(1, i), 1e-6f) << "at " << i;
}

TEST(IrConvolutionTest, IslandedConvolutionInOversampledChainMatchesBaseRate) {
  // The island guarantee: an IR block inside the ×8 chain (decimate →
  // convolve at 48 kHz → interpolate) must sound identical to plain base-rate
  // convolution — same kernel, same rate, only the transparent oversampling
  // round trip around it. Compared as magnitude at probe tones (the island's
  // few samples of allpass group delay make a raw time-domain null
  // meaningless, and magnitude is what "sounds identical" means for an LTI
  // path).
  const std::vector<double> probeFreqs = {150.0, 400.0, 1000.0, 2500.0, 6000.0, 10000.0};
  const int blockSize = 512, total = 96000;

  std::vector<float> multitone(static_cast<size_t>(total), 0.0f);
  for (double f : probeFreqs) {
    const auto tone = makeSine(total, f, 0.12f);
    for (int i = 0; i < total; ++i)
      multitone[static_cast<size_t>(i)] += tone[static_cast<size_t>(i)];
  }

  for (const char* irName : {"cab-ir-test.wav", "reverb-ir-mono-test.wav"}) {
    const juce::File irFile = testFile(irName);

    // Path A: plain convolution at the base rate (oversampling off).
    auto convolverA = makeConvolver(irFile, juce::dsp::Convolution::Stereo::no, blockSize);
    juce::AudioBuffer<float> bufferA(2, total);
    bufferA.copyFrom(0, 0, multitone.data(), total);
    bufferA.copyFrom(1, 0, multitone.data(), total);
    for (int off = 0; off < total; off += blockSize) {
      juce::dsp::AudioBlock<float> block(bufferA.getArrayOfWritePointers(), 2,
                                         static_cast<size_t>(off),
                                         static_cast<size_t>(blockSize));
      convolverA->process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // Path B: the same convolver configuration behind an island inside the
    // ×8 oversampled chain — the exact RT-path structure.
    const int factor = 8;
    auto convolverB = makeConvolver(irFile, juce::dsp::Convolution::Stereo::no, blockSize);
    ChainOversampler island;
    island.prepare(factor, blockSize);
    const auto outB = runOversampledChain(multitone, factor, blockSize, [&](float** ch, int n) {
      island.processBaseRateIsland(ch, 2, n, [&](float* const* base, int baseFrames) {
        juce::dsp::AudioBlock<float> block(base, 2, static_cast<size_t>(baseFrames));
        convolverB->process(juce::dsp::ProcessContextReplacing<float>(block));
      });
    });

    // Compare the settled tails at every probe tone.
    const int start = 32768, window = 32768;
    for (double f : probeFreqs) {
      const double gainA = db(goertzelPower(bufferA.getReadPointer(0) + start,
                                            static_cast<size_t>(window), f));
      const double gainB =
          db(goertzelPower(outB.data() + start, static_cast<size_t>(window), f));
      EXPECT_NEAR(gainB, gainA, 0.15)
          << irName << " at " << f << " Hz: islanded IR deviates from base-rate IR";
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  // JUCE convolution/format readers want the usual JUCE runtime scaffolding.
  juce::ScopedJuceInitialiser_GUI juceInit;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
