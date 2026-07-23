#include "Processor.h"
#if !HEADLESS
#include "Editor.h"
#endif
#include <cmath>
#include <random>
#include <cstring>

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
      loadingThreadPool(2) {  // 2 threads for background loading
  // Attach the file logger first thing: state restore (and the background
  // model loads it queues) runs before prepareToPlay, and its diagnostics
  // used to vanish because the logger didn't exist yet.
  if (!juce::Logger::getCurrentLogger()) {
    juce::Logger::setCurrentLogger(new juce::FileLogger(getLogFile(), "TONE3000 JUCE Log"));
  }

  resolveParamRefs();
  // Every lane starts at its minimum slot layout (kMinLaneSlots pass-through
  // insert placeholders). The right lane stays invisible until stereo mode is
  // enabled, but seeding it now keeps the invariant unconditional.
  for (auto& l : lanes)
    normalizeLaneInserts(l);
  // Built once (capturing only `this`) so invoking the boundary on the audio
  // thread never constructs a std::function per block.
  chainStageFunc = [this](float** inputs, float** outputs, int numFrames) {
    processChainStage(inputs, outputs, numFrames);
  };
  DBG("TONE3000Processor constructed");
}

// One-time string-keyed lookups; everything after this reads the atomics.
void TONE3000Processor::resolveParamRefs() {
  auto get = [this](const char* id) { return parameters.getRawParameterValue(id); };
  paramRefs.inputLevel = get("inputLevel");
  paramRefs.outputLevel = get("outputLevel");
  paramRefs.inputBalance = get("inputBalance");
  paramRefs.outputBalance = get("outputBalance");
  paramRefs.spreadEnabled = get("spreadEnabled");
  paramRefs.spreadAmount = get("spreadAmount");
  paramRefs.spreadJitter = get("spreadJitter");
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

  // Stereo balance trims for the main gains: 0.5 = centered (no effect),
  // otherwise an opposing ±12 dB trim between L and R on top of the main
  // level. Output balance only applies in stereo mode or mono+spread (see
  // processBlock); the UI hides the knobs when inactive.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"inputBalance", 17}, "inputBalance", 0.0f, 1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"outputBalance", 18}, "outputBalance", 0.0f, 1.0f, 0.5f));

  // Spread (see Spread.h): one parameter set for both modes (mono double /
  // stereo chain shift — mode-exclusive). Amount is bipolar: 0.5 = center =
  // 0 ms (processing skipped); below center delays the left channel, above
  // center the right (0..24 ms). Jitter is ± per-note random variation
  // (0..4 ms, 0 = off). Stored normalized; SpreadParams decodes to ms.
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"spreadEnabled", 19}, "spreadEnabled", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"spreadAmount", 20}, "spreadAmount", 0.0f, 1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"spreadJitter", 21}, "spreadJitter", 0.0f, 1.0f, 0.0f));

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

  return layout;
}

