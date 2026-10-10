// A view's handle on one host parameter (port of useParameter.ts). Values
// are normalised 0..1, the domain every knob scale (core/KnobScale.h) maps.
//
// Gesture model: a drag brackets its writes in begin/end so the host records
// one automation gesture, and inbound echoes are ignored while the pointer is
// down (the control owns the live value mid-drag; a stale echo must not snap
// it back). Change notifications arrive on the message thread.
//
// An unknown id (a build without that parameter) yields an inert binding
// that reads 0 / false and swallows writes, so a view never has to null-check.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

#include "backend/Backend.h"

namespace t3k::ui {

class ParamBinding {
public:
  ParamBinding(Backend& backend, const juce::String& id);
  ~ParamBinding();

  const juce::String& id() const { return id_; }

  float normalised() const;
  bool boolValue() const { return normalised() >= 0.5f; }

  // One-shot writes (a click, a typed value, a reset): a complete gesture.
  void set(float normalised);
  void set(bool value) { set(value ? 1.0f : 0.0f); }

  // Continuous writes from a drag.
  void beginGesture();
  void dragTo(float normalised);
  void endGesture();
  bool dragging() const { return dragging_; }

  // Fired on the message thread after the parameter changed from anywhere
  // (host automation, a preset, another control); suppressed mid-drag.
  std::function<void()> onChange;

private:
  void changed(float denormalised);

  juce::String id_;
  juce::RangedAudioParameter* param_ = nullptr;
  std::unique_ptr<juce::ParameterAttachment> attachment_;
  bool dragging_ = false;
  float lastSent_ = 0.0f;
};

}  // namespace t3k::ui
