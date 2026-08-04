// Engine-swap fade tests
//
// Switching a block's model must never expose the block's *dry input*. At
// 100% mix an IR block's dry input is the signal upstream of the cab (the
// raw amp head: un-cabbed, spectrally flat, and ~18 dB hotter than the
// cab-padded wet), so a bypass-shaped swap fade (wet mix gliding to 0 =
// output crossfading toward dry) blasts a short loud burst of it on every
// switch, cached or not. Engine swaps therefore mute the wet term in place
// (ChainBlock::swapWetMuteGain): the old engine dips to silence, engines
// swap, the new one fades in from silence, and the dry share of the user's
// mix holds steady throughout. The loader additionally elapses JUCE's
// internal engine-install crossfade (which also starts from dry) on silence
// before an engine goes live; see prepareBlockModelOffThread.
//
// This pins the contract end-to-end through the real processor: during a
// live cache-first model switch on a full-wet IR block, the output level
// never rises above the settled wet level. The dry input (the driven sine)
// is far hotter than the cab-processed wet, so any dry leak fails loudly.
//
// The rig (ChainTestProcessor, block-tree builders with embedded model
// bytes) is shared with branch_tests.cpp via chain_test_helpers.h; loads
// are cache-first and never touch the network.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace {

constexpr int kBlock = 512;

juce::var makeModelVar(int modelId, const juce::String& name) {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("id", modelId);
  obj->setProperty("name", name);
  obj->setProperty("model_url", "https://test.invalid/" + name + ".wav");
  return juce::var(obj);
}

// Appends `fileName`'s bytes to the block tree's ModelCache under `modelId`,
// so a later switchModel to that id is a pure cached load.
void cacheExtraModel(juce::ValueTree& block, int modelId, const char* fileName) {
  juce::MemoryBlock bytes;
  ASSERT_TRUE(testFile(fileName).loadFileAsData(bytes));
  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(bytes), nullptr);
  block.getChildWithName("ModelCache").appendChild(cached, nullptr);
}

// The state of blockId's tone entry in getChainState, or a void var.
juce::var toneItemState(TONE3000Processor& proc, const juce::String& blockId) {
  const juce::var state = proc.getChainState(-1);
  if (const auto* chain = state["chain"].getArray())
    for (const auto& item : *chain)
      if (item["kind"].toString() == "tone" && item["blockId"].toString() == blockId)
        return item;
  return {};
}

TEST(SwapFadeTest, LiveIrModelSwitchNeverExposesDryInput) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  // One IR block at 100% mix (the worst case: the output is all wet, so any
  // fade through bypass is 100% dry), with a second model's bytes
  // pre-cached so the switch below never touches the network.
  juce::ValueTree block = makeIrBlockTree("blk", 1, 100);
  cacheExtraModel(block, 101, "cab-ir-test.wav");
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(block, nullptr);
  state.appendChild(lane, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));

  // A seamlessly loopable drive signal: 250 Hz is 192 samples per cycle,
  // and 93*512 frames = 248 whole cycles; no splice transient when the
  // buffer recycles. The sine's amplitude (the block's dry input level) is
  // far above the cab-processed wet (unit-energy IR into a -18 dB pad).
  const int loopFrames = 93 * kBlock;
  const auto sine = makeSine(loopFrames, 250.0, 0.4f);

  juce::AudioBuffer<float> buffer(2, kBlock);
  juce::MidiBuffer midi;
  int offset = 0;
  auto processOneBlock = [&]() -> float {
    buffer.copyFrom(0, 0, sine.data() + offset, kBlock);
    buffer.copyFrom(1, 0, sine.data() + offset, kBlock);
    offset = (offset + kBlock) % loopFrames;
    proc.processBlock(buffer, midi);
    return buffer.getMagnitude(0, 0, kBlock);
  };

  // Settle (restore fade-in, smoothers, convolver), then take the wet
  // reference level over one second.
  const int blocksPerSecond = static_cast<int>(kFs) / kBlock;
  for (int i = 0; i < 2 * blocksPerSecond; ++i)
    processOneBlock();
  float steadyPeak = 0.0f;
  for (int i = 0; i < blocksPerSecond; ++i)
    steadyPeak = std::max(steadyPeak, processOneBlock());
  ASSERT_GT(steadyPeak, 1e-4f) << "IR block should be audibly processing";

  // Live switch to the cached model while audio keeps running, tracking the
  // peak until the background swap completes plus a second of tail (covers
  // the fade-in and then some).
  ASSERT_TRUE(proc.switchModel("blk", 101, makeModelVar(101, "cab2")));

  float maxDuringSwitch = 0.0f;
  bool switched = false;
  const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 15000;
  while (!switched && juce::Time::getMillisecondCounter() < deadline) {
    for (int i = 0; i < 8; ++i)
      maxDuringSwitch = std::max(maxDuringSwitch, processOneBlock());
    const juce::var item = toneItemState(proc, "blk");
    switched = !static_cast<bool>(item["modelLoading"]) && static_cast<bool>(item["loaded"]) &&
               static_cast<int>(item["activeModelId"]) == 101;
  }
  ASSERT_TRUE(switched) << "cached model switch did not complete";
  for (int i = 0; i < blocksPerSecond; ++i)
    maxDuringSwitch = std::max(maxDuringSwitch, processOneBlock());

  // The swap may dip the wet path to silence, but must never rise above the
  // settled wet level; with a bypass-shaped fade the dry sine (several
  // times hotter than the cab output) leaks through and trips this.
  EXPECT_LE(maxDuringSwitch, steadyPeak * 1.25f + 1e-3f)
      << "model switch leaked the block's dry input (un-cabbed signal burst)";
}

