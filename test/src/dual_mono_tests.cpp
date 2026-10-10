// Dual mono tests
//
// Input mode Dual Mono (Processor.h, InputMode): a mono chain fed by a
// stereo source runs each channel separately through its own copy of the
// chain, as if the same blocks sat in both stereo lanes. The contracts:
//
//   - the two outputs are the chain applied to each take on its own: L is
//     bit-identical to a Left-mode run on the left take, R to a Right-mode
//     run on the right take, and the whole thing to the same blocks
//     duplicated into two stereo lanes,
//   - a stereo IR convolves its *left* kernel on both sides (as inside a
//     stereo-mode lane), so a silent take stays silent,
//   - the mode is inert wherever it can't engage (mono host bus, mono rig,
//     stereo chains) and behaves exactly as Stereo there,
//   - Spread is idle while it runs; Balance tilts the two voices,
//   - the NAM engines rebuild with two voices when the mode engages and
//     back to one when it leaves, including across the stereo-mode toggle
//     and a load still in flight when the mode changes, with the chain-edit
//     mute held until the rebuilt engines land,
//   - the mode persists as "dual" in the session state (older strings and
//     garbage fall back to Stereo), is excluded from presets like the other
//     input modes, and an active branch forces it to Left,
//   - the Stereo fold on a mono chain is ½(L+R): bit-identical for a mono
//     source on a stereo track (L == R), −6.02 dB for one jack of a stereo
//     pair, and no fold at all with stereo chains,
//   - meters: a block's meter reads the louder take; the main output meter
//     shows the two channels apart; auto balance measures the takes' mismatch.
//
// Chains are seeded through setStateInformation with model bytes embedded
// (ModelCache), so loads are cache-first and never touch the network.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

namespace {

constexpr int kBlock = 512;
using InputMode = TONE3000Processor::InputMode;

// Two unrelated takes at the same level, plus silence.
const std::vector<float>& takeL() {
  static const auto v = makeNoise(240 * kBlock, 1001, 0.1f);
  return v;
}
const std::vector<float>& takeR() {
  static const auto v = makeNoise(240 * kBlock, 2002, 0.1f);
  return v;
}
const std::vector<float>& silence() {
  static const std::vector<float> v(static_cast<size_t>(240 * kBlock), 0.0f);
  return v;
}

// Mono chain [amp, cab] as a snapshot tree. The cab's mix is sub-unity so a
// dry-path mix-up (shared scratch, wrong channel) shows in the blend.
juce::ValueTree makeMonoAmpCabState(const char* irFile = "cab-ir-test.wav") {
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
  auto cab = makeIrBlockTree("blk-cab", 2, 200, irFile);
  cab.setProperty("mix", 0.7f, nullptr);
  lane.appendChild(cab, nullptr);
  state.appendChild(lane, nullptr);
  return state;
}

// The same [amp, cab] in both stereo lanes: what dual mono must sound like.
juce::ValueTree makeDuplicatedStereoState() {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTree("blk-amp-l", 1, 100), nullptr);
  auto cabL = makeIrBlockTree("blk-cab-l", 2, 200);
  cabL.setProperty("mix", 0.7f, nullptr);
  left.appendChild(cabL, nullptr);
  state.appendChild(left, nullptr);
  juce::ValueTree right("RightChainBlocks");
  right.appendChild(makeNamBlockTree("blk-amp-r", 3, 300), nullptr);
  auto cabR = makeIrBlockTree("blk-cab-r", 4, 400);
  cabR.setProperty("mix", 0.7f, nullptr);
  right.appendChild(cabR, nullptr);
  state.appendChild(right, nullptr);
  return state;
}

