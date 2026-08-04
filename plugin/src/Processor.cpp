#include "Processor.h"
#if !HEADLESS
#include "Editor.h"
#endif
#include <cmath>
#include <random>
#include <cstring>
#include <tuple>

// StandalonePluginHolder: used to inspect the audio device's active input
// channels so we can detect a mono input source (see standaloneMonoInput).
#if !HEADLESS && JucePlugin_Build_Standalone && ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

// ##############
// MAIN PROCESSOR
// ##############
TONE3000Processor::TONE3000Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("PARAMETERS"), createParameterLayout()),
      bassFilter(juce::dsp::IIR::Coefficients<float>::makeLowShelf(48000, 100.0f, 1.0f, 1.0f)),
      midFilter(juce::dsp::IIR::Coefficients<float>::makePeakFilter(48000, 1000.0f, 1.0f, 1.0f)),
      trebleFilter(juce::dsp::IIR::Coefficients<float>::makeHighShelf(48000, 4000.0f, 1.0f, 1.0f)),
      // 2 threads for background loading, +1 so a chain-edit-fade release
      // waiter (releaseChainEditFadeWhenLoadsSettle) never serializes the
      // very loads it is waiting on.
      loadingThreadPool(3) {
  // Attach the file logger first thing: state restore (and the background
  // model loads it queues) runs before prepareToPlay, and its diagnostics
  // used to vanish because the logger didn't exist yet.
  if (!juce::Logger::getCurrentLogger()) {
    juce::Logger::setCurrentLogger(new juce::FileLogger(getLogFile(), "TONE3000 JUCE Log"));
  }

  // One-line snapshot of everything read from the shared machine-wide
  // settings file at construction, plus the file's own path — the first
  // thing to check when a "settings/login don't persist" report comes in
  // (wrong/unwritable path, or the file simply isn't there yet).
  juce::Logger::writeToLog(
      "[Processor] Settings file: " + getSettingsFile().getFullPathName() +
      " (exists=" + juce::String(getSettingsFile().existsAsFile() ? "yes" : "no") +
      ") | namFullSize=" + juce::String(namFullSize.load() ? "full" : "lite") +
      " multiCore=" + juce::String(multiCoreEnabled.load() ? "on" : "off"));

  resolveParamRefs();

  // Oversampling settings apply through a message-thread bounce (see
  // applyOversamplingSettings) — the relays can fire from any thread.
  parameters.addParameterListener("osEnabled", this);
  parameters.addParameterListener("osFactor", this);

  // MIDI performance events (delivered on the message thread — see
  // MidiMapper): program changes and prev/next steps walk the preset list,
  // mapped block-power and stereo stomps route through the normal undoable
  // chain edit paths.
  midiMapper.onProgramChange = [this](int program) { loadPresetAtIndex(program); };
  midiMapper.onPresetStep = [this](int delta) { stepPreset(delta); };
  midiMapper.onBlockPowerToggle = [this](int index, bool right) {
    toggleBlockPower(index, right);
  };
  midiMapper.onStereoToggle = [this] { setStereoMode(!isStereoMode()); };

  // Every lane starts at its minimum slot layout (kMinLaneSlots pass-through
  // insert placeholders). The right lane stays invisible until stereo mode is
  // enabled, but seeding it now keeps the invariant unconditional.
  for (auto& l : lanes)
    normalizeLaneInserts(l);
  // Built once (capturing only `this`) so invoking the boundary on the audio
  // thread never constructs a std::function per block.
  chainStageFunc = [this](float** inputs, float** outputs, int numFrames) {
    processOversampledChainStage(inputs, outputs, numFrames);
  };
  DBG("TONE3000Processor constructed");
}

// One-time string-keyed lookups; everything after this reads the atomics.
void TONE3000Processor::resolveParamRefs() {
  auto get = [this](const char* id) { return parameters.getRawParameterValue(id); };
  paramRefs.inputLevel = get("inputLevel");
  paramRefs.outputLevel = get("outputLevel");
  paramRefs.outputBalance = get("outputBalance");
  paramRefs.spreadEnabled = get("spreadEnabled");
  paramRefs.spreadOffset = get("spreadOffset");
  paramRefs.spreadWobble = get("spreadWobble");
  paramRefs.stereoOffsetEnabled = get("stereoOffsetEnabled");
  paramRefs.stereoOffsetTime = get("stereoOffsetTime");
  paramRefs.chainPanLeft = get("chainPanLeft");
  paramRefs.chainPanRight = get("chainPanRight");
  paramRefs.toneBass = get("toneBass");
  paramRefs.toneMid = get("toneMid");
  paramRefs.toneTreble = get("toneTreble");
  paramRefs.gateThreshold = get("gateThreshold");
  paramRefs.gateEnabled = get("gateEnabled");
  paramRefs.toneEqEnabled = get("toneEqEnabled");
  paramRefs.targetLoudness = get("targetLoudness");
  paramRefs.calibrateInput = get("calibrateInput");
  paramRefs.inputCalibrationLevel = get("inputCalibrationLevel");
  paramRefs.osEnabled = get("osEnabled");
  paramRefs.osFactor = get("osFactor");
}

juce::AudioProcessorValueTreeState::ParameterLayout TONE3000Processor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  // Non-zero version hints are required for AU (Logic/GarageBand) parameter stability; see
  // juce_AudioProcessor.cpp validateParameter() when JucePlugin_Build_AU is defined.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"inputLevel", 1}, "inputLevel", 0.0f, 1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"toneBass", 2}, "toneBass", 0.01f, 10.0f, 5.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"toneMid", 3}, "toneMid", 0.01f, 10.0f, 5.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"toneTreble", 4}, "toneTreble", 0.01f, 10.0f, 5.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"outputLevel", 5}, "outputLevel", 0.0f, 1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"gateThreshold", 6}, "gateThreshold", -100.0f, 0.0f, -80.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"targetLoudness", 7}, "targetLoudness", -60.0f, 0.0f, -18.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"calibrateInput", 9}, "calibrateInput", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"inputCalibrationLevel", 10}, "inputCalibrationLevel", -60.0f, 60.0f, 12.0f));

  // Faceplate power switches (gate + global 3-band tone stack).
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"gateEnabled", 11}, "gateEnabled", true));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"toneEqEnabled", 12}, "toneEqEnabled", true));

  // Balance trim: 0.5 = centered (no effect), otherwise an opposing ±12 dB
  // trim between the two chains, applied *before* the chain pan blend (see
  // the post-chain image stage in processBlock). Pre-pan placement is what
  // makes the knob mean "match chain A to chain B": a setting dialed in
  // while hard-panned stays correct at any pan position. In mono+spread it
  // tilts the dry/lag sides L/R (no pan there, so it's the same idea).
  // Only applies in stereo mode or mono+spread; the UI hides the knob when
  // inactive.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"outputBalance", 18}, "outputBalance", 0.0f, 1.0f, 0.5f));

  // Version hints 19–21 belonged to the removed spread params; hints are
  // never reused (AU keys parameter identity on them).

  // Spread (mono chain mode; see Spread.h / doubler-spec.md). Offset is
  // bipolar: 0.5 = center = 0 ms (identity); below center lags the left
  // channel, above center the right (0..24 ms). Wobble is the random-walk
  // delay modulation depth (0..±1.2 ms absolute, not relative to the
  // offset). Stored normalized; SpreadParams decodes. Defaults land a tight
  // classic ADT (+15 ms R, 25% wobble) so powering spread on — it defaults
  // off — is audible immediately. The retired pre-release spread params used
  // hints 19-21 with different semantics; these are fresh ids.
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"spreadEnabled", 27}, "spreadEnabled", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"spreadOffset", 28}, "spreadOffset", 0.0f, 1.0f, 0.8125f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"spreadWobble", 29}, "spreadWobble", 0.0f, 1.0f, 0.25f));

  // Stereo offset (stereo chain mode; see StereoOffset.h): corrective
  // alignment delay between the two chains. Bipolar: 0.5 = center = 0 ms;
  // below center delays the left chain, above center the right (0..24 ms).
  // Defaults to center — a corrective tool has no useful nonzero default.
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"stereoOffsetEnabled", 30}, "stereoOffsetEnabled", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"stereoOffsetTime", 31}, "stereoOffsetTime", 0.0f, 1.0f, 0.5f));

  // Stereo-mode chain pans: constant-power positions for the Left/Right
  // chain outputs (0 = hard left, 1 = hard right). The UI constrains the
  // Left chain to [0, 0.5] and the Right chain to [0.5, 1] (each knob spans
  // hard side <-> center); the DSP takes any absolute position. Defaults keep
  // the classic hard-panned dual-chain image, which the DSP detects and
  // skips entirely. chainPanLinked is a UI behavior flag (mirrored knob
  // moves); persisted as a parameter so sessions/presets restore it.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"chainPanLeft", 22}, "chainPanLeft", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"chainPanRight", 23}, "chainPanRight", 0.0f, 1.0f, 1.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"chainPanLinked", 24}, "chainPanLinked", true));

  // ── Oversampling (Advanced settings; see ChainOversampler.h) ──
  // Deliberately not automatable: a factor change rebuilds every NAM engine
  // and re-prepares the whole chain — a settings action, not a performance
  // control. Choice index i maps to factor 2^(i+1) (2x/4x/8x).
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"osEnabled", 25}, "osEnabled", false,
      juce::AudioParameterBoolAttributes().withAutomatable(false)));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"osFactor", 26}, "osFactor", juce::StringArray{"2x", "4x", "8x"}, 0,
      juce::AudioParameterChoiceAttributes().withAutomatable(false)));

  return layout;
}

int TONE3000Processor::resolvedOversampleFactor() const {
  if (paramRefs.osEnabled == nullptr || paramRefs.osFactor == nullptr)
    return 1;
  if (paramRefs.osEnabled->load() < 0.5f)
    return 1;
  const int choiceIndex = static_cast<int>(std::lround(paramRefs.osFactor->load()));
  return 1 << (juce::jlimit(0, 2, choiceIndex) + 1);
}

void TONE3000Processor::parameterChanged(const juce::String& parameterID, float newValue) {
  // Only the oversampling params are subscribed. Defer to the message thread:
  // this can fire from the UI relays or host restore, and the apply is heavy.
  juce::ignoreUnused(parameterID, newValue);
  triggerAsyncUpdate();
}

