// One-shot HTTP listener on 127.0.0.1 for the OAuth redirect (RFC 8252
// loopback): starts on an ephemeral port, answers the single GET /?code=…
// with the "You're signed in" landing page (the TONE3000 Web mockup), hands
// the query string to the message thread and stops. Anything else the
// browser asks for (/favicon.ico) gets a 404 and the listener keeps waiting.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace t3k::ui {

class LoopbackServer : private juce::Thread {
public:
  LoopbackServer();
  ~LoopbackServer() override;

  // Bind and start accepting; false when no port could be bound.
  bool start();
  void stop();
  bool running() const { return isThreadRunning(); }
  int port() const { return port_; }
  // http://localhost:{port}/ (the publishable key auto-allows localhost).
  juce::String redirectUri() const;

  // The redirect's query string ("code=…&state=…"), on the message thread.
  std::function<void(const juce::String& query)> onCallback;

private:
  void run() override;
  void serve(juce::StreamingSocket& client);

  std::unique_ptr<juce::StreamingSocket> listener_;
  int port_ = 0;

  JUCE_DECLARE_WEAK_REFERENCEABLE(LoopbackServer)
};

}  // namespace t3k::ui
