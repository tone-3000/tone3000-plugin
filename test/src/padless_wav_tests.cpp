// Pad-less WAVs
//
// Some catalog IR WAVs end in an odd-sized data chunk but omit the trailing
// pad byte the RIFF spec requires (their header's RIFF size counts it, the
// file doesn't ship it). JUCE 9.0.1's WAV reader rejected the whole data
// chunk over that one byte, so the file read as zero samples and short cab
// IRs silently degraded to a dry passthrough; the loader used to append the
// byte itself. JUCE 9.0.2 reads such files as every other program does, so
// the loader hands them straight to JUCE, and these tests pin that a
// pad-less IR keeps loading through both real paths:
//
//   - the model-cache path (downloads, presets, embedded DAW state), where
//     it must sound identical to its well-formed twin through the chain,
//   - drop-time validation (loadLocalTone).
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr int kBlock = 512;

// 24-bit mono 48 kHz PCM WAV with an odd-sized data chunk, shaped like the
// catalog files: the RIFF size always counts the data chunk's pad byte;
// `includePadByte` controls whether the byte itself is present.
std::vector<uint8_t> makeOddChunkWav(const std::vector<float>& samples, bool includePadByte) {
  const uint32_t dataSize = static_cast<uint32_t>(samples.size()) * 3;
  EXPECT_EQ(dataSize % 2u, 1u) << "test needs an odd-sized data chunk";
  const uint32_t riffSize = 36 + dataSize + (dataSize % 2u);

  std::vector<uint8_t> bytes;
  auto push32 = [&bytes](uint32_t v) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
  };
  auto push16 = [&bytes](uint16_t v) {
    bytes.push_back(static_cast<uint8_t>(v & 0xff));
    bytes.push_back(static_cast<uint8_t>(v >> 8));
  };
  auto pushTag = [&bytes](const char* tag) {
    bytes.insert(bytes.end(), tag, tag + 4);
  };

  pushTag("RIFF");
  push32(riffSize);
  pushTag("WAVE");
  pushTag("fmt ");
  push32(16);
  push16(1);       // PCM
  push16(1);       // mono
  push32(48000);
  push32(48000 * 3);  // byte rate
  push16(3);       // block align
  push16(24);      // bits per sample
  pushTag("data");
  push32(dataSize);
  for (const float s : samples) {
    const auto v = static_cast<int32_t>(juce::jlimit(-1.0f, 1.0f, s) * 8388607.0f);
    bytes.push_back(static_cast<uint8_t>(v & 0xff));
    bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    bytes.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  }
  if (includePadByte)
    bytes.push_back(0);
  return bytes;
}

// Noise-like decaying cab kernel with an odd sample count (odd * 3 bytes =
// odd data chunk). Deliberately no dominant first tap, so a convolved
// output is clearly distinguishable from a dry passthrough.
std::vector<float> makeKernel(int count = 1413) {
  EXPECT_EQ(count % 2, 1);
  std::vector<float> h(static_cast<size_t>(count));
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (int i = 0; i < count; ++i)
    h[static_cast<size_t>(i)] = 0.7f * dist(rng) * std::exp(-i / 400.0f);
  return h;
}

