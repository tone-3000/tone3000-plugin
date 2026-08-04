#pragma once
// Chain-level test rig shared by branch_tests.cpp and duplicate_tests.cpp:
// a processor with state-restore access, block-tree builders with model
// bytes embedded (cache-first loads, no network), and stereo drive/compare
// helpers for asserting on the audible output.

#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

// Tests describe a rig as a bare ChainSnapshot tree (lanes + stereo/branch
// properties). restoreFromTree wraps it in a TONE3000State root and frames it
// exactly like getStateInformation does (T3KB magic + binary ValueTree
// stream), so restoring through it pins the real state format.
struct ChainTestProcessor : TONE3000Processor {
  void restoreFromTree(const juce::ValueTree& snapshot) {
    juce::ValueTree state("TONE3000State");
    state.setProperty("schemaVersion", 1, nullptr);
    state.appendChild(snapshot.createCopy(), nullptr);

    juce::MemoryBlock data;
    juce::MemoryOutputStream out(data, false);
    out.write("T3KB", 4);
    state.writeToStream(out);
    setStateInformation(data.getData(), static_cast<int>(data.getSize()));
  }
};

// An IR block tree in plugin-state shape, with the IR file's bytes embedded
// as its ModelCache so the background loader never needs the (fake) URL.
inline juce::ValueTree makeIrBlockTree(const juce::String& blockId, int toneId, int modelId,
                                       const char* fileName = "cab-ir-test.wav") {
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

  juce::MemoryBlock bytes;
  EXPECT_TRUE(testFile(fileName).loadFileAsData(bytes));
  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(bytes), nullptr);
  juce::ValueTree cache("ModelCache");
  cache.appendChild(cached, nullptr);
  block.appendChild(cache, nullptr);
  return block;
}

// A NAM block tree in plugin-state shape, with the amp capture's bytes
// embedded as its ModelCache (same cache-first loading as makeIrBlockTree).
inline juce::ValueTree makeNamBlockTree(const juce::String& blockId, int toneId, int modelId) {
  const juce::String toneJson =
      "{\"id\":" + juce::String(toneId) + ",\"title\":\"Test Amp\",\"format\":\"nam\","
      "\"models\":[{\"id\":" + juce::String(modelId) +
      ",\"name\":\"amp\",\"model_url\":\"https://test.invalid/amp.nam\"}]}";

  juce::ValueTree block("ChainBlock");
  block.setProperty("id", blockId, nullptr);
  block.setProperty("type", "nam", nullptr);
  block.setProperty("enabled", true, nullptr);
  block.setProperty("normalize", true, nullptr);
  block.setProperty("inputGain", 0.5f, nullptr);
  block.setProperty("outputGain", 0.5f, nullptr);
  block.setProperty("mix", 1.0f, nullptr);
  block.setProperty("toneId", toneId, nullptr);
  block.setProperty("toneJson", toneJson, nullptr);
  block.setProperty("activeModelId", modelId, nullptr);

  juce::MemoryBlock bytes;
  EXPECT_TRUE(testFile("a2-amp-test.nam").loadFileAsData(bytes));
  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(bytes), nullptr);
  juce::ValueTree cache("ModelCache");
  cache.appendChild(cached, nullptr);
  block.appendChild(cache, nullptr);
  return block;
}

// Seed stereo mode with the given IR blocks per lane (ids only; tones/models
// are synthesized). Restores through the real state path, so lanes come back
// normalized (insert slots padded) and loads are queued cache-first.
inline void seedStereoChains(ChainTestProcessor& proc, const std::vector<juce::String>& leftIds,
                             const std::vector<juce::String>& rightIds) {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);

  int toneId = 1, modelId = 100;
  juce::ValueTree left("ChainBlocks");
  for (const auto& id : leftIds)
    left.appendChild(makeIrBlockTree(id, toneId++, modelId++), nullptr);
  state.appendChild(left, nullptr);

  juce::ValueTree right("RightChainBlocks");
  for (const auto& id : rightIds)
    right.appendChild(makeIrBlockTree(id, toneId++, modelId++), nullptr);
  state.appendChild(right, nullptr);

  proc.restoreFromTree(state);
}

// Wait until every tone block in both lanes reports loaded AND the chain-edit
// mute has released. Restores hold the mute until their loads settle (plus a
// grace period) on a *wall-clock* waiter; tests pump audio much faster than
// realtime, so without this second condition a test can burn through seconds
// of "audio" while the rig is still (correctly) muted.
inline bool waitForChainLoaded(TONE3000Processor& proc, int timeoutMs = 20000) {
  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
  while (juce::Time::getMillisecondCounter() < deadline) {
    const juce::var state = proc.getChainState(-1);
    bool allLoaded = true;
    for (const auto* lane : {state["chain"].getArray(), state["chainRight"].getArray()}) {
      if (lane == nullptr)
        continue;
      for (const auto& item : *lane)
        if (item["kind"].toString() == "tone" && !static_cast<bool>(item["loaded"]))
          allLoaded = false;
    }
    if (allLoaded && !proc.isChainEditFadeHeld())
      return true;
    juce::Thread::sleep(20);
  }
  return false;
}

// Make isAudioActive() false so the next mutation skips its (bounded) fade
// wait, keeping the tests deterministic and fast.
inline void letAudioGoIdle() { juce::Thread::sleep(200); }

// Drives the processor like a host with identical audio on both channels;
// returns both output channels.
inline std::pair<std::vector<float>, std::vector<float>>
processStereo(TONE3000Processor& proc, const std::vector<float>& in, int blockSize = 512) {
  const int total = static_cast<int>(in.size());
  std::vector<float> outL(in.size(), 0.0f), outR(in.size(), 0.0f);
  juce::AudioBuffer<float> buffer(2, blockSize);
  juce::MidiBuffer midi;
  for (int off = 0; off + blockSize <= total; off += blockSize) {
    buffer.copyFrom(0, 0, in.data() + off, blockSize);
    buffer.copyFrom(1, 0, in.data() + off, blockSize);
    proc.processBlock(buffer, midi);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize,
              outL.begin() + off);
    std::copy(buffer.getReadPointer(1), buffer.getReadPointer(1) + blockSize,
              outR.begin() + off);
  }
  return {std::move(outL), std::move(outR)};
}

// Largest |L - R| over the settled region (skips the first second: block
// wet-fades, smoothers and per-channel filter states converging).
inline float settledMaxChannelDiff(const std::vector<float>& l, const std::vector<float>& r,
                                   size_t skip = 48000) {
  float maxDiff = 0.0f;
  for (size_t i = skip; i < l.size(); ++i)
    maxDiff = std::max(maxDiff, std::abs(l[i] - r[i]));
  return maxDiff;
}