// A stereo-bus processor with the given rig and input mode, loaded and
// settled (the mode is set *after* the restore: the test state carries no
// inputMode, so a restore resets it to Stereo, as an older project would).
//
// Prepared again once the rig is in place, like a host starting transport
// after a session load. Beyond realism this makes the cross-mode
// comparisons below exact: prepareToPlay primes the post-chain image matrix
// for the *current* chain mode, whereas a stereo rig restored after the
// first prepare ramps the matrix from the mono identity to the stereo pan
// gains over its first 20 ms, and cos(π/2) in float is -4e-8, not 0: a
// ±1 ulp leak of R into L that the tone stack's IIR state then rings out
// over a couple of seconds (≈ -100 dBFS, inaudible, but not zero).
std::unique_ptr<ChainTestProcessor> makeRig(const juce::ValueTree& state, InputMode mode,
                                            int inChannels = 2, int outChannels = 2) {
  auto proc = std::make_unique<ChainTestProcessor>();
  proc->setPlayConfigDetails(inChannels, outChannels, kFs, kBlock);
  proc->prepareToPlay(kFs, kBlock);
  proc->restoreFromTree(state);
  EXPECT_TRUE(waitForChainLoaded(*proc)) << "blocks never finished loading from cache";
  proc->setInputMode(mode);
  EXPECT_TRUE(waitForChainLoaded(*proc)) << "engines never settled after the mode change";
  proc->prepareToPlay(kFs, kBlock);
  return proc;
}

std::pair<std::vector<float>, std::vector<float>> runRig(const juce::ValueTree& state,
                                                         InputMode mode,
                                                         const std::vector<float>& inL,
                                                         const std::vector<float>& inR) {
  auto proc = makeRig(state, mode);
  return processStereoLR(*proc, inL, inR);
}

// Exact after the settle window: the two paths are the same arithmetic.
float settledDiff(const std::vector<float>& a, const std::vector<float>& b) {
  return settledMaxChannelDiff(a, b, 48000);
}

float settledPeak(const std::vector<float>& x, size_t skip = 48000) {
  float peak = 0.0f;
  for (size_t i = skip; i < x.size(); ++i)
    peak = std::max(peak, std::abs(x[i]));
  return peak;
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

// Each take gets the whole chain on its own: L must equal what the chain
// does to the left take alone (Left mode, which folds that take onto both
// channels), R the same for the right take.
TEST(DualMonoTest, EachChannelMatchesItsOwnMonoRun) {
  const auto state = makeMonoAmpCabState();
  const auto [dl, dr] = runRig(state, InputMode::DualMono, takeL(), takeR());
  const auto [ll, lr] = runRig(state, InputMode::Left, takeL(), takeR());
  const auto [rl, rr] = runRig(state, InputMode::Right, takeL(), takeR());

  // Sanity: the two takes really come out different.
  EXPECT_GT(settledDiff(dl, dr), 1e-3f) << "dual mono output should carry two different takes";
  EXPECT_EQ(settledDiff(dl, ll), 0.0f) << "left voice diverged from a mono run on the left take";
  EXPECT_EQ(settledDiff(dr, rr), 0.0f) << "right voice diverged from a mono run on the right take";
}

// The user-facing promise: dual mono is the same blocks in both stereo
// lanes, without building the second lane. Channel for channel identical.
TEST(DualMonoTest, MatchesDuplicatedStereoChains) {
  const auto [dl, dr] = runRig(makeMonoAmpCabState(), InputMode::DualMono, takeL(), takeR());
  const auto [sl, sr] = runRig(makeDuplicatedStereoState(), InputMode::Stereo, takeL(), takeR());

  EXPECT_EQ(settledDiff(dl, sl), 0.0f) << "left diverged from the duplicated left lane";
  EXPECT_EQ(settledDiff(dr, sr), 0.0f) << "right diverged from the duplicated right lane";
}

// A stereo IR inside a mono chain normally runs in true stereo (L ⊗ IR_L,
// R ⊗ IR_R). In dual mono the chain is two mono chains, so like a stereo-mode
// lane it uses the IR's left kernel on both sides: a silent take must stay
// silent, where the Stereo fold and the true-stereo path both light it up.
TEST(DualMonoTest, StereoIrConvolvesLeftKernelPerChannel) {
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree("blk-verb", 1, 100, "reverb-ir-stereo-test.wav"), nullptr);
  state.appendChild(lane, nullptr);

  const auto in = makeNoise(240 * kBlock, 3003, 0.1f);
  const auto [dl, dr] = runRig(state, InputMode::DualMono, in, silence());
  EXPECT_GT(settledPeak(dl), 1e-3f) << "left take should come through";
  EXPECT_EQ(settledPeak(dr), 0.0f) << "a silent take must stay silent in dual mono";

  // Left mode on the same source: true stereo, the IR's right channel
  // decorrelates R from L. Stereo (sum): both channels carry the fold.
  const auto [ll, lr] = runRig(state, InputMode::Left, in, silence());
  EXPECT_GT(settledPeak(lr), 1e-3f) << "true-stereo IR should feed the right channel";
  EXPECT_GT(settledDiff(ll, lr), 1e-3f);
  const auto [sl, sr] = runRig(state, InputMode::Stereo, in, silence());
  EXPECT_GT(settledPeak(sr), 1e-3f) << "the Stereo fold should feed both channels";
}

