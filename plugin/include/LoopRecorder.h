#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

/**
 * The audition loop's recorder and player (see docs/audition-loop.md).
 *
 * One take of dry input, looped, standing in for the player's hands while
 * they browse tones. The control model is a one-switch pedal (TC Ditto):
 * every tap advances the state machine, and a take starts on signal, not on
 * the tap (Boss RC "Auto Rec"), because the player is holding a guitar
 * when they press the button.
 *
 *   empty --tap--> armed --signal (or tap)--> recording --tap (or max)--> playing
 *   playing --tap--> stopped --tap--> playing          any --clear--> empty
 *
 * What makes a loop sound like a loop instead of a feature:
 *  - Pre-roll. The ring keeps the last few ms before the trigger, so the
 *    pick attack that crossed the threshold is in the take whole, not cut
 *    at the sample that crossed.
 *  - Seam fades. When the take closes its tail gets a raised-cosine fade
 *    over seamMs and its head a short one inside the pre-roll (half of
 *    preRollMs), so the wrap never clicks and the fade is over before the
 *    pick attack that triggered the take lands. The Ditto blends the tail
 *    past the loop point into the head instead; that is the upgrade if the
 *    dip at the seam is ever audible on sustained notes.
 *  - Crossfaded hand-over. While the loop plays, the live input is
 *    replaced, not mixed (auditioning is hands-free; noodling over the loop
 *    would defeat the comparison). Play/stop ramp a dry/loop gain over
 *    seamMs so the hand-over is silent too.
 *
 * Threading: prepare()/reset() from prepareToPlay (a re-prepare at the same
 * rate and params keeps the take: hosts re-prepare on a buffer-size change
 * or a transport start, and the player's loop must survive that); tap()/clear() from the
 * message thread (counted, consumed at the next process(): two taps inside
 * one audio block are two steps, and a clear drops the taps before it);
 * process() from
 * the audio thread, allocation free. State and position are atomics the UI
 * polls at its tick. Mono on purpose: the loop sits at the chain input
 * after the fold-down, where guitar sources are one channel.
 */
class LoopRecorder {
public:
  enum class State { empty, armed, recording, playing, stopped };

  struct Params {
    float armThresholdDb = -40.0f;  // the first sample at or above this starts the take
    double maxSeconds = 20.0;       // a take closes by itself here
    double seamMs = 10.0;           // head/tail fades, and the dry/loop hand-over ramp
    double preRollMs = 5.0;         // kept from before the trigger (pick attack)
    bool operator==(const Params&) const = default;
  };

  /** Sizes the buffers for the rate and empties the loop. Called again with
      the same rate and params (a host re-prepare for a new block size, a
      transport start) it keeps the take and the pedal state: only a rate
      change invalidates what was recorded. */
  void prepare(double sampleRate, const Params& params) {
    if (prepared_ && sampleRate == sampleRate_ && params == params_) return;
    prepared_ = true;
    params_ = params;
    sampleRate_ = sampleRate;
    maxSamples_ = std::max(1, static_cast<int>(params.maxSeconds * sampleRate));
    seamSamples_ = std::max(1, static_cast<int>(params.seamMs * 0.001 * sampleRate));
    preRollSamples_ = std::max(1, static_cast<int>(params.preRollMs * 0.001 * sampleRate));
    threshold_ = std::pow(10.0f, params.armThresholdDb / 20.0f);
    take_.assign(static_cast<size_t>(maxSamples_), 0.0f);
    preRoll_.assign(static_cast<size_t>(preRollSamples_), 0.0f);
    reset();
  }

  void reset() {
    pendingTaps_.store(0, std::memory_order_relaxed);
    pendingClear_.store(false, std::memory_order_relaxed);
    setState(State::empty);
    length_ = 0;
    pos_ = 0;
    preRollHead_ = 0;
    gain_ = 0.0f;
    position_.store(0.0, std::memory_order_relaxed);
    lengthSamples_.store(0, std::memory_order_relaxed);
  }

  // One footswitch: see the state diagram above.
  void tap() { pendingTaps_.fetch_add(1, std::memory_order_acq_rel); }
  // Back to empty, from any state.
  void clear() { pendingClear_.store(true, std::memory_order_release); }

  State state() const noexcept { return state_.load(std::memory_order_acquire); }
  // The take's length once it has closed (playing / stopped), else 0.
  int lengthSamples() const noexcept { return lengthSamples_.load(std::memory_order_relaxed); }
  // 0..1: the play head through the loop, or the take's fill while recording.
  double position() const noexcept { return position_.load(std::memory_order_relaxed); }

  const Params& params() const noexcept { return params_; }
  int seamSamples() const noexcept { return seamSamples_; }
  int preRollSamples() const noexcept { return preRollSamples_; }
  int headFadeSamples() const noexcept { return preRollSamples_ / 2; }

