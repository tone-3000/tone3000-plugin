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
      oversampler(std::make_unique<juce::dsp::Oversampling<float>>(
          2,
          0,
          juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple)),
      loadingThreadPool(2) {  // 2 threads for background loading
  normalizationGainSmoother.reset(48000, 0.05f);
  // Always start with the insert block (pass-through placeholder for "add tone" position)
  chainBlocks.push_back(
      std::make_unique<ChainBlock>(INSERT_BLOCK_ID, ChainBlockType::INSERT));
  DBG("TONE3000Processor constructed");
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
      juce::ParameterID{"normalize", 8}, "normalize", true));
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
  // level. Only audible on stereo buffers; the UI hides the knobs otherwise.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"inputBalance", 17}, "inputBalance", 0.0f, 1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"outputBalance", 18}, "outputBalance", 0.0f, 1.0f, 0.5f));

  // Spread (see Spread.h): one parameter set for both modes (mono double /
  // stereo chain shift — mode-exclusive). Amount is bipolar: 0.5 = center =
  // 0 ms (processing skipped); below center delays the left channel, above
  // center the right (0..24 ms). Jitter is ± per-note random variation
  // (0..12 ms, 0 = off). Stored normalized; SpreadParams decodes to ms.
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
  
  // Clean up chain blocks only when the processor is actually being destroyed
  {
    juce::ScopedLock lock(chainMutex);
    chainBlocks.clear();
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
  return 0.0;
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
void TONE3000Processor::prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                     double sampleRate, int samplesPerBlock) {
  for (auto& block : blocks) {
    if (block->type == ChainBlockType::NAM) {
      if (block->namResampler != nullptr) {
        block->namResampler->prepare(sampleRate, samplesPerBlock);
        block->latencySamples = block->namResampler->getLatencySamples();
        DBG("Resampling NAM prepared for block: " << block->id
            << " (latency: " << block->latencySamples << " samples)");
      } else {
        DBG("Warning: NAM block " << block->id << " has no resampling wrapper to prepare");
      }
    } else if (block->type == ChainBlockType::IR && block->convolverMono != nullptr) {
      juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2};
      block->convolverMono->prepare(spec);
      if (block->convolverStereo != nullptr)
        block->convolverStereo->prepare(spec);

      // Reset normalization smoother to current gain to prevent jumps on sample rate change
      if (block->loaded) {
        block->irNormalizationSmoother.reset(sampleRate, 0.05f);
        block->irNormalizationSmoother.setCurrentAndTargetValue(block->irNormalizationGainLinear);
      }

      DBG("IR convolvers re-prepared for block: " << block->id);
    }

    // Initialize per-block smoothers (input gain, output gain and mix)
    block->inputGainSmoother.reset(sampleRate, 0.05f);
    block->outputGainSmoother.reset(sampleRate, 0.05f);
    block->mixSmoother.reset(sampleRate, 0.05f);
    block->inputGainSmoother.setCurrentAndTargetValue(1.0f);   // updated on first process
    block->outputGainSmoother.setCurrentAndTargetValue(1.0f);  // updated on first process
    block->mixSmoother.setCurrentAndTargetValue(block->mixNormalized);

    // Post-block EQ + spectrum analyzer need the sample rate for their math.
    block->eq.prepare(sampleRate);
    block->spectrum.prepare(sampleRate);
  }
}