// ---------------------------------------------------------------------------
// Where the mode can't engage it behaves exactly as Stereo
// ---------------------------------------------------------------------------

// Stereo chains: each lane already has its own channel, so the selection
// changes nothing (and the engines stay single-voice).
TEST(DualMonoTest, InertWithStereoChains) {
  const auto state = makeDuplicatedStereoState();
  auto dual = makeRig(state, InputMode::DualMono);
  EXPECT_FALSE(dual->dualMonoEngaged());
  EXPECT_FALSE(static_cast<bool>(dual->getChainState(-1)["dualMonoActive"]));
  EXPECT_EQ(dual->namEngineVoiceCount("blk-amp-l"), 1);
  EXPECT_EQ(dual->namEngineVoiceCount("blk-amp-r"), 1);
  const auto [dl, dr] = processStereoLR(*dual, takeL(), takeR());

  const auto [sl, sr] = runRig(state, InputMode::Stereo, takeL(), takeR());
  EXPECT_EQ(settledDiff(dl, sl), 0.0f);
  EXPECT_EQ(settledDiff(dr, sr), 0.0f);
}

// Mono rig (stereo source, one output channel): nothing could reproduce the
// second voice, so the input folds to the Stereo sum and one voice runs.
TEST(DualMonoTest, InertOnMonoRigFoldsLikeStereo) {
  const auto state = makeMonoAmpCabState();
  auto dual = makeRig(state, InputMode::DualMono, 2, 1);
  EXPECT_FALSE(dual->dualMonoEngaged());
  EXPECT_EQ(dual->namEngineVoiceCount("blk-amp"), 2)
      << "the voice count follows the selection, not the rig (see wantedNamVoices)";
  const auto [dl, dr] = processStereoLR(*dual, takeL(), takeR());
  EXPECT_EQ(settledDiff(dl, dr), 0.0f) << "a mono rig should hear one summed signal";

  auto sum = makeRig(state, InputMode::Stereo, 2, 1);
  const auto [sl, sr] = processStereoLR(*sum, takeL(), takeR());
  EXPECT_EQ(settledDiff(dl, sl), 0.0f) << "dual mono on a mono rig must be the Stereo fold";
}

// Mono host bus: a one-channel buffer has no second take; the mode is a
// no-op and the chain runs as plain mono.
TEST(DualMonoTest, InertOnMonoBus) {
  auto runMono = [](InputMode mode) {
    auto proc = makeRig(makeMonoAmpCabState(), mode, 1, 1);
    const auto& in = takeL();
    std::vector<float> out(in.size(), 0.0f);
    juce::AudioBuffer<float> buffer(1, kBlock);
    juce::MidiBuffer midi;
    for (int off = 0; off + kBlock <= static_cast<int>(in.size()); off += kBlock) {
      buffer.copyFrom(0, 0, in.data() + off, kBlock);
      proc->processBlock(buffer, midi);
      std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock, out.begin() + off);
    }
    return out;
  };
  const auto dual = runMono(InputMode::DualMono);
  const auto stereo = runMono(InputMode::Stereo);
  EXPECT_GT(settledPeak(dual), 1e-3f);
  EXPECT_EQ(settledDiff(dual, stereo), 0.0f);
}

// ---------------------------------------------------------------------------
// Image stage
// ---------------------------------------------------------------------------