void TONE3000Processor::handleAsyncUpdate() {
  applyOversamplingSettings();
}

// Message thread. Re-rates the whole chain domain after an osEnabled/osFactor
// change: the oversampler and every linear engine (EQ/spectrum/smoothers,
// plus each IR block's base-rate island — the convolvers themselves stay at
// the base rate untouched) re-prepare in place under the chain-edit fade;
// NAM engines need a different phase count, so they drop to dry passthrough
// and rebuild off-thread from the block's in-memory model cache, fading back
// in as each lands.
void TONE3000Processor::applyOversamplingSettings() {
  const int newFactor = resolvedOversampleFactor();
  if (newFactor == chainOversampleFactor.load())
    return;

  juce::Logger::writeToLog("[Processor] Oversampling ×" +
                           juce::String(chainOversampleFactor.load()) + " -> ×" +
                           juce::String(newFactor) + " (chain rate " +
                           juce::String(kChainBaseSampleRate * newFactor) + " Hz)");

  // Glide the chain output to silence, splice the rate change in between
  // callbacks, glide back — the same fade structural chain edits use.
  ChainEditFade fade(*this);
  juce::ScopedLock lock(chainMutex);

  chainOversampleFactor.store(newFactor);
  chainOversampler.prepare(newFactor, juce::jmax(1, chainBaseBlockSize()));

  // The chain-domain scratch grows with the factor; the RT path never
  // resizes it.
  for (auto& scratch : laneDryScratch) {
    scratch.setSize(2, juce::jmax(1, chainDomainBlockSize()), false, false, true);
    scratch.clear();
  }

  for (auto& l : lanes)
    prepareChain(l);

  // IR blocks need nothing here: their convolvers run at the base rate
  // behind per-block islands (re-prepared by prepareChain above), so neither
  // the kernel nor the tail report moves with the factor.
  for (auto& l : lanes) {
    for (auto& block : l) {
      if (block->type == ChainBlockType::NAM && block->loaded && !block->modelLoading) {
        // In-flight loads are left alone: the apply path's factor-drift guard
        // re-queues them itself.
        block->loaded = false;
        block->modelLoading = true;
        queueActiveModelLoad(*block);
      }
    }
  }

  bumpChainRevision();
}

TONE3000Processor::~TONE3000Processor() {
  parameters.removeParameterListener("osEnabled", this);
  parameters.removeParameterListener("osFactor", this);
  cancelPendingUpdate();

  releaseResources();

  // Clean up both lanes only when the processor is actually being destroyed
  {
    juce::ScopedLock lock(chainMutex);
    for (auto& l : lanes)
      l.clear();
  }

  juce::Logger::writeToLog("[Processor] Destructor called");

  // Clean up the logger to prevent leaks
  juce::Logger::setCurrentLogger(nullptr);
}

// #############
// JUCE SETTINGS
// #############
const juce::String TONE3000Processor::getName() const {
  return "TONE3000";
}

bool TONE3000Processor::acceptsMidi() const {
  // MIDI in feeds the mapping engine only (CC/note → parameter, see
  // MidiMapper); the plugin is still an audio effect, not a synth.
  return true;
}
bool TONE3000Processor::producesMidi() const {
  return false;
}
bool TONE3000Processor::isMidiEffect() const {
  return false;
}
double TONE3000Processor::getTailLengthSeconds() const {
  // Two tail sources, report the longer one:
  //  - The longest loaded IR (reverb IRs run whole seconds; cab IRs sit far
  //    below the DC-blocker floor). Tracked lock-free in irTailBaseSamples —
  //    this (potentially RT-adjacent) query must not take the chain lock —
  //    and refreshed wherever the set of live IR engines changes (see
  //    refreshIrTailLength). IRs always convolve at the base rate, so the
  //    count is over kChainBaseSampleRate regardless of oversampling.
  //  - The 5 Hz first-order DC blocker decays over ~10 cycles (2 s); the
  //    reference NAM plugin reports the same allowance for VST3 tail checks.
  const double irTailSeconds = irTailBaseSamples.load() / kChainBaseSampleRate;
  const double dcBlockerTailSeconds = 10.0 / 5.0;
  return std::max(irTailSeconds, dcBlockerTailSeconds);
}

int TONE3000Processor::getNumPrograms() {
  return 1;
}
int TONE3000Processor::getCurrentProgram() {
  return 0;
}
void TONE3000Processor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}
const juce::String TONE3000Processor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}
void TONE3000Processor::changeProgramName(int index, const juce::String& newName) {
  juce::ignoreUnused(index, newName);
}

// #############################
// PREPARE A SINGLE CHAIN
// #############################
// Everything in a chain lives in the chain domain: chainSampleRate(), block
// sizes up to chainDomainBlockSize(). Host-rate changes only ever re-prepare
// because the domain block size depends on the host block size; oversampling
// changes re-prepare because the rate itself moves.
void TONE3000Processor::prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks) {
  const int domainBlockSize = chainDomainBlockSize();
  const double chainRate = chainSampleRate();

  for (auto& block : blocks) {
    if (block->type == ChainBlockType::NAM) {
      if (block->namEngine != nullptr) {
        block->namEngine->prepare(domainBlockSize);
        DBG("NAM engine prepared for block: " << block->id);
      } else {
        DBG("Warning: NAM block " << block->id << " has no engine to prepare");
      }
    } else if (block->type == ChainBlockType::IR && block->convolverMono != nullptr) {
      // Convolvers always run at the base rate behind the block's island
      // (see ChainBlock::irBaseRateIsland), so their spec only tracks the
      // base block size — never the oversampling factor.
      juce::dsp::ProcessSpec spec{kChainBaseSampleRate,
                                  static_cast<juce::uint32>(chainBaseBlockSize()), 2};
      block->convolverMono->prepare(spec);
      if (block->convolverStereo != nullptr)
        block->convolverStereo->prepare(spec);

      // Reset normalization smoother to current gain to prevent jumps on re-prepare
      if (block->loaded) {
        block->irNormalizationSmoother.reset(chainRate, 0.05f);
        block->irNormalizationSmoother.setCurrentAndTargetValue(block->irNormalizationGainLinear);
      }

      DBG("IR convolvers re-prepared for block: " << block->id);
    }

    // Every IR block keeps its base-rate island in step with the live factor
    // (bypass at ×1). Prepared even while unloaded — a later engine apply
    // re-prepares anyway, this just keeps the invariant simple.
    if (block->type == ChainBlockType::IR)
      block->irBaseRateIsland.prepare(chainOversampleFactor.load(),
                                      juce::jmax(1, chainBaseBlockSize()));

    // Initialize per-block smoothers (input gain, output gain, mix, NAM
    // normalization). The RT path only ever calls setTargetValue on these —
    // reset() belongs here and in the model-apply path, never per block.
    block->inputGainSmoother.reset(chainRate, 0.05f);
    block->outputGainSmoother.reset(chainRate, 0.05f);
    block->mixSmoother.reset(chainRate, 0.05f);
    block->namNormalizationSmoother.reset(chainRate, 0.05f);
    block->inputGainSmoother.setCurrentAndTargetValue(1.0f);   // updated on first process
    block->outputGainSmoother.setCurrentAndTargetValue(1.0f);  // updated on first process
    block->mixSmoother.setCurrentAndTargetValue(block->mixNormalized);
    block->namNormalizationSmoother.setCurrentAndTargetValue(1.0f);
    block->wetFadeGain.reset(chainRate, kWetFadeSeconds);
    block->wetFadeGain.setCurrentAndTargetValue(block->enabled ? 1.0f : 0.0f);
    block->swapWetMuteGain.reset(chainRate, kWetFadeSeconds);
    block->swapWetMuteGain.setCurrentAndTargetValue(1.0f);

    // Per-block EQ + spectrum analyzer need the sample rate for their math.
    block->eq.prepare(chainRate);
    block->spectrum.prepare(chainRate);
  }
}

// See the declaration. Both lanes are scanned regardless of stereo mode —
// counting a disabled right lane's IR is a harmless over-report, and it means
// stereo toggles can never truncate a host's tail rendering mid-session.
void TONE3000Processor::refreshIrTailLength() {
  int maxSamples = 0;
  for (const auto& l : lanes)
    for (const auto& b : l)
      if (b->type == ChainBlockType::IR && b->convolverMono != nullptr)
        maxSamples = std::max(maxSamples, b->irLengthBaseSamples);
  irTailBaseSamples.store(maxSamples);
}

// Constant-power pan gains for a chain at position `pan` (0 = hard left,
// 1 = hard right): cos into the left output, sin into the right.
static std::pair<float, float> constantPowerPanGains(float pan) {
  const float angle = juce::jlimit(0.0f, 1.0f, pan) * juce::MathConstants<float>::halfPi;
  return {std::cos(angle), std::sin(angle)};
}

// Peak absolute sample across the buffer's first `numChannels` channels.
static float bufferPeak(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) {
  float peak = 0.0f;
  for (int ch = 0; ch < numChannels; ++ch) {
    const auto* data = buffer.getReadPointer(ch);
    for (int i = 0; i < numSamples; ++i)
      peak = std::max(peak, std::abs(data[i]));
  }
  return peak;
}

// Main-stage level as a linear gain: 0.5 = unity, full range ±24 dB.
static float mainStageGain(float level) {
  return juce::Decibels::decibelsToGain((level - 0.5f) * 48.0f);
}

// Per-chain balance gain: the balance trim (0.5 = centered) applies up to
// ±12 dB opposing between chain 0 (Left) and chain 1 (Right).
static float balanceChainGain(float balance, int chain) {
  const float trimDb = (balance - 0.5f) * 24.0f * (chain == 0 ? -1.0f : 1.0f);
  return juce::Decibels::decibelsToGain(trimDb);
}

