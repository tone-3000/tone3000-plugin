#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Transport and take are owned by the audio thread via GlobalLooper. Storage is allocated
// only on creation / rate changes, never by record/play or the audio callback.
class MonoLooper {
public:
  enum class State { stopped, recording, playing };
  void prepare(double sampleRate) {
    if (sampleRate == rate && !audio.empty()) return;
    rate = sampleRate;
    audio.assign(static_cast<size_t>(std::ceil(rate * 40.0)), 0.0f);
    length = position = 0;
    state = State::stopped;
    blend = 0;
    panLeft = panRight = 1.0f;
  }
  void record() { length = position = 0; state = State::recording; playAtLimit = false; blend = 0; }
  // One pedal: every new recording replaces the take; finishing it loops.
  void toggleRecord() {
    if (state == State::recording) {
      stop();
      play();
    } else {
      record();
      playAtLimit = true;
    }
  }
  void stop() { state = State::stopped; }
  void play() { if (length) { position = 0; state = State::playing; } }
  bool isSilent() const { return blend <= 0; }
  State getState() const { return state; }
  double seconds() const { return rate > 0 ? length / rate : 0; }
  void process(float* left, float* right, int count, float mix, bool enabled, float pan = 0, bool panEnabled = true) {
    if (!enabled && state != State::stopped) stop();
    const float step = static_cast<float>(1.0 / std::max(1.0, rate * 0.005));
    // Balance the mono loop with unity gain at centre; live routing is untouched.
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1) * 0.78539816339f;
    const float targetLeft = right && panEnabled ? std::min(1.0f, std::sqrt(2.0f) * std::cos(angle)) : 1;
    const float targetRight = panEnabled ? std::min(1.0f, std::sqrt(2.0f) * std::sin(angle)) : 1;
    for (int i = 0; i < count; ++i) {
      if (enabled && state == State::recording && length < audio.size()) {
        audio[length++] = right ? 0.5f * (left[i] + right[i]) : left[i];
        if (length == audio.size()) {
          stop();
          if (playAtLimit) play();
        }
      }
      const bool playing = enabled && state == State::playing && length > 0;
      const float target = playing ? 2.0f * std::clamp(mix, 0.0f, 1.0f) : 0.0f;
      blend += std::clamp(target - blend, -step, step);
      // Continue the old take through the short stop fade. Record discards it.
      float wet = 0;
      if (length && state != State::recording && (playing || blend > 0)) {
        const size_t edge = std::min<size_t>(64, length / 2);
        const float ramp = edge ? std::min(1.0f, static_cast<float>(std::min(position, length - 1 - position)) / edge) : 1.0f;
        wet = audio[position] * ramp;
        position = (position + 1) % length;
      }
      panLeft += std::clamp(targetLeft - panLeft, -step, step);
      panRight += std::clamp(targetRight - panRight, -step, step);
      // Live guitar is always unity. Mix 0.5 adds the loop at unity gain.
      if (state == State::recording) blend = 0;
      left[i] += wet * blend * panLeft;
      if (right) right[i] += wet * blend * panRight;
    }
  }
private:
  std::vector<float> audio;
  double rate = 0;
  size_t length = 0, position = 0;
  State state = State::stopped;
  bool playAtLimit = false;
  float blend = 0;
  float panLeft = 1.0f, panRight = 1.0f;
};