// Spread builds a double from channel 0; dual mono already outputs two real
// channels, so the group is idle no matter what its parameter says.
TEST(DualMonoTest, SpreadStaysIdle) {
  const auto state = makeMonoAmpCabState();
  const auto [dl, dr] = runRig(state, InputMode::DualMono, takeL(), takeR());

  auto spread = makeRig(state, InputMode::DualMono);
  spread->parameters.getParameter("spreadEnabled")->setValueNotifyingHost(1.0f);
  spread->parameters.getParameter("spreadOffset")->setValueNotifyingHost(1.0f);
  const auto [pl, pr] = processStereoLR(*spread, takeL(), takeR());
  EXPECT_EQ(settledDiff(dl, pl), 0.0f) << "Spread must not touch a dual mono output";
  EXPECT_EQ(settledDiff(dr, pr), 0.0f);

  // Control: the same Spread setting on the Stereo fold does run.
  auto control = makeRig(state, InputMode::Stereo);
  control->parameters.getParameter("spreadEnabled")->setValueNotifyingHost(1.0f);
  control->parameters.getParameter("spreadOffset")->setValueNotifyingHost(1.0f);
  const auto [cl, cr] = processStereoLR(*control, takeL(), takeR());
  EXPECT_GT(settledDiff(cl, cr), 1e-3f) << "Spread should run on the Stereo fold";
}

// Balance trims the two voices against each other (a diagonal tilt): full
// right attenuates the left voice by 12 dB and lifts the right by 12 dB
// (see balanceChainGain), each voice staying its own take.
TEST(DualMonoTest, BalanceTiltsTheTwoVoices) {
  const auto state = makeMonoAmpCabState();
  const auto [dl, dr] = runRig(state, InputMode::DualMono, takeL(), takeR());

  auto tilted = makeRig(state, InputMode::DualMono);
  tilted->parameters.getParameter("outputBalance")->setValueNotifyingHost(1.0f);
  const auto [tl, tr] = processStereoLR(*tilted, takeL(), takeR());

  const float expectL = juce::Decibels::decibelsToGain(-12.0f);
  const float expectR = juce::Decibels::decibelsToGain(12.0f);
  EXPECT_NEAR(settledPeak(tl) / settledPeak(dl), expectL, 1e-3f);
  EXPECT_NEAR(settledPeak(tr) / settledPeak(dr), expectR, 1e-3f);

  // Control: on the plain Stereo fold Balance is forced to center (nothing
  // to balance), as before.
  const auto [sl, sr] = runRig(state, InputMode::Stereo, takeL(), takeR());
  auto centered = makeRig(state, InputMode::Stereo);
  centered->parameters.getParameter("outputBalance")->setValueNotifyingHost(1.0f);
  const auto [cl, cr] = processStereoLR(*centered, takeL(), takeR());
  EXPECT_EQ(settledDiff(sl, cl), 0.0f) << "Balance must stay inert on a plain mono chain";
}

// Auto balance listens to the two voices and reports their level mismatch:
// a right take 6 dB down measures as a 6 dB difference and lands the knob
// accordingly.
TEST(DualMonoTest, AutoBalanceMeasuresTheTakes) {
  auto quietR = takeL();
  for (auto& s : quietR)
    s *= 0.5f;

  // An empty chain: the measurement is then exactly the source mismatch.
  auto proc = makeRig(juce::ValueTree("ChainSnapshot"), InputMode::DualMono);
  ASSERT_TRUE(proc->dualMonoEngaged());
  proc->startAutoBalance();
  processStereoLR(*proc, takeL(), quietR);  // 2.56 s of audio > the 2 s measurement

  const juce::var result = proc->pollAutoBalance();
  ASSERT_EQ(result["state"].toString(), "done");
  EXPECT_NEAR(std::abs(static_cast<float>(result["matchedDb"])), 6.02f, 0.1f);
  const float knob = proc->parameters.getParameter("outputBalance")->getValue();
  EXPECT_NEAR(std::abs(knob - 0.5f), 6.02f / 48.0f, 0.005f) << "knob should land on the trim";
}

// ---------------------------------------------------------------------------
// Meters
// ---------------------------------------------------------------------------

