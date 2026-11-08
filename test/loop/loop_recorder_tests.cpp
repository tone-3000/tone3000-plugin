// LoopRecorder tests: the one-switch state machine and the three things
// that make the loop sound like a loop (pre-roll, seam fades, crossfaded
// hand-over). JUCE-free like EqPinchTests, so they build and run in seconds.
#include "LoopRecorder.h"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using State = LoopRecorder::State;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Streams `in` through `rec` in kBlock chunks and returns what came out.
std::vector<float> run(LoopRecorder& rec, const std::vector<float>& in) {
  std::vector<float> out(in);
  for (size_t i = 0; i < out.size(); i += kBlock)
    rec.process(out.data() + i, static_cast<int>(std::min<size_t>(kBlock, out.size() - i)));
  return out;
}

std::vector<float> silence(int n) { return std::vector<float>(static_cast<size_t>(n), 0.0f); }

// A ramp that is distinct at every sample, so misplacement shows up.
std::vector<float> ramp(int n, float from, float to) {
  std::vector<float> v(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = from + (to - from) * static_cast<float>(i) / static_cast<float>(n);
  return v;
}

float maxStep(const std::vector<float>& v) {
  float m = 0.0f;
  for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::abs(v[i] - v[i - 1]));
  return m;
}

}  // namespace

