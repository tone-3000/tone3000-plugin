#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

/**
 * Monophonic pitch detector for the tuner screen.
 *
 * The audio thread pushes raw (pre-gain, pre-gate) input samples into a ring
 * buffer whenever the tuner is enabled. The UI polls getReading() from the
 * message thread, which runs a YIN pitch analysis over the most recent window
 * and returns { frequency, confidence, level }. No locks are used: a torn
 * read across the ring wrap only ever produces a single throwaway reading.
 */
class TunerDetector {
public:
  TunerDetector();

  void prepare(double sampleRate);

  void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }
  bool isEnabled() const { return enabled.load(); }

  /** Real-time thread. Cheap copy into the ring buffer; no allocation. */
  void pushSamples(const float* samples, int numSamples);

  /**
   * Message thread. Returns a DynamicObject var:
   *   frequency  (double) detected pitch in Hz, 0.0 when no reliable pitch
   *   confidence (double) 0..1
   *   level      (double) window RMS in dBFS
   * Results are cached for a short interval so fast UI polling stays cheap.
   */
  juce::var getReading();

private:
  void analyze();

  static constexpr int kRingSize = 1 << 15;  // 32768 samples (~0.7 s at 48 kHz)
  static constexpr int kDecimation = 4;      // analyze at sampleRate / 4
  static constexpr int kWindowSize = 2048;   // decimated samples per analysis

  static constexpr double kMinFrequency = 55.0;    // below low B on a 7-string
  static constexpr double kMaxFrequency = 1500.0;  // above high-fret high E
  static constexpr double kSilenceDb = -55.0;
  static constexpr double kYinThreshold = 0.15;
  static constexpr int kAnalysisIntervalMs = 25;

  std::vector<float> ring;
  std::atomic<int> writePos{0};
  std::atomic<bool> enabled{false};
  double sampleRate = 48000.0;

  // Message-thread scratch + cached result
  std::vector<float> rawWindow;
  std::vector<float> window;
  std::vector<float> yin;
  double lastFrequency = 0.0;
  double lastConfidence = 0.0;
  double lastLevelDb = -120.0;
  juce::int64 lastAnalysisMs = 0; 
};
