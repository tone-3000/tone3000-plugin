#include "MidiMapStore.h"

namespace t3k::ui {

MidiMapStore::MidiMapStore(Backend& backend) : backend_(backend) {
  backend_.addListener(this);
  refresh();
}

MidiMapStore::~MidiMapStore() { backend_.removeListener(this); }

void MidiMapStore::refresh() {
  const auto res = backend_.getMidiMapState();
  if (res.isObject() && res.getProperty("mappings", juce::var()).isArray())
    state_ = MidiMapState::parse(res);
  else
    state_.reset();
  listeners.call([](Listener& l) { l.midiMapChanged(); });
}

void MidiMapStore::setChannel(int channel) {
  backend_.setMidiChannelFilter(channel);
  refresh();
}

void MidiMapStore::startLearn(const juce::String& targetId) {
  backend_.startMidiLearn(targetId);
  refresh();
}

void MidiMapStore::cancelLearn() {
  backend_.cancelMidiLearn();
  refresh();
}

void MidiMapStore::removeMapping(const juce::String& targetId) {
  backend_.removeMidiMapping(targetId);
  refresh();
}

void MidiMapStore::setCcMapping(const juce::String& targetId, int number) {
  backend_.setMidiCcMapping(targetId, number);
  refresh();
}

}  // namespace t3k::ui