// The four gains of the post-chain image matrix: per-chain balance trims
// multiplied into the constant-power pan gains. The balance applies to the
// *chains* (pre-pan), so it matches chain levels rather than tilting the
// output bus — an output-channel trim couldn't re-balance the chains once
// the pan blend has mixed them. When pan is inactive (mono+spread) the pan
// part is the identity and the matrix reduces to a diagonal L/R tilt.
struct ImageGains { float lToL, lToR, rToL, rToR; };
static ImageGains imageMatrixGains(bool panActive, float balance, float panLeft, float panRight) {
  float pLtoL = 1.0f, pLtoR = 0.0f, pRtoL = 0.0f, pRtoR = 1.0f;
  if (panActive) {
    std::tie(pLtoL, pLtoR) = constantPowerPanGains(panLeft);
    std::tie(pRtoL, pRtoR) = constantPowerPanGains(panRight);
  }
  const float balL = balanceChainGain(balance, 0);
  const float balR = balanceChainGain(balance, 1);
  return {balL * pLtoL, balL * pLtoR, balR * pRtoL, balR * pRtoR};
}

// Stereo input = stereo main bus, minus the standalone case where it isn't
// really: a mono input device. Pure capability — the input-mode selection
// doesn't affect it (the UI needs the button to stay visible so the user can
// cycle back to stereo). Reported through getChainState, so bump the
// revision on change.
void TONE3000Processor::updateStereoInputDetection() {
  const bool stereoIn = getMainBusNumInputChannels() >= 2 && !standaloneMonoInput.load();
  if (stereoInputDetected.exchange(stereoIn) != stereoIn)
    bumpChainRevision();
}

void TONE3000Processor::setInputMode(InputMode mode) {
  if (mode == InputMode::Stereo) {
    // An *active* branch has a single (mono) source; a stereo fold would
    // silently drop the non-trunk channel. The UI hides the option — this
    // guards MIDI/stale callers. A dormant branch (mono mode) doesn't
    // constrain the fold; re-enabling stereo re-enforces it.
    juce::ScopedLock lock(chainMutex);
    if (rtBranchTapIndex >= 0) {
      DBG("setInputMode: stereo fold unavailable while the chain is branched");
      return;
    }
  }
  inputMode.store(static_cast<int>(mode));
  bumpChainRevision();
  DBG("Input mode: " << inputModeToString(mode));
}

// Physical group delay of a ChainBoundaryResampler at this host rate, in
// host samples. GetLatency() only counts the warm-up prefill and misses the
// residual group delay of the two Lanczos kernels (2-5 samples, growing with
// the rate ratio), so an impulse is run through a scratch boundary and the
// peak located — exact by construction, and cheap enough for prepareToPlay.
static int measureChainBoundaryLatency(double hostRate, int blockSize) {
  ChainBoundaryResampler probe(kChainBaseSampleRate);
  probe.Reset(hostRate, blockSize);

  const int total = ((probe.GetLatency() + 2 * blockSize) / blockSize + 1) * blockSize;
  juce::AudioBuffer<float> in(2, total), out(2, total);
  in.clear();
  out.clear();
  in.setSample(0, 0, 1.0f);
  in.setSample(1, 0, 1.0f);

  auto identity = [](float** inputs, float** outputs, int frames) {
    juce::FloatVectorOperations::copy(outputs[0], inputs[0], frames);
    juce::FloatVectorOperations::copy(outputs[1], inputs[1], frames);
  };
  for (int offset = 0; offset < total; offset += blockSize) {
    float* ins[2] = {in.getWritePointer(0, offset), in.getWritePointer(1, offset)};
    float* outs[2] = {out.getWritePointer(0, offset), out.getWritePointer(1, offset)};
    probe.ProcessBlock(ins, outs, blockSize, identity);
  }

  int peak = 0;
  float best = 0.0f;
  const float* y = out.getReadPointer(0);
  for (int i = 0; i < total; ++i)
    if (std::abs(y[i]) > best) {
      best = std::abs(y[i]);
      peak = i;
    }
  return peak;
}

// #############################
// PREPARATIONS BEFORE RT THREAD
// #############################
void TONE3000Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  hostSampleRate = sampleRate;
  maxBlockSize = samplesPerBlock;

  tuner.prepare(sampleRate);

  // CPU readout: proportion of the callback budget spent in processBlock.
  loadMeasurer.reset(sampleRate, samplesPerBlock);

  juce::Logger::writeToLog("[Processor] prepareToPlay: sampleRate=" + juce::String(sampleRate) +
                           ", samplesPerBlock=" + juce::String(samplesPerBlock));

  // Prime the cached parameter values from the resolved atomics.
  updateCachedParameters();


  // Detect a mono input source in the standalone app. The device restarts (and
  // re-runs prepareToPlay) whenever the user changes the audio setup, so this
  // stays in sync with the selected device. Hosts (VST3/AU) never take this
  // path — channel layouts there come from the bus configuration.
  standaloneMonoInput.store(false);
#if !HEADLESS && JucePlugin_Build_Standalone && ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
  if (wrapperType == wrapperType_Standalone) {
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
      if (auto* device = holder->deviceManager.getCurrentAudioDevice())
        standaloneMonoInput.store(device->getActiveInputChannels().countNumberOfSetBits() == 1);
  }
#endif

  updateStereoInputDetection();

  // ── Chain-domain resampling boundary ──
  // Engaged whenever the host rate differs from the chain base rate — even
  // for an empty chain, so reported latency is a constant per host rate and
  // chain edits never trigger a PDC change. At a 48k host the boundary is
  // dropped entirely and the chain stage runs directly on the host buffer.
  const bool boundaryNeeded = std::abs(sampleRate - kChainBaseSampleRate) > 0.1;
  if (boundaryNeeded) {
    if (chainBoundary == nullptr)
      chainBoundary = std::make_unique<ChainBoundaryResampler>(kChainBaseSampleRate);
    chainBoundary->Reset(sampleRate, juce::jmax(1, samplesPerBlock));
    // Not GetLatency() — that under-reports by the Lanczos kernels' group
    // delay, and hosts align dry paths against this number.
    chainBoundaryLatency = measureChainBoundaryLatency(sampleRate, juce::jmax(1, samplesPerBlock));
  } else {
    chainBoundary.reset();
    chainBoundaryLatency = 0;
  }
  // The oversampler is minimum-phase (zero reported latency), so the boundary
  // remains the only latency source at any factor.
  setLatencySamples(chainBoundaryLatency);
  DBG("Chain boundary " << (boundaryNeeded ? "engaged" : "bypassed")
      << " (latency: " << chainBoundaryLatency << " samples)");

  // ── Chain oversampler ──
  // Resolve the requested factor before anything chain-domain is sized: the
  // domain block size and rate both depend on it. Hosts re-run prepareToPlay
  // freely, so this also picks up a factor restored from session state.
  chainOversampleFactor.store(resolvedOversampleFactor());
  chainOversampler.prepare(chainOversampleFactor.load(), juce::jmax(1, chainBaseBlockSize()));
  DBG("Chain oversampling ×" << chainOversampleFactor.load() << " (chain rate: "
      << chainSampleRate() << " Hz)");

  // Prepare every engine in both lanes for the chain domain (fixed rate; the
  // domain block size depends on the host rate/block size).
  {
    juce::ScopedLock lock(chainMutex);
    for (auto& l : lanes)
      prepareChain(l);
  }

  juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2};
  bassFilter.prepare(spec);
  midFilter.prepare(spec);
  trebleFilter.prepare(spec);
  dcBlocker.prepare(spec);
  spread.prepare(sampleRate, samplesPerBlock);
  stereoOffset.prepare(sampleRate, samplesPerBlock);
  autoOffset.prepare(sampleRate);
  inputGate.prepare(sampleRate);

  // Chain-edit fade: host-rate. Primed audible normally — but a device can
  // start while a fade session holds the chain (launch: state restore arms
  // the mute, then the audio device opens while models still load). Priming
  // to 1 then would blast the half-loaded chain for the glide-down; honor
  // the pending mute instead and mark it landed (pre-callback, so snapping
  // is safe — this also unblocks any requester waiting out a device
  // restart).
  chainEditFadeGain.reset(sampleRate, kWetFadeSeconds);
  const bool editFadeHeld = chainEditFadePending.load();
  chainEditFadeGain.setCurrentAndTargetValue(editFadeHeld ? 0.0f : 1.0f);
  if (editFadeHeld)
    chainEditFadeDone.store(true);

  // Output-stage gain, primed from the current parameters so a restored
  // session doesn't glide in from the wrong level.
  outputGainSmoother.reset(sampleRate, 0.02);
  outputGainSmoother.setCurrentAndTargetValue(mainStageGain(cacheOutputLevel));

  // Post-chain image matrix (balance × pan): 20 ms ramps, primed from the
  // current parameters so a restored session doesn't fade in from the wrong
  // image or chain balance.
  {
    const bool isStereo = stereoEnabled.load();
    const bool applyBalance = isStereo || cacheSpreadEnabled;
    const auto g = imageMatrixGains(isStereo, applyBalance ? cacheOutputBalance : 0.5f,
                                    cacheChainPanLeft, cacheChainPanRight);
    for (auto* smoother : {&imageGainLtoL, &imageGainLtoR, &imageGainRtoL, &imageGainRtoR})
      smoother->reset(sampleRate, 0.02);
    imageGainLtoL.setCurrentAndTargetValue(g.lToL);
    imageGainLtoR.setCurrentAndTargetValue(g.lToR);
    imageGainRtoL.setCurrentAndTargetValue(g.rToL);
    imageGainRtoR.setCurrentAndTargetValue(g.rToR);
  }

  // Temporary unity filters for boot
  *bassFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, 100.0f, 1.0f, 1.0f);
  *midFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, 1000.0f, 1.0f, 1.0f);
  *trebleFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 4000.0f, 1.0f, 1.0f);
  // First-order high-pass at 5 Hz: removes DC offset from nonlinear NAM models
  // while staying audibly and phase-wise transparent down to the lowest bass
  // fundamentals (matches the reference NeuralAmpModelerPlugin behavior).
  *dcBlocker.state =
      *juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(sampleRate, 5.0f);

  bassFilter.reset();
  midFilter.reset();
  trebleFilter.reset();
  dcBlocker.reset();

  // Scratch buffers, sized once here — the RT path never resizes them.
  // The lane dry scratches live in the chain domain, where a callback can
  // carry more frames than the host block (e.g. a 44.1k host upsampled to 48k).
  for (auto& scratch : laneDryScratch) {
    scratch.setSize(2, chainDomainBlockSize(), false, false, true);
    scratch.clear();
  }
  chainScratchChannel.setSize(1, samplesPerBlock, false, false, true);
  chainScratchChannel.clear();

  // (Re)start the lane worker with the new callback geometry. It idles until
  // a stereo callback actually forks (see rtParallelLanes); starting it here
  // unconditionally keeps the multi-core toggle a pure dispatch gate.
  laneWorker.start(sampleRate, samplesPerBlock);

  // Apply the actual tone knob gains to the tone stack filters.
  updateEqCoefficients();
}

