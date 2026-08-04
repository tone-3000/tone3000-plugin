#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

/**
 * Per-block spectrum analyzer for the EQ editor backdrop.
 *
 * Same threading pattern as TunerDetector: the audio thread copies the block's
 * post-EQ output into a lock-free ring buffer, but only while `enabled` is
 * set, i.e. only while a UI EQ view for this block is actually open. All FFT /
 * smoothing work happens lazily on the message thread inside getSpectrum()
 * (the polled native function), throttled by a small cache interval.
 *
 * getSpectrum() returns kNumBins floats in dB (clamped kMinDb..0),
 * log-spaced from kMinFreqHz to kMaxFreqHz, the exact frequency mapping the
 * EQ graph uses for its x axis, so the UI can plot bins at uniform x spacing.
 * Constants are mirrored in ui/src/hooks/useBlockSpectrum.ts.
 */
class BlockSpectrum {
public:
  static constexpr int kNumBins = 64;
  static constexpr float kMinDb = -100.0f;
  static constexpr double kMinFreqHz = 20.0;
  static constexpr double kMaxFreqHz = 20000.0;

  BlockSpectrum();

  void prepare(double sampleRate);

  void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }
  bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }

  /** Real-time thread. Mono-sums up to two channels into the ring; no allocation. */
  void pushSamples(const float* ch0, const float* ch1OrNull, int numSamples);

  /** Message thread. Returns a var array of kNumBins dB values. */
  juce::var getSpectrum();

private:
  void analyze();

  static constexpr int kRingSize = 1 << 12;   // 4096 samples
  static constexpr int kFftOrder = 11;        // 2048-point FFT
  static constexpr int kFftSize = 1 << kFftOrder;
  static constexpr int kAnalysisIntervalMs = 30;

  std::vector<float> ring;
  std::atomic<int> writePos{0};
  std::atomic<bool> enabled{false};
  double sampleRate = 48000.0;

  // Message-thread scratch + smoothed output
  juce::dsp::FFT fft{kFftOrder};
  std::vector<float> windowTable;   // Hann
  std::vector<float> fftData;       // 2 * kFftSize
  std::vector<float> smoothedDb;    // kNumBins
  juce::int64 lastAnalysisMs = 0;
};
