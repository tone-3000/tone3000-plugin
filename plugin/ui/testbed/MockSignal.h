// A moving input for the mock backend: a deterministic "player" picking
// notes (fast attack, exponential decay, rests between phrases, a hot hit
// that clips now and then), turned into everything the processor would
// report for it. Meters (input/output, per block, CPU, correlation), the
// per-block analyser spectrum, tuner readings and the settings input meters
// all follow the same notes, so the gallery glows, the meters fall, the EQ
// curve's spectrum moves and the tuner needle drifts the way they do with a
// guitar plugged in. Values jitter frame to frame like a real peak meter and
// FFT do, so the stores' "only notify on change" short-circuits see as many
// changes as they would live.
#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace t3k::ui::testbed {

class MockSignal {
public:
  MockSignal();

  // Backend::getMeterLevels for `chain` (the mock's chain state: its tone
  // blocks each get an in/out pair along the signal path).
  juce::var meters(const juce::var& chain);
  // Backend::getBlockSpectrum: 64 dB bins, 20 Hz..20 kHz log-spaced.
  juce::var spectrum(const std::string& blockId);
  // Backend::getTunerReading.
  juce::var tuner();
  // Backend::getAudioInputLevels: one dBFS value per input channel.
  juce::var inputLevels(int channels);

private:
  struct Note {
    double start;  // seconds into the loop
    double hz;
    float peakDb;
    float centsOff;  // how far the player is from pitch on this note
  };
  struct Now {
    double t;         // seconds since the note started
    const Note* note;
    float env;        // 0..1 linear envelope
    float levelDb;    // input peak, dBFS
  };

  Now now();
  float jitter(float amount);

  std::vector<Note> notes_;
  double loopSeconds_ = 0;
  double startMs_;
  juce::Random random_{1};
};

}  // namespace t3k::ui::testbed
