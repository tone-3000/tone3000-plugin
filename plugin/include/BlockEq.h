#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/**
 * Six-band parametric EQ, one per chain block. Runs post-block by default
 * (after the block's output gain + mix stage); the `pre` flag moves it
 * between the block's input gain and its model instead, shaping the signal
 * that drives the amp/IR. Self-contained module: band parameters, biquad
 * coefficient math (RBJ cookbook — mirrored exactly by
 * ui/src/components/eqMath.ts so the drawn curve is the audio truth),
 * processing, and (de)serialization.
 *
 * Threading model: setters run on the message thread while `chainMutex` is
 * held (the audio thread holds the same lock during processing), so plain
 * members are safe and all transcendental math happens off the audio thread.
 * process() does zero allocation.
 *
 * Flat-skip: every band precomputes an `active` flag when its params change.
 * Bell/shelf bands with ~0 dB gain are inert; cut bands are active by
 * their nature the moment that type is selected. When no band is active,
 * isActive() is false and callers skip process() entirely — a flat EQ costs
 * one branch per audio block.
 *
 * Bypass: `enabled` (the EQ power button) gates isActive() the same way, so a
 * bypassed EQ keeps its band settings but costs nothing on the audio thread.
 */
class BlockEq {
public:
  static constexpr int kNumBands = 6;
  static constexpr float kMinFreqHz = 20.0f;
  static constexpr float kMaxFreqHz = 20000.0f;
  static constexpr float kMaxAbsGainDb = 15.0f;
  static constexpr float kMinQ = 0.1f;
  static constexpr float kMaxQ = 10.0f;

  enum class BandType { LowCut, LowShelf, Bell, HighShelf, HighCut };

  struct Band {
    BandType type{BandType::Bell};
    float freqHz{1000.0f};
    float gainDb{0.0f};
    float q{1.0f};
  };

  /** Guitar/bass-voiced defaults, all flat (0 dB): low shelf 100 Hz, bells at
      250 (mud) / 650 (boxiness) / 1.6k (presence) / 3.5k (bite, tighter Q),
      high shelf 8 kHz (fizz/air). */
  static std::array<Band, kNumBands> defaultBands();

  /**
   * Fixed channel-strip band roles (mirrored by the UI's type selector):
   * band 0 is low cut or low shelf, the last band is high cut or high shelf,
   * everything in between is a bell. Out-of-role types (including anything in
   * older saved state) coerce to the band's shelf/bell.
   */
  static BandType coerceTypeForBand(int index, BandType type);

  BlockEq();

  /** Message thread (under chainMutex). Recomputes coefficients for the given
      sample rate; resets filter state. */
  void prepare(double sampleRate);

  /** Message thread (under chainMutex). Clamps values, recomputes the band's
      coefficients and activity. Returns false for an out-of-range index. */
  bool setBand(int index, const Band& band);

  /** Message thread (under chainMutex). Parses { type, freqHz, gainDb, q }. */
  bool setBandFromVar(int index, const juce::var& bandVar);

  /** Message thread (under chainMutex). Back to flat defaults (and enabled). */
  void resetToDefault();

  /** Message thread (under chainMutex). Bypass toggle — band settings are
      kept; a disabled EQ is skipped exactly like a flat one. */
  void setEnabled(bool shouldBeEnabled);
  bool isEnabled() const { return enabled; }

  /** Message thread (under chainMutex). Position toggle: true = before the
      block's model (after its input gain), false = after gain + mix
      (default). Filter state resets on change — the EQ taps a different
      signal point. */
  void setPre(bool shouldBePre);
  bool isPre() const { return pre; }

  bool isActive() const { return enabled && anyBandActive; }

  /** Audio thread (under chainMutex). Processes up to 2 channels in place.
      Only call when isActive(). */
  void process(juce::AudioBuffer<float>& buffer);

  /** { enabled, pre, bands: [{ type, freqHz, gainDb, q } x6] } for the UI chain state. */
  juce::var toVar() const;

  /** ValueTree persistence (plugin state save/restore). */
  juce::ValueTree toValueTree() const;
  void restoreFromValueTree(const juce::ValueTree& tree);

  static juce::String bandTypeToString(BandType type);
  static BandType bandTypeFromString(const juce::String& s);

private:
  struct Biquad {
    float b0{1.0f}, b1{0.0f}, b2{0.0f}, a1{0.0f}, a2{0.0f};  // normalized (a0 == 1)
    float z1[2]{0.0f, 0.0f}, z2[2]{0.0f, 0.0f};              // TDF2 state per channel

    inline float processSample(float x, int ch) noexcept {
      const float y = b0 * x + z1[ch];
      z1[ch] = b1 * x - a1 * y + z2[ch];
      z2[ch] = b2 * x - a2 * y;
      return y;
    }
    void resetState() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
  };

  static bool isBandActive(const Band& band);
  static Band clampBand(Band band);
  void updateBand(int index);
  void updateActivity();

  std::array<Band, kNumBands> bands;
  std::array<Biquad, kNumBands> filters;
  std::array<bool, kNumBands> bandActive{};
  bool anyBandActive{false};
  bool enabled{true};
  bool pre{false};
  double sampleRate{48000.0};
};
