#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace t3k::standalone_io {

/** Message-thread-only arbitration between the normal player, the router
    and raw capture. Callbacks and lifetime tokens never enter audio code. */
struct RoutingOwner {
  std::function<void()> suspend;
  std::function<juce::Result()> resume;
};
bool captureOwnsDevice();
bool routingOwnsDevice();
bool settingsLocked();
void registerRoutingOwner(const std::shared_ptr<RoutingOwner>&);
void unregisterRoutingOwner(const std::shared_ptr<RoutingOwner>&);
void setRoutingActive(bool);
juce::Result acquireCapture(std::function<juce::Result()>& resume);
juce::Result releaseCapture(std::function<juce::Result()>& resume);

} // namespace t3k::standalone_io