// #################
// RELEASE RESOURCES
// #################
void TONE3000Processor::releaseResources() {
  juce::Logger::writeToLog("[Processor] releaseResources() called");

  // The lane worker only lives while the host is running audio callbacks
  // (prepareToPlay restarts it). Stopping here also guarantees no worker
  // outlives the buffers/lanes a stale job could reference.
  laneWorker.stop();

  // DO NOT clear chain blocks here! They should persist across bypass/unbypassed states.
  // Chain blocks are managed by the plugin's state system and should only be cleared
  // when the plugin is actually destroyed or when explicitly requested by the user.
  
  // Only reset the audio processing components, not the plugin state
  bassFilter.reset();
  midFilter.reset();
  trebleFilter.reset();
  dcBlocker.reset();
  inputGate.reset();
}

bool TONE3000Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
}

// ######################
// UPDATE EQ COEFFICIENTS
// ######################
void TONE3000Processor::updateEqCoefficients() {
  // Recalculate EQ filter coefficients based on current tone gains
  auto bassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(getSampleRate(), 250.0f, 1.0f,
                                                                      cacheBassTone / 5.0f);

  auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(getSampleRate(), 1000.0f,
                                                                       1.0f, cacheMidTone / 5.0f);

  auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(getSampleRate(), 4000.0f,
                                                                         1.0f, cacheTrebleTone / 5.0f);

  *bassFilter.state = *bassCoeffs;
  *midFilter.state = *midCoeffs;
  *trebleFilter.state = *trebleCoeffs;
}

// ######################
// GLOBAL 3-BAND TONE STACK
// ######################
// Runs once per block, after the DC blocker (post-chain). Skipped entirely
// while powered off; filters reset on re-enable so no stale state rings in.
void TONE3000Processor::processToneStack(juce::AudioBuffer<float>& buffer) {
  if (cacheToneEqEnabled) {
    if (!toneEqWasEnabled) {
      bassFilter.reset();
      midFilter.reset();
      trebleFilter.reset();
    }
    if (eqParamsDirty) {
      updateEqCoefficients();
      eqParamsDirty = false;
    }

    juce::dsp::AudioBlock<float> eqBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> eqContext(eqBlock);
    bassFilter.process(eqContext);
    midFilter.process(eqContext);
    trebleFilter.process(eqContext);
  }
  toneEqWasEnabled = cacheToneEqEnabled;
}

// ####################
// UPDATE CACHED PARAMS
// ####################
void TONE3000Processor::updateCachedParameters() {
  constexpr float epsilon = 1e-5f;

  // Plain atomic loads — the string-keyed lookups happened once in
  // resolveParamRefs(). `tone` marks the tone-stack floats whose changes
  // must dirty the EQ coefficients.
  auto updateFloat = [&](float& cached, const std::atomic<float>* param, bool tone = false) {
    const float value = param->load();
    if (std::abs(value - cached) > epsilon) {
      cached = value;
      if (tone)
        eqParamsDirty = true;
    }
  };

  updateFloat(cacheInputLevel, paramRefs.inputLevel);
  updateFloat(cacheOutputLevel, paramRefs.outputLevel);
  updateFloat(cacheOutputBalance, paramRefs.outputBalance);
  updateFloat(cacheSpreadOffset, paramRefs.spreadOffset);
  updateFloat(cacheSpreadWobble, paramRefs.spreadWobble);
  updateFloat(cacheStereoOffsetTime, paramRefs.stereoOffsetTime);
  updateFloat(cacheChainPanLeft, paramRefs.chainPanLeft);
  updateFloat(cacheChainPanRight, paramRefs.chainPanRight);
  updateFloat(cacheBassTone, paramRefs.toneBass, true);
  updateFloat(cacheMidTone, paramRefs.toneMid, true);
  updateFloat(cacheTrebleTone, paramRefs.toneTreble, true);
  updateFloat(cacheGateThreshold, paramRefs.gateThreshold);
  updateFloat(cacheTargetLoudness, paramRefs.targetLoudness);
  updateFloat(cacheInputCalibrationLevel, paramRefs.inputCalibrationLevel);

  auto loadBool = [](const std::atomic<float>* param) { return param->load() > 0.5f; };
  cacheCalibrateInput = loadBool(paramRefs.calibrateInput);
  cacheGateEnabled = loadBool(paramRefs.gateEnabled);
  cacheToneEqEnabled = loadBool(paramRefs.toneEqEnabled);
  cacheSpreadEnabled = loadBool(paramRefs.spreadEnabled);
  cacheStereoOffsetEnabled = loadBool(paramRefs.stereoOffsetEnabled);
}

