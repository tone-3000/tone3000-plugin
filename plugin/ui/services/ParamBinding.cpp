#include "ParamBinding.h"

namespace t3k::ui {

ParamBinding::ParamBinding(Backend& backend, const juce::String& id)
    : id_(id), param_(backend.parameter(id)) {
  if (param_ == nullptr) return;
  attachment_ = std::make_unique<juce::ParameterAttachment>(
      *param_, [this](float denormalised) { changed(denormalised); });
  lastSent_ = param_->getValue();
}

ParamBinding::~ParamBinding() {
  if (dragging_) endGesture();
}

float ParamBinding::normalised() const { return param_ != nullptr ? param_->getValue() : 0.0f; }

void ParamBinding::set(float normalised) {
  if (param_ == nullptr) return;
  const float clamped = juce::jlimit(0.0f, 1.0f, normalised);
  if (juce::exactlyEqual(clamped, lastSent_) && juce::exactlyEqual(clamped, param_->getValue()))
    return;
  lastSent_ = clamped;
  attachment_->setValueAsCompleteGesture(param_->convertFrom0to1(clamped));
}

void ParamBinding::beginGesture() {
  if (param_ == nullptr || dragging_) return;
  dragging_ = true;
  attachment_->beginGesture();
}

void ParamBinding::dragTo(float normalised) {
  if (param_ == nullptr) return;
  if (!dragging_) beginGesture();
  const float clamped = juce::jlimit(0.0f, 1.0f, normalised);
  if (juce::exactlyEqual(clamped, lastSent_)) return;
  lastSent_ = clamped;
  attachment_->setValueAsPartOfGesture(param_->convertFrom0to1(clamped));
}

void ParamBinding::endGesture() {
  if (param_ == nullptr || !dragging_) return;
  dragging_ = false;
  attachment_->endGesture();
  // The last value written during the drag is authoritative; a change that
  // landed mid-drag from elsewhere shows up on the next echo.
  lastSent_ = param_->getValue();
}

void ParamBinding::changed(float /*denormalised*/) {
  if (dragging_) return;
  lastSent_ = param_->getValue();
  if (onChange) onChange();
}

}  // namespace t3k::ui