// Block meters read the louder of the two takes (what is in excess), and the
// main output meter keeps the two channels apart.
TEST(DualMonoTest, MetersShowTheLouderTakeAndSeparateChannels) {
  const auto state = makeMonoAmpCabState();

  auto dual = makeRig(state, InputMode::DualMono);
  processStereoLR(*dual, takeL(), silence());
  const juce::var dualMeters = dual->getMeterLevels();

  auto left = makeRig(state, InputMode::Left);
  processStereoLR(*left, takeL(), takeL());
  const juce::var leftMeters = left->getMeterLevels();

  // The block's meter is a per-callback peak over its channels: with the
  // right take silent it must read exactly the left take's level.
  for (const char* id : {"blk-amp", "blk-cab"}) {
    SCOPED_TRACE(id);
    EXPECT_FLOAT_EQ(static_cast<float>(dualMeters["blocks"][id]["out"]),
                    static_cast<float>(leftMeters["blocks"][id]["out"]));
    EXPECT_GT(static_cast<float>(dualMeters["blocks"][id]["out"]), -40.0f);
  }

  // Main meters: left carries the take, right is at the floor.
  EXPECT_GT(static_cast<float>(dualMeters["output"][0]), -40.0f);
  EXPECT_FLOAT_EQ(static_cast<float>(dualMeters["output"][1]), -60.0f);
  EXPECT_TRUE(static_cast<bool>(dual->getChainState(-1)["dualMonoActive"]));
}

// ---------------------------------------------------------------------------
// Voice count transitions
// ---------------------------------------------------------------------------

// The NAM engine follows the selection: two voices while Dual Mono is
// selected on a mono chain, one otherwise. Each transition rebuilds the
// engine from the block's model cache under the chain-edit mute, which is
// held until the rebuilt engine has landed.
TEST(DualMonoTest, EngineVoicesFollowTheMode) {
  auto proc = makeRig(makeMonoAmpCabState(), InputMode::Stereo);
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);

  proc->setInputMode(InputMode::DualMono);
  EXPECT_TRUE(proc->isChainEditFadeHeld()) << "the rebuild should ride a held mute";
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 2);

  // Left / Right from Dual Mono: back to one voice.
  proc->setInputMode(InputMode::Left);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);

  // Left → Right is a plain fold switch: no rebuild, no mute.
  proc->setInputMode(InputMode::Right);
  EXPECT_FALSE(proc->isChainEditFadeHeld());
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);

  proc->setInputMode(InputMode::DualMono);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 2);

  // Stereo chains don't use the second voice: toggling the chain mode moves
  // the requirement both ways while the selection stays Dual Mono.
  proc->setStereoMode(true);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->getInputMode(), InputMode::DualMono);
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);
  proc->setStereoMode(false);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 2);
}

// A mode change racing a load still in flight: the engine being built was
// sized for the old requirement, so the apply path must drop it and build
// again (see applyPreparedModelToChainBlock). Whichever way the race falls
// (load landed first → re-queued by the mode change; mode landed first →
// re-queued on apply), the settled engine has the right voice count.
TEST(DualMonoTest, ModeChangeDuringLoadSettlesOnTheRightVoiceCount) {
  for (const InputMode target : {InputMode::DualMono, InputMode::Stereo}) {
    SCOPED_TRACE(TONE3000Processor::inputModeToString(target));
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    // Restore with the *other* mode so the load is queued for the wrong
    // voice count, then flip immediately, before it can land.
    const InputMode start = target == InputMode::DualMono ? InputMode::Stereo : InputMode::DualMono;
    proc.restoreFromTree(makeMonoAmpCabState(), TONE3000Processor::inputModeToString(start));
    proc.setInputMode(target);
    ASSERT_TRUE(waitForChainLoaded(proc));
    EXPECT_EQ(proc.namEngineVoiceCount("blk-amp"), target == InputMode::DualMono ? 2 : 1);
    EXPECT_FALSE(proc.isChainEditFadeHeld());
  }
}

