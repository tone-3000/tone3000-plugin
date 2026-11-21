#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

/**
 * Click track mixed on top of the output. One identical click per beat; 0 BPM
 * is silence. Beats are counted from -0.5 at start, so the first click lands
 * half a beat in, and beats() is where the audio is now, for the UI to follow.
 * process() allocates nothing and costs one add per channel while a click
 * rings.
 */
class Metronome {
public:
  // Beats counted before the first click; the UI animation starts from it too.
  static constexpr double kLeadInBeats = 0.5;

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    click_ = makeClick(sampleRate);
    stop();
  }

  void process(juce::AudioBuffer<float>& buffer, float bpm, float gain) {
    if (bpm <= 0.0f) return stop();

    const double beatsPerSample = bpm / (60.0 * sampleRate_);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
      if (beats_ >= nextClickBeat_) startClick();
      beats_ += beatsPerSample;
      addClickSample(buffer, i, gain);
    }
  }

  double beats() const { return beats_; }

private:
  static constexpr double kClickSeconds = 0.03;
  static constexpr double kClickHz = 1000.0;
  static constexpr double kDecaySeconds = 0.005;
  static constexpr float kClickLevel = 1.0f;

  static std::vector<float> makeClick(double sampleRate) {
    std::vector<float> click(static_cast<size_t>(kClickSeconds * sampleRate));
    for (size_t n = 0; n < click.size(); ++n) {
      const double t = static_cast<double>(n) / sampleRate;
      click[n] = kClickLevel * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * kClickHz * t) *
                                                  std::exp(-t / kDecaySeconds));
    }
    return click;
  }

  void stop() {
    beats_ = -kLeadInBeats;
    nextClickBeat_ = 0.0;
    clickPosition_ = click_.size();
  }

  void startClick() {
    clickPosition_ = 0;
    nextClickBeat_ += 1.0;
  }

  void addClickSample(juce::AudioBuffer<float>& buffer, int index, float gain) {
    if (clickPosition_ >= click_.size()) return;
    const float sample = click_[clickPosition_++] * gain;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.getWritePointer(ch)[index] += sample;
  }

  double sampleRate_ = 48000.0;
  std::vector<float> click_;
  size_t clickPosition_ = 0;
  double beats_ = -kLeadInBeats, nextClickBeat_ = 0.0;
};
