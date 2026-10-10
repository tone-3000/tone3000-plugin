#include "LoopbackServer.h"

#include <juce_events/juce_events.h>

namespace t3k::ui {

namespace {
constexpr int kAcceptPollMs = 250;
constexpr int kReadTimeoutMs = 3000;

// The landing page (TONE3000 Web mockup 13301:49954): on black, a 320px
// card with the theme's hairline border and 32px corners, a green check,
// "You're signed in." and the close-this-window copy, 24px apart inside
// 32px padding. The mockup's account pill (avatar + username) is left out:
// the page goes out the instant the redirect lands, before the code is
// exchanged, so nobody knows yet who signed in. A redirect without a code
// (the user backed out, or the server named an error) gets the same card
// with no check and copy to match.
juce::String landingPage(bool signedIn) {
  juce::String page;
  page << "<!doctype html><html><head><meta charset=\"utf-8\"><title>TONE3000</title>"
          "<style>"
          "body{margin:0;min-height:100vh;background:#000;color:#fff;font:16px/1.4 Arial,sans-serif;"
          "display:flex;align-items:center;justify-content:center;text-align:center}"
          ".card{box-sizing:border-box;width:320px;padding:32px;border:1px solid rgba(84,84,88,.65);"
          "border-radius:32px;display:flex;flex-direction:column;align-items:center;gap:24px;"
          "transform:translateY(-91px)}"
          "h1{margin:0;font:bold 20px/1.4 Arial,sans-serif}"
          "p{margin:0}"
          "</style></head><body><div class=\"card\">";
  if (signedIn)
    // Lucide `check`, 48px, Colors/Green.
    page << "<svg width=\"48\" height=\"48\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#00d13b\" stroke-width=\"2\""
            " stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M20 6 9 17l-5-5\"/></svg>";
  page << "<h1>" << (signedIn ? "You\xe2\x80\x99re signed in." : "Sign-in didn\xe2\x80\x99t finish.") << "</h1>"
       << "<p>You can close this window and return to the TONE3000 Plugin.</p>"
          "</div><script>window.close()</script></body></html>";
  return page;
}

juce::String httpResponse(int status, const char* reason, const juce::String& body) {
  juce::String head;
  head << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
       << "Content-Type: text/html; charset=utf-8\r\n"
       << "Content-Length: " << body.getNumBytesAsUTF8() << "\r\n"
       << "Connection: close\r\n\r\n";
  return head + body;
}
}  // namespace

LoopbackServer::LoopbackServer() : juce::Thread("oauth-loopback") {}

LoopbackServer::~LoopbackServer() {
  masterReference.clear();
  stop();
}

bool LoopbackServer::start() {
  stop();
  listener_ = std::make_unique<juce::StreamingSocket>();
  // Port 0: the OS picks a free ephemeral port; read it back.
  if (!listener_->createListener(0, "127.0.0.1")) {
    listener_.reset();
    return false;
  }
  port_ = listener_->getBoundPort();
  startThread();
  return true;
}

void LoopbackServer::stop() {
  signalThreadShouldExit();
  if (listener_) listener_->close();  // unblocks waitForNextConnection
  stopThread(kAcceptPollMs * 4);
  listener_.reset();
  port_ = 0;
}

juce::String LoopbackServer::redirectUri() const { return "http://localhost:" + juce::String(port_) + "/"; }

void LoopbackServer::run() {
  while (!threadShouldExit() && listener_ != nullptr) {
    if (listener_->waitUntilReady(true, kAcceptPollMs) != 1) continue;
    std::unique_ptr<juce::StreamingSocket> client(listener_->waitForNextConnection());
    if (client == nullptr) continue;
    serve(*client);
  }
}

void LoopbackServer::serve(juce::StreamingSocket& client) {
  // Read the request head (the browser sends one small GET).
  juce::MemoryBlock buffer;
  char chunk[1024];
  const auto deadline = juce::Time::getMillisecondCounter() + kReadTimeoutMs;
  while (juce::Time::getMillisecondCounter() < deadline && !threadShouldExit()) {
    if (client.waitUntilReady(true, 100) != 1) continue;
    const int n = client.read(chunk, sizeof(chunk), false);
    if (n <= 0) break;
    buffer.append(chunk, static_cast<size_t>(n));
    if (juce::String::fromUTF8(static_cast<const char*>(buffer.getData()), static_cast<int>(buffer.getSize()))
            .contains("\r\n\r\n"))
      break;
  }
  const auto request = juce::String::fromUTF8(static_cast<const char*>(buffer.getData()), static_cast<int>(buffer.getSize()));
  const auto requestLine = request.upToFirstOccurrenceOf("\r\n", false, false);
  const auto target = requestLine.fromFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf(" ", false, false);
  const auto path = target.upToFirstOccurrenceOf("?", false, false);
  if (!requestLine.startsWith("GET ") || path != "/") {
    const auto reply = httpResponse(404, "Not Found", "");
    client.write(reply.toRawUTF8(), static_cast<int>(reply.getNumBytesAsUTF8()));
    return;
  }
  const auto query = target.fromFirstOccurrenceOf("?", false, false);
  // A code means success as far as the page can tell (the state check and
  // the exchange come after); the session judges the rest.
  const bool signedIn = juce::URL("http://localhost/?" + query).getParameterNames().contains("code");
  const auto reply = httpResponse(200, "OK", landingPage(signedIn));
  client.write(reply.toRawUTF8(), static_cast<int>(reply.getNumBytesAsUTF8()));
  juce::MessageManager::callAsync([self = juce::WeakReference<LoopbackServer>(this), query] {
    if (self != nullptr && self->onCallback) self->onCallback(query);
  });
  signalThreadShouldExit();  // one redirect per flow
}

}  // namespace t3k::ui