// ##########################
// RT PROCESS A SINGLE CHAIN
// ##########################
// Runs the per-block chain loop on `buffer`. The buffer may be mono (a single side in stereo
// mode) or 1-2 channels (mono mode). All per-channel work is keyed on buffer.getNumChannels().
// Must be called while holding chainMutex.
void TONE3000Processor::processChainOnBuffer(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                             juce::AudioBuffer<float>& buffer,
                                             juce::AudioBuffer<float>& dryScratch, int beginIdx,
                                             int endIdx) {
  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  if (endIdx < 0)
    endIdx = static_cast<int>(blocks.size());
  beginIdx = juce::jlimit(0, static_cast<int>(blocks.size()), beginIdx);
  endIdx = juce::jlimit(beginIdx, static_cast<int>(blocks.size()), endIdx);

  // Highest index of an enabled+loaded NAM block. Used twice: a stereo IR is only worth
  // processing in true stereo when there is no NAM downstream to collapse the image back to
  // mono, and NAM blocks *before* this index hand off at calibrated output level (see the
  // post-model gain stage below) while the last one keeps loudness normalization.
  // Computed over the whole lane even for a partial range — a branched trunk
  // is still one chain split around the tap, not two chains.
  int lastNamIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    const auto& b = blocks[i];
    if (b->type == ChainBlockType::NAM && b->loaded && b->enabled)
      lastNamIndex = i;
  }

  for (int idx = beginIdx; idx < endIdx; ++idx) {
    const auto& block = blocks[idx];
    if (block->type == ChainBlockType::INSERT) {
      continue;  // Insert block is pass-through, no audio effect
    }

    // Wet-path fade (see ChainBlock.h): power toggles and pending engine
    // swaps glide the block's wet mix to silence instead of splicing the
    // waveform. Bypass-bound transitions ride wetFadeGain (output crossfades
    // toward dry); engine swaps ride swapWetMuteGain (wet term mutes, the
    // dry share of the user's mix holds — never exposes the un-processed
    // input). A disabled block keeps processing until the glide reaches
    // bypass, then is skipped exactly like before.
    const bool swapPending = block->swapFadePending.load();
    const bool muteSwap = swapPending && block->swapMuteWet.load();
    const bool wantsWet = block->enabled && !(swapPending && !muteSwap);
    block->wetFadeGain.setTargetValue(block->loaded && wantsWet ? 1.0f : 0.0f);
    block->swapWetMuteGain.setTargetValue(muteSwap ? 0.0f : 1.0f);
    const bool wetSilent =
        !block->wetFadeGain.isSmoothing() && block->wetFadeGain.getCurrentValue() <= 0.001f;
    if (wetSilent)
      block->swapFadeDone.store(true);  // a waiting requester may splice now

    if (!block->loaded || (!wantsWet && wetSilent)) {
      // Not processing: park the block's meters at the floor. The EQ view can
      // still be open, so keep its analyzer fed with the pass-through audio.
      block->inputMeterDb.store(-60.0f);
      block->outputMeterDb.store(-60.0f);
      if (block->spectrum.isEnabled())
        block->spectrum.pushSamples(buffer.getReadPointer(0),
                                    numChannels > 1 ? buffer.getReadPointer(1) : nullptr,
                                    numSamples);
      continue;
    }

    // Prepare dry copy before processing for mix (reuse the lane's scratch)
    jassert(dryScratch.getNumChannels() >= numChannels);
    jassert(dryScratch.getNumSamples() >= numSamples);
    dryScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1) {
      dryScratch.copyFrom(1, 0, buffer, 1, 0, numSamples);
    }

    // Per-block input gain (0.5 == unity, ±24 dB), applied after the dry copy
    // so Mix still blends against the untouched signal — this drives the
    // block's DSP harder/softer like a drive control. The block input meter
    // reads the post-gain signal (what the model actually receives).
    {
      const float inputGainDbBlock = (block->inputGainNormalized - 0.5f) * 48.0f;
      block->inputGainSmoother.setTargetValue(juce::Decibels::decibelsToGain(inputGainDbBlock));

      float blockInputPeak = 0.0f;
      auto* left = buffer.getWritePointer(0);
      auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
      for (int i = 0; i < numSamples; ++i) {
        const float g = block->inputGainSmoother.getNextValue();
        left[i] *= g;
        blockInputPeak = std::max(blockInputPeak, std::abs(left[i]));
        if (right) {
          right[i] *= g;
          blockInputPeak = std::max(blockInputPeak, std::abs(right[i]));
        }
      }

      // EQ in the PRE position: between the block's input gain and its model,
      // shaping what drives the amp/IR. Skipped entirely when flat/bypassed
      // (or in the default post position — see the post-block stage below).
      if (block->eq.isPre() && block->eq.isActive()) {
        block->eq.process(buffer);
        // Re-measure so the input meter still reads what the model receives.
        blockInputPeak = bufferPeak(buffer, numChannels, numSamples);
      }

      const float blockInputDb =
          blockInputPeak > 0.0f ? juce::Decibels::gainToDecibels(blockInputPeak) : -60.0f;
      block->inputMeterDb.store(std::max(-60.0f, blockInputDb));
    }

    if (block->type == ChainBlockType::NAM) {
      // NAM Processing (the engine runs at the chain rate — no per-block resampling)
      try {
        jassert(numSamples <= dryScratch.getNumSamples());

        if (block->namEngine == nullptr) {
          DBG("Warning: NAM block " << block->id << " has no engine - skipping");
          continue;
        }

        // Calculate additional calibration gain for this specific NAM block
        float calibrationGain = 1.0f;
        if (cacheCalibrateInput && block->namEngine->hasInputLevel()) {
          const double modelInputLevel = block->namEngine->getInputLevel();
          const double calibrationAdjustmentDb = cacheInputCalibrationLevel - modelInputLevel;
          calibrationGain = juce::Decibels::decibelsToGain(static_cast<float>(calibrationAdjustmentDb));
        }

        // Apply calibration gain to the buffer
        if (calibrationGain != 1.0f) {
          buffer.applyGain(0, 0, numSamples, calibrationGain);
          if (numChannels > 1) {
            buffer.applyGain(1, 0, numSamples, calibrationGain);
          }
        }

        // Process with the NAM engine (handles mono conversion internally)
        block->namEngine->process(buffer);

        // Post-model gain: calibrated hand-off OR loudness normalization,
        // never both — they have contradictory goals (reproduce the capture
        // rig's true level vs. make every capture equally loud).
        //
        // Calibrated hand-off applies only mid-chain (another NAM downstream)
        // when calibration is on and the model carries output_level_dbu.
        // Gain = model output dBu − user's calibration dBu converts the
        // model's output back into the user's analog reference frame; the
        // downstream NAM's input calibration then converts from that frame
        // into its own model's, so the user's setting cancels and the
        // hand-off carries exactly the level of physically plugging device A
        // into device B. Normalizing mid-chain instead would wreck the drive
        // level into the next model that calibration exists to preserve.
        //
        // The last NAM block deliberately stays on normalization: calibrated
        // output at the chain's end would swing overall volume with each
        // capture's metadata (a cranked-amp model can sit 20+ dB hot). Net
        // effect: calibration governs drive/character, normalization governs
        // listening level. No clamp on the hand-off gain — it's a physical
        // level difference, not a guess — only a metadata sanity check that
        // falls back to normalization when the value is junk.
        // The smoother was prepared off the RT path (prepareChain / model
        // apply) — here we only ever move its target.
        const float targetLufs = cacheTargetLoudness;  // use live target
        float blockGain = 1.0f;
        bool calibratedHandOff = false;
        if (cacheCalibrateInput && idx < lastNamIndex && block->namEngine->hasOutputLevel()) {
          const float modelOutputLevel = static_cast<float>(block->namEngine->getOutputLevel());
          if (std::isfinite(modelOutputLevel) && modelOutputLevel >= -60.0f &&
              modelOutputLevel <= 60.0f) {
            blockGain =
                juce::Decibels::decibelsToGain(modelOutputLevel - cacheInputCalibrationLevel);
            calibratedHandOff = true;
          }
        }
        if (!calibratedHandOff && block->normalizeEnabled) {
          float modelLoudnessDb = targetLufs;  // Default fallback
          if (block->namEngine->hasLoudness()) {
            modelLoudnessDb = static_cast<float>(block->namEngine->getLoudness());
          }
          if (!std::isfinite(modelLoudnessDb) || modelLoudnessDb < -100.0f || modelLoudnessDb > 0.0f) {
            modelLoudnessDb = targetLufs;
          }
          const float gainAdjustmentDb = juce::jlimit(-12.0f, 6.0f, targetLufs - modelLoudnessDb);
          blockGain = juce::Decibels::decibelsToGain(gainAdjustmentDb);
        }
        block->namNormalizationSmoother.setTargetValue(blockGain);

        // Apply per-block normalization / hand-off gain to the buffer
        auto* left = buffer.getWritePointer(0);
        auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i) {
          const float g = block->namNormalizationSmoother.getNextValue();
          left[i] *= g;
          if (right) right[i] *= g;
        }
      } catch (const std::exception&) {
        // RT-safe failure path: disable the block (stops it re-throwing every
        // block) and flag it; the message thread writes the log line when it
        // next serializes the chain — string building/logging can't run here.
        block->loaded = false;
        block->rtProcessingFailed.store(true);
        bumpChainRevision();  // wake the UI poll so the flag is drained
        buffer.copyFrom(0, 0, dryScratch, 0, 0, numSamples);
        if (numChannels > 1) {
          buffer.copyFrom(1, 0, dryScratch, 1, 0, numSamples);
        }
        continue;
      }
    } else if (block->type == ChainBlockType::IR && block->convolverMono != nullptr) {
      // IR Processing.
      try {
        // True-stereo only when: the IR file is stereo, the working buffer is stereo, and no
        // NAM block downstream would collapse the image back to mono. Otherwise apply the IR's
        // left channel to every audio channel (convolverMono, Stereo::no).
        const bool noNamAfter = (idx > lastNamIndex);
        const bool useStereoIr = block->irNumChannels > 1 && numChannels > 1 && noNamAfter &&
                                 block->convolverStereo != nullptr;
        auto& convolver = useStereoIr ? *block->convolverStereo : *block->convolverMono;

        // Convolution runs at the base rate inside the block's island: when
        // the chain is oversampled the island decimates the wet path, hands
        // the convolver base-rate frames, and interpolates back (a direct
        // pass at ×1). Linear processing gains nothing above the base rate —
        // this keeps IR CPU flat across oversampling factors and the IR
        // sound bit-identical to the non-oversampled chain.
        block->irBaseRateIsland.processBaseRateIsland(
            buffer.getArrayOfWritePointers(), numChannels, numSamples,
            [&convolver, numChannels](float* const* baseChannels, int baseFrames) {
              juce::dsp::AudioBlock<float> irBlock(baseChannels, static_cast<size_t>(numChannels),
                                                   static_cast<size_t>(baseFrames));
              convolver.process(juce::dsp::ProcessContextReplacing<float>(irBlock));
            });

        // Unit-energy normalization, always on: an IR file's absolute level
        // is an accident of capture/export (unlike a NAM capture's, which is
        // real information — hence NAM's normalize toggle). Attenuation-only;
        // smoother is prepared in prepareChain / model apply, only the
        // target moves on the RT path.
        block->irNormalizationSmoother.setTargetValue(
            juce::jlimit(0.0f, 1.0f, block->irNormalizationGainLinear));
        for (int i = 0; i < numSamples; ++i) {
          const float g = block->irNormalizationSmoother.getNextValue();
          for (int ch = 0; ch < numChannels; ++ch) {
            buffer.getWritePointer(ch)[i] *= g;
          }
        }
      } catch (const std::exception& e) {
        DBG("Error in IR processing for block " << block->id << ": " << e.what());
      }
    }

    // Apply per-block output gain (centered at 0.5 == unity) and mix with dry
    // Map normalized gain to linear: 0.5 -> 1.0, +/-0.5 -> +/-24 dB range.
    // Short (cab-like) IR blocks carry a fixed -18 dB pad on top: cab files
    // are peak-normalized to 0 dBFS and spectrally concentrated, far too hot
    // at unity. Long (reverb-like) IRs get no pad — unit-energy
    // normalization already puts them at ≈ dry level (see irIsLong in
    // ChainBlock.h). The UI knob still reads relative dB (0 at center); the
    // pad is invisible chain gain staging (see gainDbScale in knobScale.ts).
    // Classified at load, so changes ride the engine-swap fade.
    const float irOffsetDb =
        (block->type == ChainBlockType::IR && !block->irIsLong) ? -18.0f : 0.0f;
    const float gainDb = (block->outputGainNormalized - 0.5f) * 48.0f + irOffsetDb;
    const float targetLinear = juce::Decibels::decibelsToGain(gainDb);
    block->outputGainSmoother.setTargetValue(targetLinear);
    block->mixSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, block->mixNormalized));

    float blockOutputPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
      // wetFadeGain rides the mix (bypass-bound glides crossfade toward
      // dry); swapWetMuteGain rides the wet term only (engine swaps dip the
      // wet path to silence without exposing the dry input) — see
      // ChainBlock.h.
      const float g = block->outputGainSmoother.getNextValue() *
                      block->swapWetMuteGain.getNextValue();
      const float m = block->mixSmoother.getNextValue() * block->wetFadeGain.getNextValue();
      float wetL = buffer.getWritePointer(0)[i] * g;
      float dryL = dryScratch.getReadPointer(0)[i];
      buffer.getWritePointer(0)[i] = dryL * (1.0f - m) + wetL * m;
      blockOutputPeak = std::max(blockOutputPeak, std::abs(buffer.getWritePointer(0)[i]));
      if (numChannels > 1) {
        float wetR = buffer.getWritePointer(1)[i] * g;
        float dryR = dryScratch.getReadPointer(1)[i];
        buffer.getWritePointer(1)[i] = dryR * (1.0f - m) + wetR * m;
        blockOutputPeak = std::max(blockOutputPeak, std::abs(buffer.getWritePointer(1)[i]));
      }
    }

    // Fade handshake: tell a waiting requester the wet path is fully
    // silent and its change (swap/removal) can splice in silently. Which
    // gain carried the fade depends on the swap's shape (see ChainBlock.h).
    {
      const auto& fadeGain = muteSwap ? block->swapWetMuteGain : block->wetFadeGain;
      if (block->swapFadePending.load() && !fadeGain.isSmoothing() &&
          fadeGain.getCurrentValue() <= 0.001f)
        block->swapFadeDone.store(true);
    }

    // EQ in the POST position (default): the last stage of the block, applied
    // after gain + mix so it shapes exactly what leaves the block. Skipped
    // entirely when flat/bypassed (the PRE position ran before the model).
    if (!block->eq.isPre() && block->eq.isActive()) {
      block->eq.process(buffer);
      // The mix-loop peak is pre-EQ; re-measure so the meter reflects the
      // block's true output.
      blockOutputPeak = bufferPeak(buffer, numChannels, numSamples);
    }

    // Block output meter: post gain + mix + EQ, i.e. what this block hands to
    // the next one in the chain.
    const float blockOutputDb =
        blockOutputPeak > 0.0f ? juce::Decibels::gainToDecibels(blockOutputPeak) : -60.0f;
    block->outputMeterDb.store(std::max(-60.0f, blockOutputDb));

    // Feed the EQ editor's analyzer with the block's final output — only while
    // that block's EQ view is actually open in the UI.
    if (block->spectrum.isEnabled())
      block->spectrum.pushSamples(buffer.getReadPointer(0),
                                  numChannels > 1 ? buffer.getReadPointer(1) : nullptr,
                                  numSamples);
  }
}

