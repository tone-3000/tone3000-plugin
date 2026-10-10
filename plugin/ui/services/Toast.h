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

  // solid: the white pill (the app's confirmations). quiet: white text in a
  // white outline, for the Library's frequent ones (Kept in, Moved to).
  enum class Style { solid, quiet };

  // "" when nothing is showing.
  const juce::String& message() const { return message_; }
  Style style() const { return style_; }

  // Flash a message; auto-dismisses after a moment (a long one, a summary,
  // stays up long enough to read: up to 6 s).
  void show(const juce::String& message, Style style = Style::solid) {
    style_ = style;
    set(message, juce::jlimit(kShowMs, 6000, message.length() * 55));
  }
  // Pin a message until the next show/clear (auto-measure "Listening"; the
  // Library's work under way, "Importing...").
  void pin(const juce::String& message, Style style = Style::solid) {
    style_ = style;
    set(message, 0);
  }
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
  Style style_ = Style::solid;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