int main() {
  try {
    LoopRecorder::Params p;
    p.maxSeconds = 0.5;  // 24000 samples: short enough to hit the cap in a test

    // Fresh: empty, and the dry signal passes untouched.
    LoopRecorder rec;
    rec.prepare(kFs, p);
    require(rec.state() == State::empty, "starts empty");
    auto dry = ramp(kBlock * 16, 0.1f, 0.5f);  // longer than a seam, so stop ramps land
    require(run(rec, dry) == dry, "empty passes dry through");

    // Tap arms; silence keeps it armed; signal starts the take.
    rec.tap();
    run(rec, silence(kBlock * 4));
    require(rec.state() == State::armed, "armed waits for signal");
    // A quiet lead-in below the threshold, then a hit: the take must start
    // on the hit, with the lead-in's last preRoll samples ahead of it.
    const int preRoll = rec.preRollSamples();
    auto leadIn = ramp(kBlock * 2, 0.001f, 0.002f);   // -60 dB-ish, under -40 dB
    run(rec, leadIn);
    require(rec.state() == State::armed, "a quiet lead-in does not trigger");
    auto take = ramp(kBlock * 60, 0.5f, 0.9f);        // well above threshold
    run(rec, take);
    require(rec.state() == State::recording, "signal at the threshold starts recording");
    require(rec.lengthSamples() == 0, "length is unpublished until the take closes");

    // Tap closes the take; its length is pre-roll + what was recorded.
    rec.tap();
    auto firstPass = run(rec, silence(kBlock * 40));  // shorter than the loop: no wrap yet
    require(rec.state() == State::playing, "tap while recording closes the take and plays it");
    const int len = rec.lengthSamples();
    require(len == preRoll + static_cast<int>(take.size()), "take = pre-roll + recorded samples");

    // Pre-roll: the samples right before the hit are in the head of the loop.
    // The loop output fades in over the head fade and the hand-over ramp, so
    // look past both: sample preRoll + seam*2 of the loop must be take[seam*2].
    const int seam = rec.seamSamples();
    const int headFade = rec.headFadeSamples();
    const int probe = preRoll + seam * 2;
    require(std::abs(firstPass[static_cast<size_t>(probe)] - take[static_cast<size_t>(seam * 2)]) < 1e-5f,
            "the loop plays the take from its head, pre-roll first");
    // And the pre-roll itself holds the lead-in's tail, under the head fade
    // (over before the pre-roll ends) and the dry/loop hand-over ramp.
    {
      const int i = preRoll - 1;
      const float w = i < headFade ? 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(headFade)) : 1.0f;
      const float g = std::min(1.0f, static_cast<float>(i + 1) / static_cast<float>(seam));
      const float expected = leadIn.back() * w * g;
      require(expected > 0.0f && std::abs(firstPass[static_cast<size_t>(i)] - expected) < 1e-5f,
              "the pre-roll carries the lead-in, not silence");
    }

    // Seam: the take's last sample is faded to (near) zero, and the output
    // over a wrap has no step larger than the fades' own slope. The play
    // head is `off` into the loop after the first pass, so loop sample k
    // sits at output index (k - off) mod len.
    auto twoLoops = run(rec, silence(len * 2));
    const int off = static_cast<int>(firstPass.size()) % len;
    const int tailAt = (len - 1 - off + len) % len;
    require(std::abs(twoLoops[static_cast<size_t>(tailAt)]) < 1e-3f, "tail fades out at the seam");
    // Steps across the seam, from the tail fade to the end of the pre-roll
    // (the take's own attack right after it is content, not a click).
    {
      float step = 0.0f;
      for (int j = tailAt - seam + 1; j <= tailAt + preRoll; ++j)
        step = std::max(step, std::abs(twoLoops[static_cast<size_t>(j)] - twoLoops[static_cast<size_t>(j - 1)]));
      require(step < 0.01f, "no click across the wrap");
    }
    // The loop repeats: sample k and k + len match once past the fades.
    for (int k = seam * 2; k < len - seam * 2; k += 97)
      require(std::abs(twoLoops[static_cast<size_t>(k)] - twoLoops[static_cast<size_t>(k + len)]) < 1e-5f,
              "the loop repeats sample for sample");
    require(rec.position() >= 0.0 && rec.position() < 1.0, "position is a fraction of the loop");

    // Stop: the loop fades out over the seam and dry passes again.
    rec.tap();
    auto stopping = run(rec, dry);
    require(rec.state() == State::stopped, "tap while playing stops");
    require(stopping[stopping.size() - 1] == dry[dry.size() - 1], "after the ramp the dry signal passes");
    require(maxStep(stopping) < 0.05f, "stopping ramps instead of cutting");
    require(run(rec, dry) == dry, "stopped passes dry through");

    // Tap again: plays from the top. Position restarts at 0.
    rec.tap();
    run(rec, silence(kBlock));
    require(rec.state() == State::playing, "tap while stopped plays again");
    require(rec.position() * len < kBlock + 1, "a restart plays from the head");

    // Clear: empty again, nothing published.
    rec.clear();
    require(run(rec, dry) == dry, "clear passes dry through");
    require(rec.state() == State::empty && rec.lengthSamples() == 0, "clear empties the loop");

    // Manual start: a tap while armed records even without signal, and the
    // max length closes the take by itself.
    rec.tap();
    rec.tap();
    run(rec, silence(kBlock));
    require(rec.state() == State::recording, "a second tap starts the take without signal");
    run(rec, silence(static_cast<int>(p.maxSeconds * kFs) + kBlock));
    require(rec.state() == State::playing, "the take closes itself at the cap");
    require(rec.lengthSamples() == static_cast<int>(p.maxSeconds * kFs), "the cap is the loop length");

    // A host re-prepare at the same rate (new block size, transport start)
    // keeps the take and the pedal state; a rate change empties the loop,
    // since the samples would play at the wrong speed.
    {
      const int lenBefore = rec.lengthSamples();
      rec.prepare(kFs, p);
      require(rec.state() == State::playing && rec.lengthSamples() == lenBefore,
              "a same-rate re-prepare keeps the loop");
      rec.tap();  // the pedal still works on it
      run(rec, dry);
      require(rec.state() == State::stopped, "and the pedal state");
      rec.prepare(kFs * 2, p);
      require(rec.state() == State::empty && rec.lengthSamples() == 0, "a rate change empties the loop");
      require(run(rec, dry) == dry, "and dry passes again");
    }

    std::cout << "LoopRecorder tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "LoopRecorder tests failed: " << e.what() << "\n";
    return 1;
  }
}