  /** Audio thread. `mono` is the chain's dry input; while the loop plays
      (and during the hand-over ramps) it is replaced in place. */
  void process(float* mono, int n) {
    consumeGesture();
    int i = 0;
    switch (state()) {
      case State::empty:
        feedPreRoll(mono, n);
        break;
      case State::armed: {
        const int hit = firstAtOrAbove(mono, n);
        if (hit < 0) {
          feedPreRoll(mono, n);
          break;
        }
        feedPreRoll(mono, hit);
        startTake();
        i = hit;
      }
        [[fallthrough]];
      case State::recording:
        record(mono + i, n - i);
        break;
      case State::playing:
      case State::stopped:
        play(mono, n);
        break;
    }
  }

private:
  void setState(State s) { state_.store(s, std::memory_order_release); }

  void consumeGesture() {
    int taps = pendingTaps_.exchange(0, std::memory_order_acq_rel);
    if (pendingClear_.exchange(false, std::memory_order_acq_rel)) {
      setState(State::empty);
      length_ = 0;
      pos_ = 0;
      gain_ = 0.0f;
      lengthSamples_.store(0, std::memory_order_relaxed);
      position_.store(0.0, std::memory_order_relaxed);
      taps = 0;  // taps before a clear were about the loop that is gone
    }
    for (; taps > 0; --taps) {
      switch (state()) {
        case State::empty: setState(State::armed); break;
        case State::armed: startTake(); break;           // manual start for a quiet intro
        case State::recording: closeTake(); break;
        case State::playing: setState(State::stopped); break;
        case State::stopped: pos_ = 0; setState(State::playing); break;
      }
    }
  }

  int firstAtOrAbove(const float* x, int n) const {
    for (int i = 0; i < n; ++i)
      if (std::abs(x[i]) >= threshold_) return i;
    return -1;
  }

  void feedPreRoll(const float* x, int n) {
    for (int i = 0; i < n; ++i) {
      preRoll_[static_cast<size_t>(preRollHead_)] = x[i];
      preRollHead_ = (preRollHead_ + 1) % preRollSamples_;
    }
  }

  // The take opens with the pre-roll ring, oldest sample first.
  void startTake() {
    length_ = 0;
    for (int i = 0; i < preRollSamples_ && length_ < maxSamples_; ++i)
      take_[static_cast<size_t>(length_++)] = preRoll_[static_cast<size_t>((preRollHead_ + i) % preRollSamples_)];
    setState(State::recording);
  }

  void record(const float* x, int n) {
    const int room = maxSamples_ - length_;
    const int take = std::min(n, room);
    std::copy(x, x + take, take_.begin() + length_);
    length_ += take;
    position_.store(static_cast<double>(length_) / maxSamples_, std::memory_order_relaxed);
    if (length_ >= maxSamples_) closeTake();
  }

  // Seam fades on the head and tail, then play from the top.
  void closeTake() {
    const int head = std::min(headFadeSamples(), length_ / 2);
    const int tail = std::min(seamSamples_, length_ / 2);
    for (int i = 0; i < head; ++i) take_[static_cast<size_t>(i)] *= fadeIn(i, head);
    for (int i = 0; i < tail; ++i) take_[static_cast<size_t>(length_ - 1 - i)] *= fadeIn(i, tail);
    pos_ = 0;
    lengthSamples_.store(length_, std::memory_order_relaxed);
    setState(State::playing);
  }

  static float fadeIn(int i, int n) {
    // Raised cosine, 0 at i = 0 up to (not reaching) 1 at i = n.
    return 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(n));
  }

  // out = dry * (1 - g) + loop * g, with g ramping to 1 while playing and
  // to 0 while stopped; the loop keeps advancing until the ramp lands so
  // a stop fades the loop out instead of cutting it.
  void play(float* mono, int n) {
    const float target = state() == State::playing ? 1.0f : 0.0f;
    const float step = 1.0f / static_cast<float>(seamSamples_);
    for (int i = 0; i < n; ++i) {
      if (gain_ < target) gain_ = std::min(target, gain_ + step);
      else if (gain_ > target) gain_ = std::max(target, gain_ - step);
      if (gain_ <= 0.0f && target == 0.0f) break;  // dry passes untouched from here
      const float loop = take_[static_cast<size_t>(pos_)];
      mono[i] = mono[i] * (1.0f - gain_) + loop * gain_;
      if (++pos_ >= length_) pos_ = 0;
    }
    position_.store(length_ > 0 ? static_cast<double>(pos_) / length_ : 0.0, std::memory_order_relaxed);
  }

  Params params_;
  bool prepared_ = false;
  double sampleRate_ = 48000.0;
  int maxSamples_ = 1, seamSamples_ = 1, preRollSamples_ = 1;
  float threshold_ = 0.01f;

  std::vector<float> take_;
  std::vector<float> preRoll_;
  int preRollHead_ = 0;
  int length_ = 0;  // audio thread's view; lengthSamples_ is the UI's
  int pos_ = 0;
  float gain_ = 0.0f;

  std::atomic<State> state_{State::empty};
  std::atomic<int> pendingTaps_{0};
  std::atomic<bool> pendingClear_{false};
  std::atomic<int> lengthSamples_{0};
  std::atomic<double> position_{0.0};
};