// Constant-power pan gains for a chain at position `pan` (0 = hard left,
// 1 = hard right): cos into the left output, sin into the right.
static std::pair<float, float> constantPowerPanGains(float pan) {
  const float angle = juce::jlimit(0.0f, 1.0f, pan) * juce::MathConstants<float>::halfPi;
  return {std::cos(angle), std::sin(angle)};
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
  // Set up JUCE logger to write to file (only if not already set). Logs go to a
  // user-discoverable location (see getLogFile) so users can send them for
  // debugging via the Settings → Diagnostics actions.
  if (!juce::Logger::getCurrentLogger()) {
    juce::Logger::setCurrentLogger(
        new juce::FileLogger(getLogFile(), "TONE3000 JUCE Log"));
  }

  resampleInputBuffer.resize(samplesPerBlock + 16);
  resampleOutputBuffer.resize(samplesPerBlock + 16);
  modelSampleRate = sampleRate;
  hostSampleRate = sampleRate;  // Update host sample rate
  maxBlockSize = samplesPerBlock;
  inputBuffer.resize(maxBlockSize);
  outputBuffer.resize(maxBlockSize);

  tuner.prepare(sampleRate);

  DBG("Preparing to play: sampleRate=" << sampleRate << ", samplesPerBlock=" << samplesPerBlock);
  DBG("Model sample rate set to: " << modelSampleRate);

  // Cached parameters
  cacheInputLevel = parameters.getRawParameterValue("inputLevel")->load();
  cacheOutputLevel = parameters.getRawParameterValue("outputLevel")->load();
  cacheInputBalance = parameters.getRawParameterValue("inputBalance")->load();
  cacheOutputBalance = parameters.getRawParameterValue("outputBalance")->load();
  cacheSpreadEnabled = parameters.getRawParameterValue("spreadEnabled")->load() > 0.5f;
  cacheSpreadAmount = parameters.getRawParameterValue("spreadAmount")->load();
  cacheSpreadJitter = parameters.getRawParameterValue("spreadJitter")->load();
  cacheChainPanLeft = parameters.getRawParameterValue("chainPanLeft")->load();
  cacheChainPanRight = parameters.getRawParameterValue("chainPanRight")->load();
  cacheBassTone = parameters.getRawParameterValue("toneBass")->load();
  cacheMidTone = parameters.getRawParameterValue("toneMid")->load();
  cacheTrebleTone = parameters.getRawParameterValue("toneTreble")->load();
  cacheGateThreshold = parameters.getRawParameterValue("gateThreshold")->load();
  cacheGateEnabled = parameters.getRawParameterValue("gateEnabled")->load() > 0.5f;
  cacheToneEqEnabled = parameters.getRawParameterValue("toneEqEnabled")->load() > 0.5f;
  cacheTargetLoudness = parameters.getRawParameterValue("targetLoudness")->load();
  DBG("Debug parameter values: gateThreshold=" << cacheGateThreshold << ", targetLoudness=" << cacheTargetLoudness);
  cacheNormalize = parameters.getRawParameterValue("normalize")->load() > 0.5f;
  cacheCalibrateInput = parameters.getRawParameterValue("calibrateInput")->load() > 0.5f;
  cacheInputCalibrationLevel = parameters.getRawParameterValue("inputCalibrationLevel")->load();
  DBG("Input calibration level loaded directly: " << cacheInputCalibrationLevel << " dBu");
  
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

  // Reset all chain blocks (both Left and Right chains)
  {
    juce::ScopedLock lock(chainMutex);
    prepareChain(chainBlocks, sampleRate, samplesPerBlock);
    prepareChain(rightChainBlocks, sampleRate, samplesPerBlock);
  }

  juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2};
  bassFilter.prepare(spec);
  midFilter.prepare(spec);
  trebleFilter.prepare(spec);
  dcBlockerLeft.prepare(spec);
  spread.prepare(sampleRate, samplesPerBlock);

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
  *dcBlockerLeft.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);

  bassFilter.reset();
  midFilter.reset();
  trebleFilter.reset();
  dcBlockerLeft.reset();

  // Oversampling prep
  oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
      2, 0, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
  oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));
  oversampler->reset();

  oversampleBuffer.setSize(2, samplesPerBlock + 8, false, false, true);
  oversampleBuffer.clear();
  tempDryBuffer.setSize(2, samplesPerBlock, false, false, true);
  tempDryBuffer.clear();

  // Pre-allocated mono scratch buffers for per-side processing in stereo mode.
  stereoChainBufferL.setSize(1, samplesPerBlock, false, false, true);
  stereoChainBufferR.setSize(1, samplesPerBlock, false, false, true);
  stereoChainBufferL.clear();
  stereoChainBufferR.clear();

  normalizationGainSmoother.reset(sampleRate, 0.05f);
  normalizationGainSmoother.setCurrentAndTargetValue(1.0f);  // ✅ CRITICAL

  setLatencySamples(bypassResampling ? 0 : static_cast<int>(oversampler->getLatencyInSamples()));

  // Update latency compensation for resampling
  updateLatencyCompensation();

  // ✅ Apply actual tone gain to EQ
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
  dcBlockerLeft.reset();
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

