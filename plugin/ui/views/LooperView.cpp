#include "LooperView.h"
#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

LooperView::LooperView(Services& services) : services_(services) {
  setOpaque(true);
  close_.setName("Close looper");
  close_.onClick = [this] { if (onClose) onClose(); };
  addAndMakeVisible(close_);
  for (auto* button : {&record_, &stop_, &play_}) addAndMakeVisible(*button);
  record_.onClick = [this] { services_.backend.looperCommand("record"); };
  stop_.onClick = [this] { services_.backend.looperCommand("stop"); };
  play_.onClick = [this] { services_.backend.looperCommand("play"); };
  mix_.setRange(0, 1, 0.01);
  pan_.setRange(-1, 1, 0.01);
  pan_.setDoubleClickReturnValue(true, 0);
  const auto state = services_.backend.getLooperState();
  mix_.setValue(state["mix"], juce::dontSendNotification);
  pan_.setValue(state["pan"], juce::dontSendNotification);
  for (auto* slider : {&mix_, &pan_}) {
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 36);
    addAndMakeVisible(*slider);
  }
  mix_.onValueChange = [this] { services_.backend.setLooperMix(static_cast<float>(mix_.getValue())); };
  pan_.onValueChange = [this] { services_.backend.setLooperPan(static_cast<float>(pan_.getValue())); };
  addAndMakeVisible(status_);
  mixLabel_.setText("Mix: loop off 0 / equal 0.5 / double 1", juce::dontSendNotification);
  panLabel_.setText("Pan: left -1 / centre 0 / right 1", juce::dontSendNotification);
  addAndMakeVisible(mixLabel_);
  addAndMakeVisible(panLabel_);
  timerCallback();
  startTimerHz(10);
}

void LooperView::timerCallback() {
  const auto state = services_.backend.getLooperState();
  status_.setText(state["state"].toString() + "   " +
      juce::String(static_cast<double>(state["seconds"]), 1) + " / 40.0 sec", juce::dontSendNotification);
  play_.setEnabled(static_cast<double>(state["seconds"]) > 0);
  record_.setToggleState(state["state"].toString() == "Recording", juce::dontSendNotification);
  play_.setToggleState(state["state"].toString() == "Playing", juce::dontSendNotification);
}

void LooperView::resized() {
  close_.setBounds(getWidth() - 64, 12, 44, 44);
  const int width = std::min(640, getWidth() - 48);
  const int x = (getWidth() - width) / 2;
  const int y = std::max(56, (getHeight() - 280) / 2);
  status_.setBounds(x, y, width, 36);
  const int buttonWidth = (width - 24) / 3;
  record_.setBounds(x, y + 44, buttonWidth, 64);
  stop_.setBounds(x + buttonWidth + 12, y + 44, buttonWidth, 64);
  play_.setBounds(x + 2 * (buttonWidth + 12), y + 44, buttonWidth, 64);
  mixLabel_.setBounds(x, y + 116, width, 30);
  mix_.setBounds(x, y + 146, width, 48);
  panLabel_.setBounds(x, y + 198, width, 30);
  pan_.setBounds(x, y + 228, width, 48);
}

void LooperView::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  g.setColour(theme::kWhite);
  g.setFont(Fonts::sans(24, true));
  g.drawText("LOOPER", getLocalBounds().withHeight(56), juce::Justification::centred);
}

}  // namespace t3k::ui