TEST(SwapFadeTest, UndoRestoreHoldsChainMuteUntilModelsReload) {
  // Preset loads and undo/redo replace the whole chain via
  // restoreChainSnapshot, which queues every changed block's engine rebuild
  // on the background loader. The chain-edit mute must hold until those
  // loads land (releaseChainEditFadeWhenLoadsSettle): releasing it at
  // splice time fades the chain back in on unloaded pass-through blocks,
  // blasting the raw dry input for the whole rebuild window, the "preset
  // switch pop". Undoing a model switch exercises the exact same restore
  // path presets use, cache-first, so it pins the contract without preset
  // files or network.
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  juce::ValueTree block = makeIrBlockTree("blk", 1, 100);
  cacheExtraModel(block, 101, "cab-ir-test.wav");
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(block, nullptr);
  state.appendChild(lane, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));

  // Host-style render thread: the fade handshakes only engage while audio
  // callbacks are live, and undoChain blocks its caller waiting on them,
  // so audio must keep running on its own thread, exactly like in a host.
  std::atomic<bool> stop{false};
  std::atomic<bool> track{false};
  std::atomic<float> trackedMax{0.0f};
  std::thread pump([&] {
    const int loopFrames = 93 * kBlock;  // 248 whole 250 Hz cycles: seamless loop
    const auto sine = makeSine(loopFrames, 250.0, 0.4f);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    int offset = 0;
    while (!stop.load()) {
      buffer.copyFrom(0, 0, sine.data() + offset, kBlock);
      buffer.copyFrom(1, 0, sine.data() + offset, kBlock);
      offset = (offset + kBlock) % loopFrames;
      proc.processBlock(buffer, midi);
      if (track.load()) {
        const float peak = buffer.getMagnitude(0, 0, kBlock);
        float cur = trackedMax.load();
        while (peak > cur && !trackedMax.compare_exchange_weak(cur, peak)) {
        }
      }
      juce::Thread::sleep(1);  // ~10x real-time; keeps the mutex uncontended
    }
  });

  auto waitForActiveModel = [&](int modelId) {
    const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 15000;
    while (juce::Time::getMillisecondCounter() < deadline) {
      const juce::var item = toneItemState(proc, "blk");
      if (!static_cast<bool>(item["modelLoading"]) && static_cast<bool>(item["loaded"]) &&
          static_cast<int>(item["activeModelId"]) == modelId)
        return true;
      juce::Thread::sleep(10);
    }
    return false;
  };

  // Settle (restore fade-in, smoothers), then take the wet reference level.
  juce::Thread::sleep(300);
  track.store(true);
  juce::Thread::sleep(200);
  const float steadyPeak = trackedMax.load();
  ASSERT_GT(steadyPeak, 1e-4f) << "IR block should be audibly processing";

  // A cached model switch creates the undo entry (same IR bytes, so the
  // reference level carries over).
  ASSERT_TRUE(proc.switchModel("blk", 101, makeModelVar(101, "cab2")));
  ASSERT_TRUE(waitForActiveModel(101)) << "cached model switch did not complete";

  // Undo while audio runs: the whole-chain restore reloads model 100 from
  // the block's in-memory cache. Track the peak through the mute, the
  // reload, and a generous glide-in tail.
  trackedMax.store(0.0f);
  ASSERT_TRUE(proc.undoChain());
  ASSERT_TRUE(waitForActiveModel(100)) << "undo reload did not complete";
  juce::Thread::sleep(400);
  stop.store(true);
  pump.join();

  // The transition may dip to silence but must never rise above the settled
  // wet level; an early mute release plays the raw dry sine (several times
  // hotter than the cab output) while the engine rebuilds.
  EXPECT_LE(trackedMax.load(), steadyPeak * 1.25f + 1e-3f)
      << "chain restore leaked dry input while models reloaded (preset-switch pop)";
}

}  // namespace