// A session restored with Dual Mono builds its engines two-voice straight
// away (the mode lands before the chain, see setStateInformation).
TEST(DualMonoTest, RestoreBuildsEnginesForTheRestoredMode) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  proc.restoreFromTree(makeMonoAmpCabState(), "dual");
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(proc.getInputMode(), InputMode::DualMono);
  EXPECT_EQ(proc.namEngineVoiceCount("blk-amp"), 2);
}

// Undo/redo across the chain-mode toggle re-checks the requirement too: a
// mono snapshot coming back while Dual Mono is selected must bring the
// second voice with it.
TEST(DualMonoTest, UndoAcrossStereoToggleRevoices) {
  auto proc = makeRig(makeMonoAmpCabState(), InputMode::DualMono);
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 2);

  proc->setStereoMode(true);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);

  ASSERT_TRUE(proc->undoChain());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_FALSE(proc->isStereoMode());
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 2);

  ASSERT_TRUE(proc->redoChain());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_TRUE(proc->isStereoMode());
  EXPECT_EQ(proc->namEngineVoiceCount("blk-amp"), 1);
}

// ---------------------------------------------------------------------------
// Persistence and the branch invariant
// ---------------------------------------------------------------------------

TEST(DualMonoTest, ModeStringsRoundTrip) {
  EXPECT_EQ(TONE3000Processor::inputModeToString(InputMode::DualMono), "dual");
  EXPECT_EQ(TONE3000Processor::inputModeFromString("dual"), InputMode::DualMono);
  EXPECT_EQ(TONE3000Processor::inputModeFromString("stereo"), InputMode::Stereo);
  EXPECT_EQ(TONE3000Processor::inputModeFromString("left"), InputMode::Left);
  EXPECT_EQ(TONE3000Processor::inputModeFromString("right"), InputMode::Right);
  // Unknown or missing: the default, as an older build reading "dual" does.
  EXPECT_EQ(TONE3000Processor::inputModeFromString(""), InputMode::Stereo);
  EXPECT_EQ(TONE3000Processor::inputModeFromString("bogus"), InputMode::Stereo);
  EXPECT_TRUE(TONE3000Processor::isStereoFeed(InputMode::DualMono));
  EXPECT_TRUE(TONE3000Processor::isStereoFeed(InputMode::Stereo));
  EXPECT_FALSE(TONE3000Processor::isStereoFeed(InputMode::Left));
  EXPECT_FALSE(TONE3000Processor::isStereoFeed(InputMode::Right));
}

// Session state carries the mode; presets don't (input routing is not tone),
// so a preset load leaves the selection alone.
TEST(DualMonoTest, SurvivesStateSaveRestoreButNotPresets) {
  auto proc = makeRig(makeMonoAmpCabState(), InputMode::DualMono);
  juce::MemoryBlock saved;
  proc->getStateInformation(saved);

  ChainTestProcessor restored;
  restored.setPlayConfigDetails(2, 2, kFs, kBlock);
  restored.prepareToPlay(kFs, kBlock);
  restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(restored));
  EXPECT_EQ(restored.getInputMode(), InputMode::DualMono);
  EXPECT_EQ(restored.namEngineVoiceCount("blk-amp"), 2);

  // A preset saved while in Dual Mono, loaded into a Left-mode session,
  // must not drag the selection along.
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-dual-mono-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  proc->setPresetStoreForTesting(tmp);
  const juce::var preset = proc->savePreset("Doubled");
  ASSERT_TRUE(preset.isObject());
  proc->setInputMode(InputMode::Left);
  ASSERT_TRUE(waitForChainLoaded(*proc));
  ASSERT_TRUE(proc->loadPreset(preset["id"].toString()));
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(proc->getInputMode(), InputMode::Left);
  // Look the amp up by format rather than id, in case the preset's blocks
  // come back under fresh ids.
  const juce::var chain = proc->getChainState(-1);
  std::string ampId;
  if (const auto* lane = chain["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone" && item["tone"]["format"].toString() == "nam")
        ampId = item["blockId"].toString().toStdString();
  ASSERT_FALSE(ampId.empty());
  EXPECT_EQ(proc->namEngineVoiceCount(ampId), 1);
  tmp.deleteRecursively();
}