// Fork/join for the stereo lanes (see LaneWorker.h). The job context lives on
// this stack frame and stays valid until join() returns; the lambda decays to
// a plain function pointer, so dispatching allocates nothing on the RT path.
void TONE3000Processor::processLanePair(Lane& workerBlocks,
                                        juce::AudioBuffer<float>& workerBuffer,
                                        juce::AudioBuffer<float>& workerScratch,
                                        int workerBeginIdx, Lane& localBlocks,
                                        juce::AudioBuffer<float>& localBuffer,
                                        juce::AudioBuffer<float>& localScratch,
                                        int localBeginIdx) {
  if (rtParallelLanes) {
    struct LaneJob {
      TONE3000Processor* proc;
      Lane* blocks;
      juce::AudioBuffer<float>* buffer;
      juce::AudioBuffer<float>* scratch;
      int beginIdx;
    } job{this, &workerBlocks, &workerBuffer, &workerScratch, workerBeginIdx};

    const bool dispatched = laneWorker.dispatch(
        [](void* ctx) {
          auto& j = *static_cast<LaneJob*>(ctx);
          j.proc->processChainOnBuffer(*j.blocks, *j.buffer, *j.scratch, j.beginIdx);
        },
        &job);

    processChainOnBuffer(localBlocks, localBuffer, localScratch, localBeginIdx);

    if (dispatched) {
      laneWorker.join();
      return;
    }
    // The worker wasn't running after all — finish its section inline.
    processChainOnBuffer(workerBlocks, workerBuffer, workerScratch, workerBeginIdx);
    return;
  }

  processChainOnBuffer(localBlocks, localBuffer, localScratch, localBeginIdx);
  processChainOnBuffer(workerBlocks, workerBuffer, workerScratch, workerBeginIdx);
}

// ##############################
// RT CHAIN STAGE (chain rate)
// ##############################
// The oversampled entry to the chain stage — the callable both invocation
// paths share (the boundary callback and the direct 48k-host path). Raises
// the rate by the current factor around processChainStage; transparent
// passthrough when oversampling is off. Called with chainMutex held.
void TONE3000Processor::processOversampledChainStage(float** inputs, float** outputs,
                                                     int numFrames) {
  chainOversampler.process(inputs, outputs, numFrames,
                           [this](float** chainIns, float** chainOuts, int chainFrames) {
                             processChainStage(chainIns, chainOuts, chainFrames);
                           });
}

// The encapsulated side of the chain-domain boundary: lane L (and lane R in
// stereo mode) over the given channel pointers. Invoked at the chain rate
// (48 kHz × oversampling factor) via processOversampledChainStage. Called
// with chainMutex held (processBlock takes it).
void TONE3000Processor::processChainStage(float** inputs, float** outputs, int numFrames) {
  // The boundary hands us distinct input/output buffers; the chain processes
  // in place, so move the audio to the output side first. On the direct path
  // the pointers alias and the copies are skipped.
  for (int ch = 0; ch < 2; ++ch) {
    if (outputs[ch] != inputs[ch])
      std::memcpy(outputs[ch], inputs[ch], sizeof(float) * static_cast<size_t>(numFrames));
  }

  if (rtStereoChains) {
    if (rtBranchTapIndex >= 0) {
      // Branched routing: the trunk lane runs on its own channel; the branch
      // lane's input is the trunk's signal after the tapped block (not the
      // raw channel input). Split the trunk around the tap: prefix → copy the
      // tap signal across → remainder and branch lane run independently.
      const int trunkCh = branchSourceSide == ChainSide::Right ? 1 : 0;
      const int branchCh = 1 - trunkCh;
      const ChainSide branchSide =
          branchSourceSide == ChainSide::Right ? ChainSide::Left : ChainSide::Right;

      float* trunkPtr[] = {outputs[trunkCh]};
      float* branchPtr[] = {outputs[branchCh]};
      juce::AudioBuffer<float> trunkBuf(trunkPtr, 1, numFrames);
      juce::AudioBuffer<float> branchBuf(branchPtr, 1, numFrames);

      auto& trunk = lane(branchSourceSide);
      auto& trunkScratch = laneDryScratch[static_cast<size_t>(laneIndex(branchSourceSide))];
      auto& branchScratch = laneDryScratch[static_cast<size_t>(laneIndex(branchSide))];
      // The prefix must complete before the tap copy, so it always runs
      // serially here; after the copy the trunk remainder and the branch
      // lane are independent and can fork (branch to the worker).
      processChainOnBuffer(trunk, trunkBuf, trunkScratch, 0, rtBranchTapIndex + 1);
      std::memcpy(outputs[branchCh], outputs[trunkCh],
                  sizeof(float) * static_cast<size_t>(numFrames));
      processLanePair(lane(branchSide), branchBuf, branchScratch, 0, trunk, trunkBuf,
                      trunkScratch, rtBranchTapIndex + 1);
    } else {
      // Stereo mode: each channel is an independent mono lane, processed in
      // place — no split/merge copies needed. Right lane to the worker (when
      // this callback forked — see rtParallelLanes), Left on this thread.
      float* left[] = {outputs[0]};
      float* right[] = {outputs[1]};
      juce::AudioBuffer<float> bufferL(left, 1, numFrames);
      juce::AudioBuffer<float> bufferR(right, 1, numFrames);
      processLanePair(lane(ChainSide::Right), bufferR, laneDryScratch[1], 0,
                      lane(ChainSide::Left), bufferL, laneDryScratch[0], 0);
    }
  } else {
    juce::AudioBuffer<float> chainBuffer(outputs, rtChainChannels, numFrames);
    processChainOnBuffer(lane(ChainSide::Left), chainBuffer, laneDryScratch[0]);
  }
}

