#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <functional>
#include "NAM/dsp.h"

// Define constants needed by AudioDSPTools
#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 1024
#endif

// Define namespace and constants for AudioDSPTools compatibility
namespace iplug {
    constexpr double PI = 3.14159265358979323846;
}

#include "dsp/ResamplingContainer/ResamplingContainer.h"

/**
 * A JUCE-compatible wrapper around nam::DSP that provides transparent resampling
 * using the AudioDSPTools ResamplingContainer with high-quality Lanczos anti-aliasing.
 *
 * This wrapper automatically handles resampling when the host sample rate differs
 * from the NAM model's expected sample rate, and bypasses resampling when they match.
 */
class NamResampler {
public:
    /**
     * Constructor
     * @param model The NAM DSP model to wrap
     * @param hostSampleRate The host/plugin sample rate
     */
    NamResampler(std::unique_ptr<nam::DSP> model, double hostSampleRate);

    /**
     * Destructor
     */
    ~NamResampler() = default;

    // Non-copyable, movable
    NamResampler(const NamResampler&) = delete;
    NamResampler& operator=(const NamResampler&) = delete;
    NamResampler(NamResampler&&) = default;
    NamResampler& operator=(NamResampler&&) = default;

    /**
     * Prepare the wrapper for processing
     * @param sampleRate The host sample rate
     * @param maxBlockSize Maximum number of samples per processing block
     */
    void prepare(double sampleRate, int maxBlockSize);

    /**
     * Process an audio buffer (mono processing, stereo output)
     * @param buffer The audio buffer to process (modified in-place)
     */
    void process(juce::AudioBuffer<float>& buffer);

    /**
     * Get the latency introduced by resampling (in samples)
     * @return Latency in samples, or 0 if no resampling is needed
     */
    int getLatencySamples() const;

    /**
     * Check if resampling is needed
     * @return true if the host and model sample rates differ significantly
     */
    bool needsResampling() const;

    /**
     * Get the model's expected sample rate
     * @return The sample rate the NAM model was trained at
     */
    double getModelSampleRate() const { return modelSampleRate; }

    /**
     * Get the current host sample rate
     * @return The host sample rate
     */
    double getHostSampleRate() const { return hostSampleRate; }

    /**
     * Check if the model has input level metadata
     * @return true if input level is available
     */
    bool hasInputLevel() const { return wrappedModel->HasInputLevel(); }

    /**
     * Get the model's input level (if available)
     * @return Input level in dBFS
     */
    double getInputLevel() const { return wrappedModel->GetInputLevel(); }

    /**
     * Check if the model has output level metadata
     * @return true if output level is available
     */
    bool hasOutputLevel() const { return wrappedModel->HasOutputLevel(); }

    /**
     * Get the model's output level (if available)
     * @return Output level in dBFS
     */
    double getOutputLevel() const { return wrappedModel->GetOutputLevel(); }

    /**
     * Check if the model has loudness metadata
     * @return true if loudness is available
     */
    bool hasLoudness() const { return wrappedModel->HasLoudness(); }

    /**
     * Get the model's loudness (if available)
     * @return Loudness in LUFS
     */
    double getLoudness() const { return wrappedModel->GetLoudness(); }

    /**
     * Reset and prewarm the model
     * @param sampleRate Sample rate for reset
     * @param maxBlockSize Maximum block size
     */
    void resetAndPrewarm(double sampleRate, int maxBlockSize);

    /** True if the wrapped NAM model supports SlimmableModel (container / A2 slimmable). */
    bool isSlimmableModel() const;

    /**
     * Requested slimmable size (0.5 = lite, 1.0 = full). Clamped to [0.5, 1.0].
     * Applied in prepare() and immediately if already prepared.
     */
    void setSlimmableSize(double val);

    double getSlimmableSize() const noexcept { return requestedSlimmableSize; }

private:
    // The wrapped NAM model
    std::unique_ptr<nam::DSP> wrappedModel;

    // Sample rates
    double hostSampleRate;
    double modelSampleRate;

    // AudioDSPTools ResamplingContainer (mono, Lanczos filter size 12)
    std::unique_ptr<dsp::ResamplingContainer<float, 1, 12>> resampler;

    // Processing function for the ResamplingContainer callback
    std::function<void(float**, float**, int)> processFunc;

    // Conversion buffers for float<->double conversion (NAM expects double)
    std::vector<double> inputConversionBuffer;
    std::vector<double> outputConversionBuffer;

    // Prepared state
    bool isPrepared;
    int maxBlockSize;

    double requestedSlimmableSize{1.0};

    // Helper to get sample rate from NAM model
    static double extractModelSampleRate(const std::unique_ptr<nam::DSP>& model);

    // Tolerance for sample rate comparison
    static constexpr double SAMPLE_RATE_TOLERANCE = 0.1;
};
