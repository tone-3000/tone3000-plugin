// One app-wide toast (port of Toast.tsx's control half): quick confirmations
// ("Preset Saved", "Link Copied") and auto-measure status. Only one message
// shows at a time; a new one replaces whatever is up. ToastView renders it.
#pragma once

#include <juce_events/juce_events.h>

namespace t3k::ui {

class Toast : private juce::Timer {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void toastChanged() = 0;
  };

  // How long a flashed message stays up.
  static constexpr int kShowMs = 1800;

  ~Toast() override { stopTimer(); }

  // "" when nothing is showing.
  const juce::String& message() const { return message_; }

  // Flash a message; auto-dismisses after a moment.
  void show(const juce::String& message) { set(message, kShowMs); }
  // Pin a message until the next show/clear (auto-measure "Listening").
  void pin(const juce::String& message) { set(message, 0); }
  // Take a pinned message down with no follow-up (cancel, timeout).
  void clear() { set({}, 0); }

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

private:
  void set(const juce::String& message, int dismissMs) {
    stopTimer();
    message_ = message;
    if (message.isNotEmpty() && dismissMs > 0) startTimer(dismissMs);
    listeners.call([](Listener& l) { l.toastChanged(); });
  }
  void timerCallback() override { clear(); }

  juce::String message_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
