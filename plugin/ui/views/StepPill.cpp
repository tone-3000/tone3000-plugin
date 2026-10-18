#include "StepPill.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
const juce::Colour kPill = juce::Colours::black.withAlpha(0.6f);
}  // namespace

StepPill::StepPill(Services& services, std::string blockId)
    : services_(services),
      blockId_(std::move(blockId)),
      prev_(Icon::ChevronLeft, ChromeIconButton::Tone::plain, help::Key::stepPrev),
      next_(Icon::ChevronRight, ChromeIconButton::Tone::plain, help::Key::stepNext) {
  addAndMakeVisible(prev_);
  addAndMakeVisible(next_);
  prev_.onClick = [this] { services_.stepper.step(blockId_, -1); };
  next_.onClick = [this] { services_.stepper.step(blockId_, +1); };
  services_.stepper.addListener(this);
  setSize(kWidth, kHeight);
  refresh();
}

StepPill::~StepPill() { services_.stepper.removeListener(this); }

void StepPill::refresh() {
  cursor_ = services_.stepper.cursor(blockId_);
  if (cursor_) {
    prev_.setEnabled(cursor_->canPrev);
    next_.setEnabled(cursor_->canNext);
    prev_.setAlpha(cursor_->canPrev ? 1.0f : theme::kDisabledOpacity);
    next_.setAlpha(cursor_->canNext ? 1.0f : theme::kDisabledOpacity);
  }
  repaint();
}

void StepPill::paint(juce::Graphics& g) {
  if (!cursor_) return;
  const auto box = getLocalBounds().toFloat();
  g.setColour(kPill);
  g.fillRoundedRectangle(box, box.getHeight() / 2);
  // The cursor this block is focused for the arrow keys gets the accent rim.
  if (services_.stepper.focusBlock() == blockId_) {
    g.setColour(theme::kBrandYellow.withAlpha(0.7f));
    g.drawRoundedRectangle(box.reduced(0.5f), box.getHeight() / 2, 1.0f);
  }
  const auto label = cursor_->busy ? juce::String("…")
                                   : juce::String(cursor_->position) + "/" + juce::String(cursor_->total);
  g.setColour(cursor_->busy ? theme::kMuted : theme::kWhite);
  g.setFont(Fonts::mono(11));
  g.drawText(label, getLocalBounds().withSizeKeepingCentre(kLabelWidth, kHeight), juce::Justification::centred,
             false);
}

void StepPill::resized() {
  const int y = (getHeight() - theme::kIconBoxSize) / 2;
  prev_.setBounds(4, y, theme::kIconBoxSize, theme::kIconBoxSize);
  next_.setBounds(getWidth() - 4 - theme::kIconBoxSize, y, theme::kIconBoxSize, theme::kIconBoxSize);
}

}  // namespace t3k::ui