// ####################
// UPDATE CACHED PARAMS
// ####################
void TONE3000Processor::updateCachedParameters() {
  constexpr float epsilon = 1e-5f;

  auto loadFloat = [&](const juce::String& paramID) -> float {
    return parameters.getRawParameterValue(paramID)->load();
  };

  auto updateFloat = [&](float& cached, const juce::String& paramID) {
    float value = loadFloat(paramID);
    if (std::abs(value - cached) > epsilon) {
      cached = value;
      // Set eqParamsDirty if any EQ parameter changed
      if (paramID == "toneBass" || paramID == "toneMid" || paramID == "toneTreble") {
        eqParamsDirty = true;
      }
    }
  };

  updateFloat(cacheInputLevel, "inputLevel");
  updateFloat(cacheOutputLevel, "outputLevel");
  updateFloat(cacheInputBalance, "inputBalance");
  updateFloat(cacheOutputBalance, "outputBalance");
  updateFloat(cacheSpreadAmount, "spreadAmount");
  updateFloat(cacheSpreadJitter, "spreadJitter");
  updateFloat(cacheChainPanLeft, "chainPanLeft");
  updateFloat(cacheChainPanRight, "chainPanRight");
  updateFloat(cacheBassTone, "toneBass");
  updateFloat(cacheMidTone, "toneMid");
  updateFloat(cacheTrebleTone, "toneTreble");
  updateFloat(cacheGateThreshold, "gateThreshold");
  updateFloat(cacheTargetLoudness, "targetLoudness");
  updateFloat(cacheInputCalibrationLevel, "inputCalibrationLevel");

  auto loadBool = [&](const juce::String& paramID) -> bool {
    return parameters.getRawParameterValue(paramID)->load() > 0.5f;
  };
  cacheNormalize = loadBool("normalize");
  cacheCalibrateInput = loadBool("calibrateInput");
  cacheGateEnabled = loadBool("gateEnabled");
  cacheToneEqEnabled = loadBool("toneEqEnabled");
  cacheSpreadEnabled = loadBool("spreadEnabled");
}