// ################
// RT PROCESS BLOCK
// ################
void TONE3000Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  // Times this whole callback against its real-time budget (the CPU readout).
  juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer(loadMeasurer, buffer.getNumSamples());

  // Mapped MIDI first, so parameter moves (bypass stomps, expression sweeps)
  // land before this block's cached-parameter refresh below.
  midiMapper.processMidi(midi);

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  if (numSamples <= 0 || numChannels <= 0) {
    DBG("Invalid buffer: samples=" << numSamples << ", channels=" << numChannels);
    buffer.clear();
    outputMeterLevelL.store(-60.0f);
    outputMeterLevelR.store(-60.0f);
    return;
  }

  updateCachedParameters();

  // Heartbeat for isAudioActive(): fade handshakes skip their bounded waits
  // when no callbacks are running (nothing is audible then).
  lastAudioCallbackMs.store(juce::Time::currentTimeMillis());

  // Input fold-down, up front so everything downstream (meters, tuner,
  // chains) sees the effective source:
  // - Mono input device (standalone): signal only arrives on channel 0 —
  //   mirror it.
  // - Input mode L/R on a stereo source: duplicate the chosen channel onto
  //   both, exactly like a host feeding a mono source to a stereo bus.
  if (numChannels > 1) {
    if (standaloneMonoInput.load()) {
      buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    } else {
      switch (static_cast<InputMode>(inputMode.load())) {
        case InputMode::Left: buffer.copyFrom(1, 0, buffer, 0, 0, numSamples); break;
        case InputMode::Right: buffer.copyFrom(0, 0, buffer, 1, 0, numSamples); break;
        case InputMode::Stereo: break;
      }
    }
  }

  // #########################
  // Input gain + noise gate
  // #########################

  // Input gain (level ±24 dB), constant across the block. Computed up front
  // so the meters can show the post-gain level without a second pass over
  // the samples.
  const float inputGain = mainStageGain(cacheInputLevel);

  // Per-channel input meters: raw peaks scaled by the input gain, so the
  // meter tracks the knob. Pre-gate on purpose — a closed gate would
  // otherwise read as a dead input. Mono sources report the same level on
  // both channels.
  auto peakToDb = [](float peak) {
    return peak > 0.0f ? std::max(-60.0f, juce::Decibels::gainToDecibels(peak)) : -60.0f;
  };
  {
    float peakL = 0.0f, peakR = 0.0f;
    const auto* l = buffer.getReadPointer(0);
    for (int i = 0; i < numSamples; ++i)
      peakL = std::max(peakL, std::abs(l[i]));
    if (numChannels > 1) {
      const auto* r = buffer.getReadPointer(1);
      for (int i = 0; i < numSamples; ++i)
        peakR = std::max(peakR, std::abs(r[i]));
    } else {
      peakR = peakL;
    }
    inputMeterLevelL.store(peakToDb(peakL * inputGain));
    inputMeterLevelR.store(peakToDb(peakR * inputGain));
  }

  // Feed the tuner from the raw input (pre-gain, pre-gate) while the tuner
  // screen is open. Channel 0 only: guitar sources are mono, and mixing
  // channels risks phase cancellation.
  if (tuner.isEnabled())
    tuner.pushSamples(buffer.getReadPointer(0), numSamples);

  // Apply the input gain (vectorized).
  for (int ch = 0; ch < numChannels; ++ch)
    buffer.applyGain(ch, 0, numSamples, inputGain);

  // Noise gate, post input gain so the threshold knob's dB meaning matches
  // the level heading into the chain. Envelope/hysteresis gate (NoiseGate.h);
  // re-enabling resets the detector so a stale envelope never gates the
  // first block.
  if (cacheGateEnabled) {
    if (!gateWasEnabled)
      inputGate.reset();
    inputGate.setThresholdDb(cacheGateThreshold);
    inputGate.process(buffer);
  }
  gateWasEnabled = cacheGateEnabled;

  // ####################
  // MODULAR CHAIN PROCESSING (chain domain — 48 kHz × OS factor, see ChainDomain.h)
  // ####################
  // Runs under chainMutex — but the render thread must never *block* behind a
  // long splice. Preset/undo restores hold chainMutex on the message thread
  // while they decode megabytes of embedded model bytes; a blocking lock here
  // stalls the CoreAudio render thread for 100+ ms, which overloads the
  // driver and can restart the device (observed in the field: repeated
  // prepareToPlay/releaseResources cycles and a fallback to the OS default
  // device right after heavy preset loads). So: try the lock first. If it's
  // contended while the chain-edit fade has fully landed (pending && done ⇒
  // the tap below outputs silence no matter what the chain produces), skip
  // the stage wait-free — inaudible, and the splice can take as long as it
  // needs. Contention outside a landed fade is ordinary and brief (UI state
  // pulls, engine installs), so fall back to the blocking lock as before.
  const auto runChainStage = [&] {
    rtStereoChains = stereoEnabled.load() && numChannels >= 2;
    rtChainChannels = juce::jmin(numChannels, 2);

    // Fork the lanes across cores only when both sides of the parallel
    // section carry work — otherwise the handoff costs more than the empty
    // loop it would hide. For branched routing the parallel section is
    // trunk-suffix ∥ branch, so the trunk only counts blocks after the tap.
    rtParallelLanes = false;
    if (rtStereoChains && multiCoreEnabled.load(std::memory_order_relaxed) &&
        laneWorker.isRunning()) {
      if (rtBranchTapIndex >= 0) {
        const ChainSide branchSide =
            branchSourceSide == ChainSide::Right ? ChainSide::Left : ChainSide::Right;
        rtParallelLanes = laneHasWork(lane(branchSourceSide), rtBranchTapIndex + 1) &&
                          laneHasWork(lane(branchSide));
      } else {
        rtParallelLanes =
            laneHasWork(lane(ChainSide::Left)) && laneHasWork(lane(ChainSide::Right));
      }
    }

    // Hosts occasionally exceed the block size they promised in prepareToPlay.
    // Feed the chain stage in prepared-size slices so the boundary's internal
    // buffers (and the chain-domain scratch) can never overflow — a single
    // pass in the normal case.
    const int maxSlice = juce::jmax(1, maxBlockSize);
    for (int offset = 0; offset < numSamples; offset += maxSlice) {
      const int sliceLen = juce::jmin(maxSlice, numSamples - offset);

      // The boundary is a fixed 2-channel container, so a mono host buffer
      // gets the pre-cleared scratch as its second channel (processChainStage
      // only touches rtChainChannels of them; the boundary passes the rest
      // through).
      float* channels[2] = {buffer.getWritePointer(0) + offset,
                            numChannels > 1 ? buffer.getWritePointer(1) + offset
                                            : chainScratchChannel.getWritePointer(0)};

      if (chainBoundary != nullptr)
        chainBoundary->ProcessBlock(channels, channels, sliceLen, chainStageFunc);
      else
        processOversampledChainStage(channels, channels, sliceLen);
    }
  };

  {
    juce::ScopedTryLock tryLock(chainMutex);
    if (tryLock.isLocked()) {
      runChainStage();
    } else if (chainEditFadePending.load() && chainEditFadeDone.load()) {
      // A splice owns the lock and the output is held at silence: hand the
      // downstream stages a cleared buffer instead of raw input (the tap
      // would zero it anyway, but the DC blocker sits before the tap).
      buffer.clear();
    } else {
      juce::ScopedLock lock(chainMutex);
      runChainStage();
    }
  }

  // ##########
  // Post-chain stereo image, before the downstream stereo stages (DC / tone
  // stack / output gain + meters) so they see the real image. Order
  // matters: the mode's image engine runs first so it always shapes full
  // chain output, then the balance/pan matrix mixes the result.
  //  - Mono mode: the Spread builds the stereo image from the channel-0
  //    chain output (an ADT-style double — see Spread.h). Engage/bypass is
  //    its internal ~25 ms crossfade against the untouched buffer; fully
  //    skipped once idle.
  //  - Stereo mode: the StereoOffset delays one chain in place, purely
  //    corrective alignment (see StereoOffset.h): 0 ms = identity, all
  //    transitions glide through zero; fully skipped once idle.
  //  - The opposite mode's engine is force-idled (no fade needed): mode
  //    switches always ride the chain-edit fade above, so the hard stop
  //    lands on silence.
  //  - Balance + pan (one 2×2 matrix, see imageMatrixGains): the balance
  //    trim scales each *chain*, then the constant-power pan blend mixes
  //    them across the output bus. The centered/hard-panned default is the
  //    identity and skips the loop.
  // ##########
  {
    const bool isStereo = stereoEnabled.load() && numChannels >= 2;

    if (isStereo) {
      spread.forceIdle();

      // Auto offset listening tap: the raw chain outputs BEFORE the offset
      // delay, so the measurement is the chains' absolute misalignment —
      // independent of whatever the knob currently says. Zero work unless
      // armed.
      if (autoOffset.state() == AutoOffset::State::Listening)
        autoOffset.capture(buffer, numSamples);

      stereoOffset.setTarget(StereoOffsetParams::fromNormalized(cacheStereoOffsetTime),
                             cacheStereoOffsetEnabled);
      if (stereoOffset.isRunning())
        stereoOffset.process(buffer);
    } else {
      stereoOffset.forceIdle();
      spread.setTarget(SpreadParams::fromNormalized(cacheSpreadOffset, cacheSpreadWobble),
                       cacheSpreadEnabled && numChannels >= 2);
      if (spread.isRunning())
        spread.process(buffer);
    }

    // Auto balance listening tap: the raw chain outputs, before the balance
    // and pan gains, so the measurement is the chains' true mismatch. It must
    // sit pre-pan: post-pan the two channels converge as the pans approach
    // center even when the chains are badly mismatched, which would starve
    // the measurement. Zero work unless armed.
    if (autoBalanceState.load(std::memory_order_acquire) ==
        static_cast<int>(AutoBalanceState::Listening))
      runAutoBalanceStage(buffer, numSamples);

    // Balance + pan matrix. Balance is forced center whenever the output
    // image isn't actually stereo (mono mode without spread), so a leftover
    // Bal setting from a prior session can't skew a mono (identical L/R)
    // bus — matches the UI hiding the knob. All four gains are smoothed so
    // knob moves AND the balance on/off gating (spread/stereo toggles)
    // glide instead of stepping (pop).
    if (numChannels >= 2) {
      const bool applyBalance = isStereo || cacheSpreadEnabled;
      const auto g = imageMatrixGains(isStereo, applyBalance ? cacheOutputBalance : 0.5f,
                                      cacheChainPanLeft, cacheChainPanRight);
      imageGainLtoL.setTargetValue(g.lToL);
      imageGainLtoR.setTargetValue(g.lToR);
      imageGainRtoL.setTargetValue(g.rToL);
      imageGainRtoR.setTargetValue(g.rToR);

      const bool smoothing = imageGainLtoL.isSmoothing() || imageGainLtoR.isSmoothing() ||
                             imageGainRtoL.isSmoothing() || imageGainRtoR.isSmoothing();
      const bool identity = std::abs(g.lToL - 1.0f) < 1.0e-4f &&
                            std::abs(g.rToR - 1.0f) < 1.0e-4f &&
                            g.lToR < 1.0e-4f && g.rToL < 1.0e-4f;
      if (smoothing || !identity) {
        auto* l = buffer.getWritePointer(0);
        auto* r = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i) {
          const float ll = imageGainLtoL.getNextValue();
          const float lr = imageGainLtoR.getNextValue();
          const float rl = imageGainRtoL.getNextValue();
          const float rr = imageGainRtoR.getNextValue();
          const float chainL = l[i];
          const float chainR = r[i];
          l[i] = chainL * ll + chainR * rl;
          r[i] = chainL * lr + chainR * rr;
        }
      }
    }
  }

  // ##########
  // DC blocker
  // ##########
  {
    // ProcessorDuplicator runs an independent filter instance per channel, so a
    // single duplicator covers the whole (mono or stereo) buffer. Running a
    // second one here would high-pass every channel twice.
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    dcBlocker.process(context);
  }

  // ##########
  // Chain-edit fade (see ChainEditFade): structural edits that can't be
  // expressed as one block's wet fade (reorder, cross-lane move, preset /
  // undo restores) glide the whole chain output to silence, splice the edit
  // in between callbacks, and glide back. Idle cost: one atomic load + one
  // branch.
  //
  // The tap deliberately sits AFTER the image stage and DC blocker:
  //  - Image stage: its hard stops on mode switches (forceIdle) land
  //    upstream of this gain, so they still splice into silence.
  //  - DC blocker: NAM models can idle at a DC offset, and a restored rig's
  //    blocks fade in while the chain is held muted. With the tap upstream
  //    of the blocker, the blocker would settle to zero state during the
  //    hold and the new rig's DC would step through it at release — an
  //    audible thump on every preset/undo switch. Downstream of the
  //    blocker, the blocker tracks the live chain (including its DC)
  //    throughout the hold, so release ramps in an already-centered signal.
  // ##########
  {
    const bool editPending = chainEditFadePending.load();
    chainEditFadeGain.setTargetValue(editPending ? 0.0f : 1.0f);
    if (chainEditFadeGain.isSmoothing()) {
      for (int i = 0; i < numSamples; ++i) {
        const float g = chainEditFadeGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
          buffer.getWritePointer(ch)[i] *= g;
      }
    } else if (editPending && chainEditFadeGain.getCurrentValue() <= 0.001f) {
      // Fully faded: hold silence until the editor thread finishes its splice.
      buffer.clear();
      chainEditFadeDone.store(true);
    }
  }

  // ##########
  // EQ section (global 3-band tone stack), post-chain.
  // ##########
  processToneStack(buffer);

  // ###########
  // Output gain (level ±24 dB, same on both channels — the balance trim
  // lives in the post-chain image matrix above, pre-pan). Smoothed so knob
  // moves glide instead of stepping once per block. Per-channel output
  // meters ride the same pass.
  // ###########
  {
    outputGainSmoother.setTargetValue(mainStageGain(cacheOutputLevel));

    float peakL = 0.0f, peakR = 0.0f;
    auto* l = buffer.getWritePointer(0);
    auto* r = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i) {
      const float g = outputGainSmoother.getNextValue();
      l[i] *= g;
      peakL = std::max(peakL, std::abs(l[i]));
      if (r) {
        r[i] *= g;
        peakR = std::max(peakR, std::abs(r[i]));
      }
    }
    if (numChannels < 2) 
      peakR = peakL;
    outputMeterLevelL.store(peakToDb(peakL));
    outputMeterLevelR.store(peakToDb(peakR));
  }
}

