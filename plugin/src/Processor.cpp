#include "Processor.h"
#if !HEADLESS
#include "Editor.h"
#endif
#include <cmath>
#include <random>
#include <cstring>

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

    // Initialize per-block smoothers (output gain and mix)
    block->outputGainSmoother.reset(sampleRate, 0.05f);
    block->mixSmoother.reset(sampleRate, 0.05f);
    block->outputGainSmoother.setCurrentAndTargetValue(1.0f);  // updated on first process
    block->mixSmoother.setCurrentAndTargetValue(block->mixNormalized);
  }
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

  DBG("Preparing to play: sampleRate=" << sampleRate << ", samplesPerBlock=" << samplesPerBlock);
  DBG("Model sample rate set to: " << modelSampleRate);

  // Cached parameters
  cacheInputLevel = parameters.getRawParameterValue("inputLevel")->load();
  cacheOutputLevel = parameters.getRawParameterValue("outputLevel")->load();
  cacheBassTone = parameters.getRawParameterValue("toneBass")->load();
  cacheMidTone = parameters.getRawParameterValue("toneMid")->load();
  cacheTrebleTone = parameters.getRawParameterValue("toneTreble")->load();
  cacheGateThreshold = parameters.getRawParameterValue("gateThreshold")->load();
  cacheTargetLoudness = parameters.getRawParameterValue("targetLoudness")->load();
  DBG("Debug parameter values: gateThreshold=" << cacheGateThreshold << ", targetLoudness=" << cacheTargetLoudness);
  cacheNormalize = parameters.getRawParameterValue("normalize")->load() > 0.5f;
  cacheCalibrateInput = parameters.getRawParameterValue("calibrateInput")->load() > 0.5f;
  cacheInputCalibrationLevel = parameters.getRawParameterValue("inputCalibrationLevel")->load();
  DBG("Input calibration level loaded directly: " << cacheInputCalibrationLevel << " dBu");
  
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
  dcBlockerRight.prepare(spec);

  // Temporary unity filters for boot
  *bassFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, 100.0f, 1.0f, 1.0f);
  *midFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, 1000.0f, 1.0f, 1.0f);
  *trebleFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 4000.0f, 1.0f, 1.0f);
  *dcBlockerLeft.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);
  *dcBlockerRight.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);

  bassFilter.reset();
  midFilter.reset();
  trebleFilter.reset();
  dcBlockerLeft.reset();
  dcBlockerRight.reset();

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
  dcBlockerRight.reset();
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
  updateFloat(cacheBassTone, "toneBass");
  updateFloat(cacheMidTone, "toneMid");
  updateFloat(cacheTrebleTone, "toneTreble");
  updateFloat(cacheGateThreshold, "gateThreshold");
  updateFloat(cacheTargetLoudness, "targetLoudness");
  updateFloat(cacheInputCalibrationLevel, "inputCalibrationLevel");
  
  // Update boolean parameters
  bool newNormalize = parameters.getRawParameterValue("normalize")->load() > 0.5f;
  if (cacheNormalize != newNormalize) {
    cacheNormalize = newNormalize;
  }
  
  bool newCalibrateInput = parameters.getRawParameterValue("calibrateInput")->load() > 0.5f;
  if (cacheCalibrateInput != newCalibrateInput) {
    cacheCalibrateInput = newCalibrateInput;
  }
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
      continue;
    }

    // Prepare dry copy before processing for mix (reuse temp buffer)
    jassert(tempDryBuffer.getNumChannels() >= numChannels);
    jassert(tempDryBuffer.getNumSamples() >= numSamples);
    tempDryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1) {
      tempDryBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);
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
        DBG("Error in NAM processing for block " << block->id << ": " << e.what());
        std::cout << "Error in NAM processing for block " << block->id << ": " << e.what()
                  << std::endl;
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

    for (int i = 0; i < numSamples; ++i) {
      const float g = block->outputGainSmoother.getNextValue();
      const float m = block->mixSmoother.getNextValue();
      float wetL = buffer.getWritePointer(0)[i] * g;
      float dryL = tempDryBuffer.getReadPointer(0)[i];
      buffer.getWritePointer(0)[i] = dryL * (1.0f - m) + wetL * m;
      if (numChannels > 1) {
        float wetR = buffer.getWritePointer(1)[i] * g;
        float dryR = tempDryBuffer.getReadPointer(1)[i];
        buffer.getWritePointer(1)[i] = dryR * (1.0f - m) + wetR * m;
      }
    }
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
    outputMeterLevel.store(-60.0f);
    return;
  }

  updateCachedParameters();

  // #########################
  // Input + Noise Gate (always enabled)
  // #########################
  
  // Calculate input meter level before processing
  float inputPeak = 0.0f;
  for (int ch = 0; ch < numChannels; ++ch) {
    const auto* channelData = buffer.getReadPointer(ch);
    for (int i = 0; i < numSamples; ++i) {
      inputPeak = std::max(inputPeak, std::abs(channelData[i]));
    }
  }
  float inputDb = inputPeak > 0.0f ? juce::Decibels::gainToDecibels(inputPeak) : -60.0f;
  inputMeterLevel.store(std::max(-60.0f, inputDb));

  // Map normalized [0..1] to dB around unity: 0.5 => 0 dB, ±0.5 => ±12 dB
  const float inputGainDb = (cacheInputLevel - 0.5f) * 24.0f;  // -12 dB .. +12 dB
  const float inputGainLinear = juce::Decibels::decibelsToGain(inputGainDb);
  for (int ch = 0; ch < numChannels; ++ch) {
    auto* channelData = buffer.getWritePointer(ch);
    for (int i = 0; i < numSamples; ++i) {
      float& sample = channelData[i];
      sample *= inputGainLinear;
      if (std::abs(sample) < juce::Decibels::decibelsToGain(cacheGateThreshold))
        sample = 0.0f;
    }
  }

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
  // DC blocker
  // ##########
  {
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    dcBlockerLeft.process(context);
    if (numChannels > 1)
      dcBlockerRight.process(context);
  }

  // Remove global normalization stage; handled per NAM block

  // ##########
  // EQ section (always enabled)
  // ##########
  if (eqParamsDirty) {
    updateEqCoefficients();
    eqParamsDirty = false;
  }

  {
    juce::dsp::AudioBlock<float> eqBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> eqContext(eqBlock);
    bassFilter.process(eqContext);
    midFilter.process(eqContext);
    trebleFilter.process(eqContext);
  }

  // ###########
  // Output gain (0.5 => unity, ±12 dB range)
  // ###########
  const float outputGainDb = (cacheOutputLevel - 0.5f) * 24.0f;  // -12 dB .. +12 dB
  const float outputGainLinear = juce::Decibels::decibelsToGain(outputGainDb);
  for (int ch = 0; ch < numChannels; ++ch) {
    auto* channelData = buffer.getWritePointer(ch);
    for (int i = 0; i < numSamples; ++i)
      channelData[i] *= outputGainLinear;
  }

  // Calculate output meter level after final processing
  float outputPeak = 0.0f;
  for (int ch = 0; ch < numChannels; ++ch) {
    const auto* channelData = buffer.getReadPointer(ch);
    for (int i = 0; i < numSamples; ++i) {
      outputPeak = std::max(outputPeak, std::abs(channelData[i]));
    }
  }
  float outputDb = outputPeak > 0.0f ? juce::Decibels::gainToDecibels(outputPeak) : -60.0f;
  outputMeterLevel.store(std::max(-60.0f, outputDb));
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
  return inputMeterLevel.load();
}

float TONE3000Processor::getOutputMeterLevel() const {
  return outputMeterLevel.load();
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