// An IR block in plugin-state shape with the given WAV bytes embedded as its
// ModelCache (same shape as makeIrBlockTree, but from in-memory bytes).
juce::ValueTree irBlockFromBytes(const juce::String& blockId, int toneId, int modelId,
                                 const std::vector<uint8_t>& wavBytes) {
  const juce::String toneJson =
      "{\"id\":" + juce::String(toneId) + ",\"title\":\"Test IR\",\"format\":\"ir\","
      "\"models\":[{\"id\":" + juce::String(modelId) +
      ",\"name\":\"cab\",\"model_url\":\"https://test.invalid/cab.wav\"}]}";

  juce::ValueTree block("ChainBlock");
  block.setProperty("id", blockId, nullptr);
  block.setProperty("type", "ir", nullptr);
  block.setProperty("enabled", true, nullptr);
  block.setProperty("normalize", true, nullptr);
  block.setProperty("inputGain", 0.5f, nullptr);
  block.setProperty("outputGain", 0.5f, nullptr);
  block.setProperty("mix", 1.0f, nullptr);
  block.setProperty("toneId", toneId, nullptr);
  block.setProperty("toneJson", toneJson, nullptr);
  block.setProperty("activeModelId", modelId, nullptr);

  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(juce::MemoryBlock(wavBytes.data(), wavBytes.size())),
                     nullptr);
  juce::ValueTree cache("ModelCache");
  cache.appendChild(cached, nullptr);
  block.appendChild(cache, nullptr);
  return block;
}

}  // namespace

// The model-cache load path (downloads, presets, embedded DAW state): a
// pad-less IR must load and convolve identically to its well-formed twin.
// Left lane gets the pad-less bytes, right lane the well-formed ones; with
// identical input on both channels the outputs must match. A reader that
// rejected the pad-less file would leave the left lane an empty kernel
// passing (padded) dry signal.
TEST(PadlessWavTest, PadlessIrMatchesWellFormedTwinThroughChain) {
  const auto kernel = makeKernel();

  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(irBlockFromBytes("blk-padless", 1, 100, makeOddChunkWav(kernel, false)),
                   nullptr);
  state.appendChild(left, nullptr);
  juce::ValueTree right("RightChainBlocks");
  right.appendChild(irBlockFromBytes("blk-wellformed", 2, 200, makeOddChunkWav(kernel, true)),
                    nullptr);
  state.appendChild(right, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc)) << "IR blocks never finished loading from cache";

  const auto in = makeNoise(240 * kBlock, 1234, 0.25f);
  const auto [l, r] = processStereo(proc, in);

  // The pad-less lane is indistinguishable from the well-formed one.
  EXPECT_LT(settledMaxChannelDiff(l, r), 1e-4f);

  // And both actually convolved: the kernel has no dominant first tap, so a
  // (scaled) passthrough would still correlate strongly with the input.
  double num = 0.0, den1 = 0.0, den2 = 0.0;
  for (size_t i = 48000; i < in.size(); ++i) {
    num += static_cast<double>(r[i]) * static_cast<double>(in[i]);
    den1 += static_cast<double>(r[i]) * static_cast<double>(r[i]);
    den2 += static_cast<double>(in[i]) * static_cast<double>(in[i]);
  }
  ASSERT_GT(den1, 0.0) << "chain output is silent";
  EXPECT_LT(std::abs(num) / std::sqrt(den1 * den2), 0.5)
      << "output correlates with the dry input: the IR did not convolve";
}

// Drop-time validation (loadLocalTone): the same pad-less file must be
// accepted, stashed, and load through the normal pipeline.
TEST(PadlessWavTest, PadlessWavSurvivesDropValidationAndLoads) {
  const auto bytes = makeOddChunkWav(makeKernel(), false);

  juce::DynamicObject::Ptr entry = new juce::DynamicObject();
  entry->setProperty("name", "padless-cab.wav");
  entry->setProperty("data", juce::Base64::toBase64(bytes.data(), bytes.size()));

  TONE3000Processor proc;
  const juce::var res =
      proc.loadLocalTone("padless-cab", juce::var(juce::Array<juce::var>{juce::var(entry.get())}));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(res["blockId"].toString().isNotEmpty());
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var state = proc.getChainState(-1);
  const auto* lane = state["chain"].getArray();
  ASSERT_NE(lane, nullptr);
  for (const auto& item : *lane) {
    if (item["kind"].toString() != "tone")
      continue;
    EXPECT_EQ(item["tone"]["format"].toString(), juce::String("ir"));
    EXPECT_TRUE(static_cast<bool>(item["loaded"]));
    return;
  }
  FAIL() << "no tone block after drop";
}