// Per-channel gain for a main stage: main level ±12 dB, plus the balance
// trim (0.5 = centered) which adds up to ±12 dB opposing between L and R.
// Mono buffers pass ch == 0 only, so balance is inert there by construction.
static float mainStageChannelGain(float level, float balance, int channel) {
  const float levelDb = (level - 0.5f) * 24.0f;
  const float trimDb = (balance - 0.5f) * 24.0f * (channel == 0 ? -1.0f : 1.0f);
  return juce::Decibels::decibelsToGain(levelDb + trimDb);
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

  // Highest index of an enabled+loaded NAM block. A stereo IR is only worth processing in
  // true stereo when there is no NAM downstream to collapse the image back to mono.
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
    if (!block->loaded || !block->enabled) {
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

    // Per-block input gain (0.5 == unity, ±12 dB), applied after the dry copy
    // so Mix still blends against the untouched signal — this drives the
    // block's DSP harder/softer like a drive control. The block input meter
    // reads the post-gain signal (what the model actually receives).
    {
      const float inputGainDbBlock = (block->inputGainNormalized - 0.5f) * 24.0f;
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
      const float blockInputDb =
          blockInputPeak > 0.0f ? juce::Decibels::gainToDecibels(blockInputPeak) : -60.0f;
      block->inputMeterDb.store(std::max(-60.0f, blockInputDb));
    }

    if (block->type == ChainBlockType::NAM) {
      // NAM Processing with resampling wrapper
      try {
        jassert(numSamples <= maxBlockSize);

        // Skip processing if no resampling wrapper is available
        if (block->namResampler == nullptr) {
          DBG("Warning: NAM block " << block->id << " has no resampling wrapper - skipping");
          continue;
        }

        // Calculate additional calibration gain for this specific NAM block
        float calibrationGain = 1.0f;
        if (cacheCalibrateInput && block->namResampler->hasInputLevel()) {
          const double modelInputLevel = block->namResampler->getInputLevel();
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

        // Process with resampling wrapper (handles mono conversion internally)
        block->namResampler->process(buffer);

        // Compute per-block NAM normalization target and apply smoother
        // If normalization is disabled, force smoother to unity.
        const float targetLufs = cacheTargetLoudness;  // use live target
        float blockGain = 1.0f;
        if (cacheNormalize) {
          float modelLoudnessDb = targetLufs;  // Default fallback

          // Get loudness from the resampling wrapper
          if (block->namResampler->hasLoudness()) {
            modelLoudnessDb = static_cast<float>(block->namResampler->getLoudness());
          }

          if (!std::isfinite(modelLoudnessDb) || modelLoudnessDb < -100.0f || modelLoudnessDb > 0.0f) {
            modelLoudnessDb = targetLufs;
          }
          const float gainAdjustmentDb = juce::jlimit(-12.0f, 6.0f, targetLufs - modelLoudnessDb);
          blockGain = juce::Decibels::decibelsToGain(gainAdjustmentDb);
          if (!block->namNormalizationSmoother.isSmoothing()) {
            block->namNormalizationSmoother.reset(getSampleRate(), 0.05f);
            block->namNormalizationSmoother.setCurrentAndTargetValue(blockGain);
          }
          block->namNormalizationSmoother.setTargetValue(blockGain);
        } else {
          if (!block->namNormalizationSmoother.isSmoothing()) {
            block->namNormalizationSmoother.reset(getSampleRate(), 0.02f);
            block->namNormalizationSmoother.setCurrentAndTargetValue(1.0f);
          } else {
            block->namNormalizationSmoother.setTargetValue(1.0f);
          }
        }

        // Apply per-block normalization gain to the buffer
        auto* left = buffer.getWritePointer(0);
        auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i) {
          const float g = block->namNormalizationSmoother.getNextValue();
          left[i] *= g;
          if (right) right[i] *= g;
        }
      } catch (const std::exception& e) {
        // Not RT-safe, but this is a one-shot failure path: loaded=false below
        // stops the block from processing (and re-throwing) again. Without this
        // a NAM failure is a silent bypass with nothing in the release log.
        juce::Logger::writeToLog("[NAM] Processing failed for block " + juce::String(block->id) +
                                 ": " + e.what() + " — disabling block");
        block->loaded = false;
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

        // Apply RMS-based normalization at runtime via smoother (attenuation-only)
        const float targetGain =
            cacheNormalize ? juce::jlimit(0.0f, 1.0f, block->irNormalizationGainLinear) : 1.0f;
        if (!block->irNormalizationSmoother.isSmoothing()) {
          block->irNormalizationSmoother.reset(getSampleRate(), 0.05f);
          block->irNormalizationSmoother.setCurrentAndTargetValue(targetGain);
        }
        block->irNormalizationSmoother.setTargetValue(targetGain);
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
    // Map normalized gain to linear: 0.5 -> 1.0, +/-0.5 -> +/-12 dB range
    const float gainDb = (block->outputGainNormalized - 0.5f) * 24.0f;  // -12 dB .. +12 dB
    const float targetLinear = juce::Decibels::decibelsToGain(gainDb);
    block->outputGainSmoother.setTargetValue(targetLinear);
    block->mixSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, block->mixNormalized));

    float blockOutputPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
      const float g = block->outputGainSmoother.getNextValue();
      const float m = block->mixSmoother.getNextValue();
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

    // Post-block EQ: the last stage of the block, applied after gain + mix so
    // it shapes exactly what leaves the block. Skipped entirely when flat.
    if (block->eq.isActive()) {
      block->eq.process(buffer);
      // The mix-loop peak is pre-EQ; re-measure so the meter reflects the
      // block's true output.
      blockOutputPeak = 0.0f;
      for (int ch = 0; ch < numChannels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
          blockOutputPeak = std::max(blockOutputPeak, std::abs(data[i]));
      }
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
  // Input + Noise Gate (always enabled)
  // #########################
  
  // Per-channel input meters, captured before processing. Mono sources
  // report the same level on both channels.
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
    inputMeterLevelL.store(peakToDb(peakL));
    inputMeterLevelR.store(peakToDb(peakR));
  }

  // Feed the tuner from the raw input (pre-gain, pre-gate) while the tuner
  // screen is open. Channel 0 only: guitar sources are mono, and mixing
  // channels risks phase cancellation.
  if (tuner.isEnabled())
    tuner.pushSamples(buffer.getReadPointer(0), numSamples);

  // Main input gain per channel (level ±12 dB, balance trim ±12 dB opposing).
  // The gate rides the same loop but only when its power switch is on.
  const float gateThresholdLinear = juce::Decibels::decibelsToGain(cacheGateThreshold);
  const bool gateOn = cacheGateEnabled;
  for (int ch = 0; ch < numChannels; ++ch) {
    const float gainLinear = mainStageChannelGain(cacheInputLevel, cacheInputBalance, ch);
    auto* channelData = buffer.getWritePointer(ch);
    for (int i = 0; i < numSamples; ++i) {
      float& sample = channelData[i];
      sample *= gainLinear;
      if (gateOn && std::abs(sample) < gateThresholdLinear)
        sample = 0.0f;
    }
  }

  // Spread parameter sync + onset analysis, pre-chain: the detector must see
  // the raw picked signal (post-gain/gate) — amp/IR processing compresses
  // pick attacks into mush and its noise floor fakes constant onsets. The
  // delay itself runs post-chain (below).
  spread.setTarget(SpreadParams::fromNormalized(cacheSpreadAmount, cacheSpreadJitter),
                   cacheSpreadEnabled && numChannels >= 2);
  if (spread.isRunning())
    spread.analyzeOnsets(buffer.getReadPointer(0), numSamples);

  // ####################
  // MODULAR CHAIN PROCESSING
  // ####################
  {
    juce::ScopedLock lock(chainMutex);

    if (stereoEnabled.load() && numChannels >= 2) {
      // Stereo mode: split the buffer into two mono sides, run an independent chain on each,
      // then merge back. Scratch buffers are pre-allocated in prepareToPlay.
      stereoChainBufferL.setSize(1, numSamples, false, false, true);
      stereoChainBufferR.setSize(1, numSamples, false, false, true);
      stereoChainBufferL.copyFrom(0, 0, buffer, 0, 0, numSamples);
      stereoChainBufferR.copyFrom(0, 0, buffer, 1, 0, numSamples);

      processChainOnBuffer(chainBlocks, stereoChainBufferL);
      processChainOnBuffer(rightChainBlocks, stereoChainBufferR);

      buffer.copyFrom(0, 0, stereoChainBufferL, 0, 0, numSamples);
      buffer.copyFrom(1, 0, stereoChainBufferR, 0, 0, numSamples);
    } else {
      processChainOnBuffer(chainBlocks, buffer);
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
    if (spread.isRunning()) {
      if (!isStereo && numChannels >= 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);  // seed the double
      spread.process(buffer);
    }

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
    dcBlockerLeft.process(context);
  }

  // Remove global normalization stage; handled per NAM block

  // ##########
  // EQ section (global 3-band tone stack, skipped when powered off)
  // ##########
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

  // ###########
  // Output gain per channel (level ±12 dB, balance trim ±12 dB opposing).
  // Per-channel output meters ride the same pass.
  // ###########
  {
    float peakL = 0.0f, peakR = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch) {
      const float gainLinear = mainStageChannelGain(cacheOutputLevel, cacheOutputBalance, ch);
      float& peak = (ch == 0) ? peakL : peakR;
      auto* channelData = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        channelData[i] *= gainLinear;
        peak = std::max(peak, std::abs(channelData[i]));
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
// METER LEVEL GETTERS
// #########################
float TONE3000Processor::getInputMeterLevel() const {
  return std::max(inputMeterLevelL.load(), inputMeterLevelR.load());
}

float TONE3000Processor::getOutputMeterLevel() const {
  return std::max(outputMeterLevelL.load(), outputMeterLevelR.load());
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