// ##################
// ENABLE EDITOR / UI
// ##################
bool TONE3000Processor::hasEditor() const {
  return true;
}

// ##############
// CREATE EDITOR
// ##############
juce::AudioProcessorEditor* TONE3000Processor::createEditor() {
#if !HEADLESS
  return new TONE3000Editor(*this);
#else
  return nullptr;
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new TONE3000Processor();
}

// #########################
// AUTO BALANCE (one-shot chain energy match)
// #########################
// The workflow is "click =, play for a couple of seconds, done": we measure
// the user's real playing rather than injecting a test signal, because NAM
// chains are nonlinear (a noise burst at an arbitrary level says little about
// how the chains compare under a real pick attack) and a burst would be
// audible at the output. Continuous AGC is deliberately avoided — it would
// chase the player's dynamics instead of correcting a static chain mismatch.
// The tap sits on the raw chain outputs, before the balance/pan matrix, so
// the measured mismatch (and the balance value it produces) is independent
// of the current pan positions.

namespace {
// Signal gate: blocks whose loudest channel is below this RMS don't count
// toward the measurement, so silence between phrases can't dilute it.
constexpr double kAutoBalanceFloorRms = 3.16e-3;  // −50 dBFS
constexpr double kAutoBalanceMeasureSeconds = 2.0;
constexpr double kAutoBalanceTimeoutSeconds = 15.0;
}  // namespace

void TONE3000Processor::startAutoBalance() {
  // Reset is safe from the message thread: the audio thread only touches the
  // accumulators while the state is Listening, and the release-store below
  // publishes the zeroed accumulators together with the state flip.
  autoBalanceSumL = 0.0;
  autoBalanceSumR = 0.0;
  autoBalanceSamples.store(0, std::memory_order_relaxed);
  autoBalanceElapsed.store(0, std::memory_order_relaxed);
  autoBalanceState.store(static_cast<int>(AutoBalanceState::Listening),
                         std::memory_order_release);
}

void TONE3000Processor::cancelAutoBalance() {
  autoBalanceState.store(static_cast<int>(AutoBalanceState::Idle), std::memory_order_release);
}

// Audio thread, on the raw chain outputs (pre-balance/pan), only while
// Listening.
void TONE3000Processor::runAutoBalanceStage(const juce::AudioBuffer<float>& buffer,
                                            int numSamples) {
  autoBalanceElapsed.fetch_add(numSamples, std::memory_order_relaxed);

  if (buffer.getNumChannels() >= 2) {
    const float* l = buffer.getReadPointer(0);
    const float* r = buffer.getReadPointer(1);
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < numSamples; ++i) {
      sumL += static_cast<double>(l[i]) * l[i];
      sumR += static_cast<double>(r[i]) * r[i];
    }

    const double blockRms = std::sqrt(std::max(sumL, sumR) / std::max(1, numSamples));
    if (blockRms > kAutoBalanceFloorRms) {
      autoBalanceSumL += sumL;
      autoBalanceSumR += sumR;
      autoBalanceSamples.fetch_add(numSamples, std::memory_order_relaxed);
    }
  }

  const auto needed =
      static_cast<juce::int64>(kAutoBalanceMeasureSeconds * hostSampleRate);
  if (autoBalanceSamples.load(std::memory_order_relaxed) >= needed) {
    const double energyL = std::max(autoBalanceSumL, 1.0e-12);
    const double energyR = std::max(autoBalanceSumR, 1.0e-12);
    // Positive = left chain louder. The balance trim corrects up to ±12 dB
    // per chain (±24 dB relative), so clamp to what the knob can express.
    autoBalanceMatchedDb = static_cast<float>(
        juce::jlimit(-24.0, 24.0, 10.0 * std::log10(energyL / energyR)));
    autoBalanceState.store(static_cast<int>(AutoBalanceState::Measured),
                           std::memory_order_release);
  } else if (autoBalanceElapsed.load(std::memory_order_relaxed) >
             static_cast<juce::int64>(kAutoBalanceTimeoutSeconds * hostSampleRate)) {
    autoBalanceState.store(static_cast<int>(AutoBalanceState::TimedOut),
                           std::memory_order_release);
  }
}

// Message thread (UI poll). Applying the result here — not on the audio
// thread — keeps setValueNotifyingHost off the RT path and on the thread
// hosts expect parameter gestures from.
juce::var TONE3000Processor::pollAutoBalance() {
  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  const auto state =
      static_cast<AutoBalanceState>(autoBalanceState.load(std::memory_order_acquire));

  switch (state) {
    case AutoBalanceState::Listening: {
      const auto needed =
          static_cast<juce::int64>(kAutoBalanceMeasureSeconds * hostSampleRate);
      const auto samples = autoBalanceSamples.load(std::memory_order_relaxed);
      obj->setProperty("state", "listening");
      obj->setProperty("progress",
                       juce::jlimit(0.0, 1.0, static_cast<double>(samples) /
                                                  static_cast<double>(std::max<juce::int64>(1, needed))));
      break;
    }
    case AutoBalanceState::Measured: {
      // diff dB → knob position: the trim applies ∓diff/2 to the Left chain
      // and ±diff/2 to the Right, and the knob maps (value − 0.5) · 24 to
      // the per-chain trim dB (see balanceChainGain).
      const float diffDb = autoBalanceMatchedDb;
      const float balance = juce::jlimit(0.0f, 1.0f, 0.5f + diffDb / 48.0f);
      if (auto* param = parameters.getParameter("outputBalance")) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(balance);
        param->endChangeGesture();
      }
      autoBalanceState.store(static_cast<int>(AutoBalanceState::Idle),
                             std::memory_order_release);
      obj->setProperty("state", "done");
      obj->setProperty("matchedDb", diffDb);
      juce::Logger::writeToLog("[AutoBalance] Matched L/R (diff " +
                               juce::String(diffDb, 2) + " dB)");
      break;
    }
    case AutoBalanceState::TimedOut:
      autoBalanceState.store(static_cast<int>(AutoBalanceState::Idle),
                             std::memory_order_release);
      obj->setProperty("state", "timeout");
      break;
    case AutoBalanceState::Idle:
      obj->setProperty("state", "idle");
      break;
  }
  return juce::var(obj.get());
}

// #########################
// AUTO OFFSET (one-shot chain time alignment, stereo chain mode)
// #########################
// Same "click, play for a couple of seconds, done" flow as auto balance and
// for the same reasons (see the auto-balance section above); the measurement
// itself — capture, silence gating, timeout, FFT cross-correlation — lives in
// AutoOffset (AutoOffset.h). The processor's share is the audio tap in
// processBlock (pre-offset, stereo branch) and applying the result to the
// host parameters here on the message thread.

namespace {
// Below this normalized peak correlation the measurement is noise — e.g. the
// true misalignment is beyond the ±24 ms the knob can express, or the chains
// were fed unrelated audio. Reject instead of setting a junk offset. Related
// chain outputs correlate far above this; unrelated ones peak near 0.
constexpr float kAutoOffsetMinConfidence = 0.15f;
// Lags under this are already aligned for any practical purpose (well under
// a sample's worth of imaging); don't power the offset on over nothing.
constexpr float kAutoOffsetSilentMs = 0.05f;
}  // namespace

void TONE3000Processor::startAutoOffset() { autoOffset.start(); }

void TONE3000Processor::cancelAutoOffset() { autoOffset.cancel(); }

// Message thread (UI poll). The analysis (a one-shot FFT over the 2 s
// capture) also runs here, never on the audio thread.
juce::var TONE3000Processor::pollAutoOffset() {
  juce::DynamicObject::Ptr obj = new juce::DynamicObject();

  switch (autoOffset.state()) {
    case AutoOffset::State::Listening:
      obj->setProperty("state", "listening");
      obj->setProperty("progress", static_cast<double>(autoOffset.progress()));
      break;
    case AutoOffset::State::Captured: {
      const auto result = autoOffset.analyze();
      if (result.confidence < kAutoOffsetMinConfidence) {
        obj->setProperty("state", "timeout");
        juce::Logger::writeToLog("[AutoOffset] Rejected: peak correlation " +
                                 juce::String(result.confidence, 3) +
                                 " (misalignment out of range or unrelated chains)");
        break;
      }
      // ms → knob position, the StereoOffsetParams::fromNormalized inverse:
      // (value − 0.5) · 2 · 24 ms, positive = right chain delayed.
      const float norm = juce::jlimit(
          0.0f, 1.0f, 0.5f + result.offsetMs / (2.0f * StereoOffsetParams::kMaxOffsetMs));
      if (auto* param = parameters.getParameter("stereoOffsetTime")) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(norm);
        param->endChangeGesture();
      }
      // Power the offset on when there's a real correction to hear. An
      // effectively-zero result still rewrites the time (clearing a stale
      // knob value) but leaves the power switch alone.
      if (std::abs(result.offsetMs) >= kAutoOffsetSilentMs) {
        if (auto* param = parameters.getParameter("stereoOffsetEnabled")) {
          param->beginChangeGesture();
          param->setValueNotifyingHost(1.0f);
          param->endChangeGesture();
        }
      }
      obj->setProperty("state", "done");
      obj->setProperty("matchedMs", result.offsetMs);
      juce::Logger::writeToLog("[AutoOffset] Aligned chains (offset " +
                               juce::String(result.offsetMs, 2) + " ms, confidence " +
                               juce::String(result.confidence, 3) + ")");
      break;
    }
    case AutoOffset::State::TimedOut:
      autoOffset.cancel();
      obj->setProperty("state", "timeout");
      break;
    case AutoOffset::State::Idle:
      obj->setProperty("state", "idle");
      break;
  }
  return juce::var(obj.get());
}

// #########################
// DIAGNOSTIC LOG FILE
// #########################
// Mirrors juce::FileLogger::createDefaultAppLogger's path so the logger and the
// UI's copy/reveal actions always target the same file.
juce::File TONE3000Processor::getLogFile() {
  return juce::FileLogger::getSystemLogFileFolder()
      .getChildFile("TONE3000")
      .getChildFile("TONE3000.log");
}