// An active branch has a single mono source: engaging it forces a definite
// channel, and the stereo feeds are refused while it stands.
TEST(DualMonoTest, BranchForcesLeftAndRefusesStereoFeeds) {
  auto proc = makeRig(makeDuplicatedStereoState(), InputMode::DualMono);
  EXPECT_EQ(proc->getInputMode(), InputMode::DualMono);

  letAudioGoIdle();
  ASSERT_TRUE(proc->setChainBranch("left", "blk-amp-l"));
  EXPECT_EQ(proc->getInputMode(), InputMode::Left);

  proc->setInputMode(InputMode::DualMono);
  EXPECT_EQ(proc->getInputMode(), InputMode::Left) << "a branched chain must refuse dual mono";
  proc->setInputMode(InputMode::Stereo);
  EXPECT_EQ(proc->getInputMode(), InputMode::Left) << "a branched chain must refuse stereo";
  proc->setInputMode(InputMode::Right);
  EXPECT_EQ(proc->getInputMode(), InputMode::Right);

  // Dropping the branch frees the feeds again.
  ASSERT_TRUE(proc->clearChainBranch());
  proc->setInputMode(InputMode::DualMono);
  EXPECT_EQ(proc->getInputMode(), InputMode::DualMono);
}

// ---------------------------------------------------------------------------
// The Stereo fold on a mono chain: ½(L+R)
// ---------------------------------------------------------------------------

// A mono source on a stereo track (L == R) passes bit-identically: the fold
// is exact in float for equal operands, so every existing mono rig is
// untouched by the sum.
TEST(StereoFoldTest, IdenticalChannelsPassBitExact) {
  const auto state = makeMonoAmpCabState();
  const auto [sl, sr] = runRig(state, InputMode::Stereo, takeL(), takeL());
  const auto [ll, lr] = runRig(state, InputMode::Left, takeL(), takeL());
  EXPECT_EQ(settledDiff(sl, ll), 0.0f);
  EXPECT_EQ(settledDiff(sr, lr), 0.0f);
  EXPECT_EQ(settledDiff(sl, sr), 0.0f) << "a mono source should stay dual mono";
}

// One jack of a stereo pair (the other silent): the chain sees the take at
// half level, −6.02 dB, the same fold a host applies summing to mono.
TEST(StereoFoldTest, OneSidedSourceSumsToHalf) {
  const auto state = makeMonoAmpCabState();
  auto half = takeL();
  for (auto& s : half)
    s *= 0.5f;

  const auto [sl, sr] = runRig(state, InputMode::Stereo, takeL(), silence());
  const auto [hl, hr] = runRig(state, InputMode::Left, half, half);
  EXPECT_EQ(settledDiff(sl, hl), 0.0f) << "the fold should be exactly ½(L+R)";
  EXPECT_EQ(settledDiff(sr, hr), 0.0f);
  EXPECT_EQ(settledDiff(sl, sr), 0.0f);

  // Both takes present: the chain hears their average.
  std::vector<float> avg(takeL().size());
  for (size_t i = 0; i < avg.size(); ++i)
    avg[i] = 0.5f * takeL()[i] + 0.5f * takeR()[i];
  const auto [bl, br] = runRig(state, InputMode::Stereo, takeL(), takeR());
  const auto [al, ar] = runRig(state, InputMode::Left, avg, avg);
  EXPECT_EQ(settledDiff(bl, al), 0.0f);
}

// Stereo chains never fold: channel 0 feeds the Left chain and channel 1 the
// Right, so each lane's output equals a fold of its own channel onto both.
TEST(StereoFoldTest, NoFoldWithStereoChains) {
  const auto state = makeDuplicatedStereoState();
  const auto [sl, sr] = runRig(state, InputMode::Stereo, takeL(), takeR());
  const auto [ll, lr] = runRig(state, InputMode::Left, takeL(), takeR());
  const auto [rl, rr] = runRig(state, InputMode::Right, takeL(), takeR());
  EXPECT_EQ(settledDiff(sl, ll), 0.0f) << "Left chain should see channel 0 whole";
  EXPECT_EQ(settledDiff(sr, rr), 0.0f) << "Right chain should see channel 1 whole";
}

}  // namespace