TONE3000Processor::~TONE3000Processor() {
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
  return false;
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
  //    below the DC-blocker floor). Tracked lock-free in irTailChainSamples —
  //    this (potentially RT-adjacent) query must not take the chain lock —
  //    and refreshed wherever the set of live IR engines changes (see
  //    refreshIrTailLength).
  //  - The 5 Hz first-order DC blocker decays over ~10 cycles (2 s); the
  //    reference NAM plugin reports the same allowance for VST3 tail checks.
  const double irTailSeconds = irTailChainSamples.load() / kChainSampleRate;
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
// Everything in a chain lives in the chain domain: fixed kChainSampleRate,
// block sizes up to chainDomainBlockSize(). Host-rate changes only ever
// re-prepare because the domain block size depends on the host block size.
void TONE3000Processor::prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks) {
  const int domainBlockSize = chainDomainBlockSize();

  for (auto& block : blocks) {
    if (block->type == ChainBlockType::NAM) {
      if (block->namEngine != nullptr) {
        block->namEngine->prepare(domainBlockSize);
        DBG("NAM engine prepared for block: " << block->id);
      } else {
        DBG("Warning: NAM block " << block->id << " has no engine to prepare");
      }
    } else if (block->type == ChainBlockType::IR && block->convolverMono != nullptr) {
      juce::dsp::ProcessSpec spec{kChainSampleRate, static_cast<juce::uint32>(domainBlockSize), 2};
      block->convolverMono->prepare(spec);
      if (block->convolverStereo != nullptr)
        block->convolverStereo->prepare(spec);

      // Reset normalization smoother to current gain to prevent jumps on re-prepare
      if (block->loaded) {
        block->irNormalizationSmoother.reset(kChainSampleRate, 0.05f);
        block->irNormalizationSmoother.setCurrentAndTargetValue(block->irNormalizationGainLinear);
      }

      DBG("IR convolvers re-prepared for block: " << block->id);
    }

    // Initialize per-block smoothers (input gain, output gain, mix, NAM
    // normalization). The RT path only ever calls setTargetValue on these —
    // reset() belongs here and in the model-apply path, never per block.
    block->inputGainSmoother.reset(kChainSampleRate, 0.05f);
    block->outputGainSmoother.reset(kChainSampleRate, 0.05f);
    block->mixSmoother.reset(kChainSampleRate, 0.05f);
    block->namNormalizationSmoother.reset(kChainSampleRate, 0.05f);
    block->inputGainSmoother.setCurrentAndTargetValue(1.0f);   // updated on first process
    block->outputGainSmoother.setCurrentAndTargetValue(1.0f);  // updated on first process
    block->mixSmoother.setCurrentAndTargetValue(block->mixNormalized);
    block->namNormalizationSmoother.setCurrentAndTargetValue(1.0f);
    block->wetFadeGain.reset(kChainSampleRate, kWetFadeSeconds);
    block->wetFadeGain.setCurrentAndTargetValue(block->enabled ? 1.0f : 0.0f);

    // Per-block EQ + spectrum analyzer need the sample rate for their math.
    block->eq.prepare(kChainSampleRate);
    block->spectrum.prepare(kChainSampleRate);
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
        maxSamples = std::max(maxSamples, b->irLengthChainSamples);
  irTailChainSamples.store(maxSamples);
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

// Per-channel gain for a main stage: main level ±24 dB, plus the balance
// trim (0.5 = centered) which adds up to ±12 dB opposing between L and R.
// Mono buffers pass ch == 0 only, so balance is inert there by construction.
static float mainStageChannelGain(float level, float balance, int channel) {
  const float levelDb = (level - 0.5f) * 48.0f;
  const float trimDb = (balance - 0.5f) * 24.0f * (channel == 0 ? -1.0f : 1.0f);
  return juce::Decibels::decibelsToGain(levelDb + trimDb);
}

// Stereo input = stereo main bus, minus the standalone cases where it isn't
// really: a mono input device, or the input mode set to a single channel.
// The UI shows dual input meters + balance when set; reported through
// getChainState, so bump the revision on change.
void TONE3000Processor::updateStereoInputDetection() {
  bool stereoIn = getMainBusNumInputChannels() >= 2 && !standaloneMonoInput.load();
  if (isStandalone() &&
      standaloneInputMode.load() != static_cast<int>(InputMode::Stereo))
    stereoIn = false;
  if (stereoInputDetected.exchange(stereoIn) != stereoIn)
    bumpChainRevision();
}

void TONE3000Processor::setStandaloneInputMode(InputMode mode) {
  standaloneInputMode.store(static_cast<int>(mode));
  updateStereoInputDetection();
  bumpChainRevision();
  DBG("Standalone input mode: " << inputModeToString(mode));
}

// #############################
// PREPARATIONS BEFORE RT THREAD
// #############################
void TONE3000Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  hostSampleRate = sampleRate;
  maxBlockSize = samplesPerBlock;

  tuner.prepare(sampleRate);

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
  // Engaged whenever the host rate differs from the fixed chain rate — even
  // for an empty chain, so reported latency is a constant per host rate and
  // chain edits never trigger a PDC change. At a 48k host the boundary is
  // dropped entirely and the chain stage runs directly on the host buffer.
  const bool boundaryNeeded = std::abs(sampleRate - kChainSampleRate) > 0.1;
  if (boundaryNeeded) {
    if (chainBoundary == nullptr)
      chainBoundary = std::make_unique<ChainBoundaryResampler>(kChainSampleRate);
    chainBoundary->Reset(sampleRate, juce::jmax(1, samplesPerBlock));
    chainBoundaryLatency = chainBoundary->GetLatency();
  } else {
    chainBoundary.reset();
    chainBoundaryLatency = 0;
  }
  setLatencySamples(chainBoundaryLatency);
  DBG("Chain boundary " << (boundaryNeeded ? "engaged" : "bypassed")
      << " (latency: " << chainBoundaryLatency << " samples)");

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
  inputGate.prepare(sampleRate);

  // Chain-edit fade (reorder / cross-lane move): host-rate, primed audible.
  chainEditFadeGain.reset(sampleRate, kWetFadeSeconds);
  chainEditFadeGain.setCurrentAndTargetValue(1.0f);

  // Spread mono-double blend: primed at "no double"; the first running
  // spread block ramps it in (see the post-chain stereo image stage).
  spreadDoubleBlend.reset(sampleRate, kWetFadeSeconds);
  spreadDoubleBlend.setCurrentAndTargetValue(0.0f);

  // Output-stage gains, primed from the current parameters so a restored
  // session doesn't glide in from the wrong level.
  {
    const bool applyOutputBalance = stereoEnabled.load() || cacheSpreadEnabled;
    const float outputBalance = applyOutputBalance ? cacheOutputBalance : 0.5f;
    outputGainSmootherL.reset(sampleRate, 0.02);
    outputGainSmootherR.reset(sampleRate, 0.02);
    outputGainSmootherL.setCurrentAndTargetValue(
        mainStageChannelGain(cacheOutputLevel, outputBalance, 0));
    outputGainSmootherR.setCurrentAndTargetValue(
        mainStageChannelGain(cacheOutputLevel, outputBalance, 1));
  }

  // Chain-pan blend gains: 20 ms ramps, primed from the current parameters
  // so a restored session doesn't fade in from the wrong image.
  {
    const auto [gLtoL, gLtoR] = constantPowerPanGains(cacheChainPanLeft);
    const auto [gRtoL, gRtoR] = constantPowerPanGains(cacheChainPanRight);
    for (auto* smoother : {&panGainLtoL, &panGainLtoR, &panGainRtoL, &panGainRtoR})
      smoother->reset(sampleRate, 0.02);
    panGainLtoL.setCurrentAndTargetValue(gLtoL);
    panGainLtoR.setCurrentAndTargetValue(gLtoR);
    panGainRtoL.setCurrentAndTargetValue(gRtoL);
    panGainRtoR.setCurrentAndTargetValue(gRtoR);
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
  // tempDryBuffer lives in the chain domain, where a callback can carry more
  // frames than the host block (e.g. a 44.1k host upsampled to 48k).
  tempDryBuffer.setSize(2, chainDomainBlockSize(), false, false, true);
  tempDryBuffer.clear();
  chainScratchChannel.setSize(1, samplesPerBlock, false, false, true);
  chainScratchChannel.clear();

  // Apply the actual tone knob gains to the tone stack filters.
  updateEqCoefficients();
}

// #################
// RELEASE RESOURCES
// #################
void TONE3000Processor::releaseResources() {
  juce::Logger::writeToLog("[Processor] releaseResources() called");

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
  updateFloat(cacheInputBalance, paramRefs.inputBalance);
  updateFloat(cacheOutputBalance, paramRefs.outputBalance);
  updateFloat(cacheSpreadAmount, paramRefs.spreadAmount);
  updateFloat(cacheSpreadJitter, paramRefs.spreadJitter);
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
}

// ##########################
// RT PROCESS A SINGLE CHAIN
// ##########################
// Runs the per-block chain loop on `buffer`. The buffer may be mono (a single side in stereo
// mode) or 1-2 channels (mono mode). All per-channel work is keyed on buffer.getNumChannels().
// Must be called while holding chainMutex.
void TONE3000Processor::processChainOnBuffer(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                             juce::AudioBuffer<float>& buffer) {
  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  // Highest index of an enabled+loaded NAM block. Used twice: a stereo IR is only worth
  // processing in true stereo when there is no NAM downstream to collapse the image back to
  // mono, and NAM blocks *before* this index hand off at calibrated output level (see the
  // post-model gain stage below) while the last one keeps loudness normalization.
  int lastNamIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    const auto& b = blocks[i];
    if (b->type == ChainBlockType::NAM && b->loaded && b->enabled)
      lastNamIndex = i;
  }

  for (int idx = 0; idx < static_cast<int>(blocks.size()); ++idx) {
    const auto& block = blocks[idx];
    if (block->type == ChainBlockType::INSERT) {
      continue;  // Insert block is pass-through, no audio effect
    }

    // Wet-path fade (see ChainBlock.h): power toggles and pending engine
    // swaps glide the block's wet mix to/from bypass instead of splicing the
    // waveform. A disabled block keeps processing until the glide reaches
    // bypass, then is skipped exactly like before.
    const bool wantsWet = block->enabled && !block->swapFadePending.load();
    block->wetFadeGain.setTargetValue(block->loaded && wantsWet ? 1.0f : 0.0f);
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

    // Prepare dry copy before processing for mix (reuse temp buffer)
    jassert(tempDryBuffer.getNumChannels() >= numChannels);
    jassert(tempDryBuffer.getNumSamples() >= numSamples);
    tempDryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1) {
      tempDryBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);
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
        jassert(numSamples <= tempDryBuffer.getNumSamples());

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
        buffer.copyFrom(0, 0, tempDryBuffer, 0, 0, numSamples);
        if (numChannels > 1) {
          buffer.copyFrom(1, 0, tempDryBuffer, 1, 0, numSamples);
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

        juce::dsp::AudioBlock<float> irBlock(buffer);
        auto& convolver = useStereoIr ? *block->convolverStereo : *block->convolverMono;
        convolver.process(juce::dsp::ProcessContextReplacing<float>(irBlock));

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
      const float g = block->outputGainSmoother.getNextValue();
      // wetFadeGain rides the mix so every block transition (swap, power
      // toggle, add/remove) glides through bypass — see ChainBlock.h.
      const float m = block->mixSmoother.getNextValue() * block->wetFadeGain.getNextValue();
      float wetL = buffer.getWritePointer(0)[i] * g;
      float dryL = tempDryBuffer.getReadPointer(0)[i];
      buffer.getWritePointer(0)[i] = dryL * (1.0f - m) + wetL * m;
      blockOutputPeak = std::max(blockOutputPeak, std::abs(buffer.getWritePointer(0)[i]));
      if (numChannels > 1) {
        float wetR = buffer.getWritePointer(1)[i] * g;
        float dryR = tempDryBuffer.getReadPointer(1)[i];
        buffer.getWritePointer(1)[i] = dryR * (1.0f - m) + wetR * m;
        blockOutputPeak = std::max(blockOutputPeak, std::abs(buffer.getWritePointer(1)[i]));
      }
    }

    // Fade handshake: tell a waiting requester the wet path is fully
    // bypassed and its change (swap/removal) can splice in silently.
    if (block->swapFadePending.load() && !block->wetFadeGain.isSmoothing() &&
        block->wetFadeGain.getCurrentValue() <= 0.001f)
      block->swapFadeDone.store(true);

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

// #########################
// RT CHAIN STAGE (48 kHz)
// #########################
// The encapsulated side of the chain-domain boundary: lane L (and lane R in
// stereo mode) over the given channel pointers. Invoked by the boundary
// resampler at 48 kHz, or directly on the host buffer when the host already
// runs at 48 kHz. Called with chainMutex held (processBlock takes it).
void TONE3000Processor::processChainStage(float** inputs, float** outputs, int numFrames) {
  // The boundary hands us distinct input/output buffers; the chain processes
  // in place, so move the audio to the output side first. On the direct path
  // the pointers alias and the copies are skipped.
  for (int ch = 0; ch < 2; ++ch) {
    if (outputs[ch] != inputs[ch])
      std::memcpy(outputs[ch], inputs[ch], sizeof(float) * static_cast<size_t>(numFrames));
  }

  if (rtStereoChains) {
    // Stereo mode: each channel is an independent mono lane, processed in
    // place — no split/merge copies needed.
    float* left[] = {outputs[0]};
    float* right[] = {outputs[1]};
    juce::AudioBuffer<float> bufferL(left, 1, numFrames);
    juce::AudioBuffer<float> bufferR(right, 1, numFrames);
    processChainOnBuffer(lane(ChainSide::Left), bufferL);
    processChainOnBuffer(lane(ChainSide::Right), bufferR);
  } else {
    juce::AudioBuffer<float> chainBuffer(outputs, rtChainChannels, numFrames);
    processChainOnBuffer(lane(ChainSide::Left), chainBuffer);
  }
}

// ################
// RT PROCESS BLOCK
// ################
void TONE3000Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(midi);

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

  // Standalone input fold-down, up front so everything downstream (meters,
  // tuner, chains, spread detection) sees the real source:
  // - Mono input device: signal only arrives on channel 0 — mirror it.
  // - Stereo device with a mono input mode selected (guitar in one jack of a
  //   line 1+2 pair): duplicate the chosen channel onto both, exactly like a
  //   host feeding a mono source to a stereo bus.
  if (numChannels > 1) {
    if (standaloneMonoInput.load()) {
      buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    } else if (isStandalone()) {
      const auto mode = static_cast<InputMode>(standaloneInputMode.load());
      if (mode == InputMode::Input1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
      else if (mode == InputMode::Input2)
        buffer.copyFrom(0, 0, buffer, 1, 0, numSamples);
    }
  }

  // #########################
  // Input gain + noise gate
  // #########################

  // Per-channel input gains (level ±24 dB, balance trim ±12 dB opposing),
  // constant across the block. Computed up front so the meters can show the
  // post-gain level without a second pass over the samples.
  const float inputGainL = mainStageChannelGain(cacheInputLevel, cacheInputBalance, 0);
  const float inputGainR = mainStageChannelGain(cacheInputLevel, cacheInputBalance, 1);

  // Per-channel input meters: raw peaks scaled by the input gain/balance, so
  // the meter tracks those knobs. Pre-gate on purpose — a closed gate would
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
    inputMeterLevelL.store(peakToDb(peakL * inputGainL));
    inputMeterLevelR.store(peakToDb(peakR * (numChannels > 1 ? inputGainR : inputGainL)));
  }

  // Feed the tuner from the raw input (pre-gain, pre-gate) while the tuner
  // screen is open. Channel 0 only: guitar sources are mono, and mixing
  // channels risks phase cancellation.
  if (tuner.isEnabled())
    tuner.pushSamples(buffer.getReadPointer(0), numSamples);

  // Apply the input gain per channel (vectorized).
  for (int ch = 0; ch < numChannels; ++ch)
    buffer.applyGain(ch, 0, numSamples, ch == 0 ? inputGainL : inputGainR);

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

  // Spread parameter sync + onset analysis, pre-chain: the detector must see
  // the raw picked signal (post-gain/gate) — amp/IR processing compresses
  // pick attacks into mush and its noise floor fakes constant onsets. The
  // delay itself runs post-chain (below).
  spread.setTarget(SpreadParams::fromNormalized(cacheSpreadAmount, cacheSpreadJitter),
                   cacheSpreadEnabled && numChannels >= 2);
  if (spread.isRunning())
    spread.analyzeOnsets(buffer.getReadPointer(0), numSamples);

  // ####################
  // MODULAR CHAIN PROCESSING (chain domain — fixed 48 kHz, see ChainDomain.h)
  // ####################
  {
    juce::ScopedLock lock(chainMutex);

    rtStereoChains = stereoEnabled.load() && numChannels >= 2;
    rtChainChannels = juce::jmin(numChannels, 2);

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
        processChainStage(channels, channels, sliceLen);
    }
  }

  // ##########
  // Chain-edit fade (see ChainEditFade): structural edits that can't be
  // expressed as one block's wet fade (reorder, cross-lane move) glide the
  // whole chain output to silence, splice the edit in between callbacks,
  // and glide back. Idle cost: one atomic load + one branch.
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
  // Post-chain stereo image, before the downstream stereo stages (DC / tone
  // stack / output balance + meters) so they see the real image. Order
  // matters: spread runs first so the delay always shifts one full chain,
  // then the pan blend mixes the (possibly shifted) chains.
  //  - Spread (both modes, one engine): delays the chosen side. Mono seeds
  //    ch 1 with the chain output first (the classic double); stereo delays
  //    that side's chain in place. Runs whenever the power switch is on
  //    (0 ms = identity, transitions glide through zero — see Spread.h);
  //    fully skipped once the glide-out after power-off completes.
  //  - Stereo chain pan: constant-power blend of the two chains across the
  //    output bus. Hard-panned defaults are the identity and skip the loop.
  // ##########
  {
    const bool isStereo = stereoEnabled.load() && numChannels >= 2;

    // Mono-double seed, crossfaded: with a true stereo source the two
    // channels carry different chain output, so the spread power toggle
    // glides channel 1 between its own signal and the channel-0 double
    // instead of hard-copying it (pop). Settled at 1 it's the plain copy;
    // settled at 0 it costs nothing. (For mono sources ch0 == ch1 and the
    // blend is inert either way.)
    if (!isStereo && numChannels >= 2) {
      spreadDoubleBlend.setTargetValue(cacheSpreadEnabled ? 1.0f : 0.0f);
      if (spreadDoubleBlend.isSmoothing()) {
        const auto* l = buffer.getReadPointer(0);
        auto* r = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i) {
          const float b = spreadDoubleBlend.getNextValue();
          r[i] += (l[i] - r[i]) * b;
        }
      } else if (spreadDoubleBlend.getCurrentValue() >= 0.999f) {
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
      }
    } else {
      // Stereo mode owns both channels; park the blend so the next mono use
      // starts from the right resting point.
      spreadDoubleBlend.setCurrentAndTargetValue(cacheSpreadEnabled ? 1.0f : 0.0f);
    }

    if (spread.isRunning())
      spread.process(buffer);

    if (isStereo) {
      const auto [gLtoL, gLtoR] = constantPowerPanGains(cacheChainPanLeft);
      const auto [gRtoL, gRtoR] = constantPowerPanGains(cacheChainPanRight);
      panGainLtoL.setTargetValue(gLtoL);
      panGainLtoR.setTargetValue(gLtoR);
      panGainRtoL.setTargetValue(gRtoL);
      panGainRtoR.setTargetValue(gRtoR);

      const bool smoothing = panGainLtoL.isSmoothing() || panGainLtoR.isSmoothing() ||
                             panGainRtoL.isSmoothing() || panGainRtoR.isSmoothing();
      const bool identity = gLtoL >= 0.9999f && gRtoR >= 0.9999f;
      if (smoothing || !identity) {
        auto* l = buffer.getWritePointer(0);
        auto* r = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i) {
          const float ll = panGainLtoL.getNextValue();
          const float lr = panGainLtoR.getNextValue();
          const float rl = panGainRtoL.getNextValue();
          const float rr = panGainRtoR.getNextValue();
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
  // EQ section (global 3-band tone stack), post-chain.
  // ##########
  processToneStack(buffer);

  // ##########
  // Auto balance listening pass: accumulate L/R energy right before the
  // output stage, so the measured difference is exactly what the balance
  // trim must correct. Zero work unless a measurement is armed.
  // ##########
  if (autoBalanceState.load(std::memory_order_acquire) ==
      static_cast<int>(AutoBalanceState::Listening))
    runAutoBalanceStage(buffer, numSamples);

  // ###########
  // Output gain per channel (level ±24 dB, balance trim ±12 dB opposing).
  // Balance only applies when the output image is actually stereo: stereo
  // mode, or mono mode with spread powered on. Otherwise force center so a
  // leftover Bal setting from a prior mono+spread session can't skew a
  // mono (identical L/R) bus — matches the UI hiding the Bal knob.
  // The gains are smoothed so knob moves AND the balance on/off gating
  // (spread/stereo toggles) glide instead of stepping (pop). Per-channel
  // output meters ride the same pass.
  // ###########
  {
    const bool applyOutputBalance = stereoEnabled.load() || cacheSpreadEnabled;
    const float outputBalance = applyOutputBalance ? cacheOutputBalance : 0.5f;
    outputGainSmootherL.setTargetValue(mainStageChannelGain(cacheOutputLevel, outputBalance, 0));
    outputGainSmootherR.setTargetValue(mainStageChannelGain(cacheOutputLevel, outputBalance, 1));

    float peakL = 0.0f, peakR = 0.0f;
    auto* l = buffer.getWritePointer(0);
    auto* r = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i) {
      l[i] *= outputGainSmootherL.getNextValue();
      peakL = std::max(peakL, std::abs(l[i]));
      const float gainR = outputGainSmootherR.getNextValue();
      if (r) {
        r[i] *= gainR;
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
// AUTO BALANCE (one-shot L/R energy match)
// #########################
// The workflow is "click =, play for a couple of seconds, done": we measure
// the user's real playing rather than injecting a test signal, because NAM
// chains are nonlinear (a noise burst at an arbitrary level says little about
// how the chains compare under a real pick attack) and a burst would be
// audible at the output. Continuous AGC is deliberately avoided — it would
// chase the player's dynamics instead of correcting a static chain mismatch.

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

// Audio thread, pre-output-gain, only while Listening.
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
    // Positive = left louder. The balance trim corrects up to ±12 dB per
    // channel (±24 dB relative), so clamp to what the knob can express.
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
      // diff dB → knob position: the trim applies ∓diff/2 to L and ±diff/2 to
      // R, and the knob maps (value − 0.5) · 24 to the per-channel trim dB.
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
// DIAGNOSTIC LOG FILE
// #########################
// Mirrors juce::FileLogger::createDefaultAppLogger's path so the logger and the
// UI's copy/reveal actions always target the same file.
juce::File TONE3000Processor::getLogFile() {
  return juce::FileLogger::getSystemLogFileFolder()
      .getChildFile("TONE3000")
      .getChildFile("TONE3000.log");
}