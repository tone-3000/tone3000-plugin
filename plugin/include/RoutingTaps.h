#pragma once

#include "ChainDomain.h"
#include "ChainOversampler.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstring>
#include <memory>
#include <string>

namespace t3k::routing {

enum class Stage { stack, mod, wetFx };

/** Workstation routing roles, deliberately outside presets/model state.
    IDs name the end block (Stack/Mod) or first designated serial FX block
    (Wet FX), separately for each rendered lane. In mono/dual-mono mode the
    primary role drives both channels. A branch lane can name its shared
    trunk prefix. Configure only while the processor is detached/quiescent,
    then prepareToPlay; IDs are re-resolved under the chain lock each render. */
struct TapPlan {
  bool enabled = false;
  std::array<std::string, 2> stackEnd, modEnd, wetStart;
};

/** Host-rate, pre-image/EQ/master-gain buses. Pointers are valid until the
    next processBlock or re-prepare. Missing roles produce zero and false,
    never a guessed fallback. Oversized host callbacks produce an empty
    view; their converter clocks still advance slice by slice. */
struct TapView {
  std::array<const float*, 2> channels{};
  std::array<bool, 2> available{};
  int frames = 0;
};

namespace detail {

/** Allocation-free auxiliary conversion of exact chain-domain taps through
    the same downsampling filters and host-boundary clock as the main bus.
    Each stage owns independent filter state; even unavailable stages feed
    zero through both conversions, so becoming valid cannot change phase. */
class TapBuffers {
public:
  static constexpr size_t count = 3;
  static size_t index(Stage stage) { return static_cast<size_t>(stage); }

  void prepare(double hostRate, int hostFrames, int baseFrames, int factor) {
    maximumHostFrames = juce::jmax(1, hostFrames);
    const int baseCapacity = juce::jmax(1, baseFrames) * 2 + 64;
    zeroInput.setSize(2, maximumHostFrames, false, true, false);
    zeroInput.clear();
    for (size_t i = 0; i < count; ++i) {
      auto& s = stages[i];
      s.os.setSize(2, juce::jmax(1, baseFrames) * factor, false, true, false);
      s.base.setSize(2, baseCapacity, false, true, false);
      s.host.setSize(2, maximumHostFrames, false, true, false);
      s.os.clear(); s.base.clear(); s.host.clear();
      s.down.prepare(factor, juce::jmax(1, baseFrames));
      s.boundary.reset();
      if (std::abs(hostRate - kChainBaseSampleRate) > 0.1) {
        s.boundary = std::make_unique<ChainBoundaryResampler>(kChainBaseSampleRate);
        s.boundary->Reset(hostRate, maximumHostFrames);
      }
      s.callback = [this, i](float**, float** output, int frames) {
        auto& stage = stages[i];
        const int remaining = stage.baseWritten - stage.baseRead;
        const int take = juce::jmax(0, juce::jmin(remaining, frames));
        for (int ch = 0; ch < 2; ++ch) {
          if (take > 0)
            std::memcpy(output[ch], stage.base.getReadPointer(ch, stage.baseRead),
                        sizeof(float) * static_cast<size_t>(take));
          juce::FloatVectorOperations::clear(output[ch] + take, frames - take);
        }
        stage.baseRead += take;
        if (take != frames)
          stage.clockMismatch = true;
      };
    }
    prepared = true;
    invalidate();
  }

  void disable() { prepared = false; invalidate(); }
  bool isPrepared() const { return prepared; }

  void invalidate() {
    viewFrames = 0;
    for (auto& stage : stages)
      stage.available.fill(false);
  }

  void beginHostSlice() {
    for (auto& stage : stages) {
      stage.baseWritten = stage.baseRead = 0;
      stage.clockMismatch = false;
      stage.observedAny = false;
      stage.available.fill(false);
    }
  }

  void beginOversampledSlice(int frames) {
    for (auto& stage : stages)
      stage.os.clear(0, frames);
  }

  float* osChannel(Stage stage, int channel) {
    return stages[index(stage)].os.getWritePointer(channel);
  }

  void appendOversampledSlice(int frames,
                             const std::array<std::array<bool, 2>, count>& available) {
    for (size_t i = 0; i < count; ++i) {
      auto& s = stages[i];
      const int baseFrames = frames / s.down.getFactor();
      if (s.baseWritten + baseFrames > s.base.getNumSamples()) {
        s.clockMismatch = true;
        continue;
      }
      float* in[2] = {s.os.getWritePointer(0), s.os.getWritePointer(1)};
      float* out[2] = {s.base.getWritePointer(0, s.baseWritten),
                       s.base.getWritePointer(1, s.baseWritten)};
      s.down.processDownsampleOnly(in, out, 2, frames);
      s.baseWritten += baseFrames;
      for (int ch = 0; ch < 2; ++ch)
        s.available[static_cast<size_t>(ch)] = s.observedAny
            ? s.available[static_cast<size_t>(ch)] && available[i][static_cast<size_t>(ch)]
            : available[i][static_cast<size_t>(ch)];
      s.observedAny = true;
    }
  }

  void finishHostSlice(int frames, bool expose) {
    for (auto& s : stages) {
      if (s.boundary != nullptr) {
        float* in[2] = {zeroInput.getWritePointer(0), zeroInput.getWritePointer(1)};
        float* out[2] = {s.host.getWritePointer(0), s.host.getWritePointer(1)};
        s.boundary->ProcessBlock(in, out, frames, s.callback);
        if (s.baseRead != s.baseWritten)
          s.clockMismatch = true;
      } else if (s.baseWritten == frames) {
        for (int ch = 0; ch < 2; ++ch)
          s.host.copyFrom(ch, 0, s.base, ch, 0, frames);
      } else {
        s.host.clear(0, frames);
        s.clockMismatch = true;
      }
      for (int ch = 0; ch < 2; ++ch) {
        s.available[static_cast<size_t>(ch)] = expose && s.observedAny &&
            !s.clockMismatch && s.available[static_cast<size_t>(ch)];
        if (!s.available[static_cast<size_t>(ch)])
          s.host.clear(ch, 0, frames);
      }
    }
    viewFrames = expose ? frames : 0;
  }

  TapView view(Stage stage) const {
    if (!prepared || viewFrames <= 0)
      return {};
    const auto& s = stages[index(stage)];
    return {{{s.host.getReadPointer(0), s.host.getReadPointer(1)}}, s.available, viewFrames};
  }

private:
  struct StageBuffers {
    juce::AudioBuffer<float> os, base, host;
    ChainOversampler down;
    std::unique_ptr<ChainBoundaryResampler> boundary;
    ChainBoundaryResampler::BlockProcessFunc callback;
    std::array<bool, 2> available{};
    int baseWritten = 0, baseRead = 0;
    bool clockMismatch = false, observedAny = false;
  };
  std::array<StageBuffers, count> stages;
  juce::AudioBuffer<float> zeroInput;
  int maximumHostFrames = 0;
  int viewFrames = 0;
  bool prepared = false;
};

}  // namespace detail
}  // namespace t3k::routing
