// Asynchronous HTTP for the TONE3000 client: a request goes to a small
// thread pool, the reply comes back on the message thread. The transport is
// an interface so the client's token logic can be tested against scripted
// responses (SelfTests) and the testbed never touches the network.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace t3k::ui {

struct HttpRequest {
  juce::URL url;
  juce::String method{"GET"};
  juce::StringPairArray headers;
  // Sent as the body for POST/PUT with `contentType`.
  juce::String body;
  juce::String contentType;
  int timeoutMs = 15000;
};

struct HttpResponse {
  // 0 = no HTTP response at all (DNS, refused, TLS, timeout).
  int status = 0;
  juce::String body;
  int elapsedMs = 0;

  bool ok() const { return status >= 200 && status < 300; }
  bool failed() const { return status == 0; }
  // Parsed JSON body (void when it isn't JSON).
  juce::var json() const { return juce::JSON::parse(body); }
};

class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  // `onDone` runs on the message thread; it is dropped if the transport is
  // destroyed first.
  virtual void send(HttpRequest request, std::function<void(HttpResponse)> onDone) = 0;
};

class HttpClient final : public HttpTransport {
public:
  HttpClient();
  ~HttpClient() override;

  void send(HttpRequest request, std::function<void(HttpResponse)> onDone) override;

private:
  class Job;

  juce::ThreadPool pool_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(HttpClient)
};

}  // namespace t3k::ui
