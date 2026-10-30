#include "StandaloneIoLease.h"

namespace t3k::standalone_io {
namespace {
// The standalone has one device manager. All mutators are message-thread
// operations; no lock, callback allocation or ownership work runs on audio.
std::weak_ptr<RoutingOwner> owner;
bool capturing = false, routing = false;
}
bool captureOwnsDevice() { return capturing; }
bool routingOwnsDevice() { return routing; }
bool settingsLocked() { return capturing || routing; }
void registerRoutingOwner(const std::shared_ptr<RoutingOwner>& value) { owner = value; }
void unregisterRoutingOwner(const std::shared_ptr<RoutingOwner>& value) {
  if (owner.lock() == value) owner.reset();
}
void setRoutingActive(bool active) { routing = active; }
juce::Result acquireCapture(std::function<juce::Result()>& resume) {
  if (capturing) return juce::Result::fail("Another capture already owns the audio device.");
  resume = {};
  if (routing) {
    const auto participant = owner.lock();
    if (!participant || !participant->suspend || !participant->resume)
      return juce::Result::fail("The active router could not yield its audio device.");
    participant->suspend();
    if (routing) return juce::Result::fail("Stop routing before starting a capture.");
    const std::weak_ptr<RoutingOwner> weak = participant;
    resume = [weak] {
      if (const auto current = weak.lock(); current && owner.lock() == current) return current->resume();
      return juce::Result::ok();
    };
  }
  capturing = true;
  return juce::Result::ok();
}
juce::Result releaseCapture(std::function<juce::Result()>& resume) {
  capturing = false;
  auto restore = std::move(resume);
  resume = {};
  return restore ? restore() : juce::Result::ok();
}
} // namespace t3k::standalone_io
