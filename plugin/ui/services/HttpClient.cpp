#include "HttpClient.h"

namespace t3k::ui {

namespace {
// Enough for the browser's parallel bursts (user + stream + version) without
// a stalled request holding up the connection probe.
constexpr int kThreads = 3;
constexpr int kShutdownGraceMs = 2000;
}  // namespace

class HttpClient::Job : public juce::ThreadPoolJob {
public:
  Job(juce::WeakReference<HttpClient> owner, HttpRequest request, std::function<void(HttpResponse)> onDone)
      : ThreadPoolJob("http"), owner_(std::move(owner)), request_(std::move(request)), onDone_(std::move(onDone)) {}

  JobStatus runJob() override {
    HttpResponse response;
    const auto started = juce::Time::getMillisecondCounter();
    auto url = request_.url;
    juce::String headers;
    for (const auto& key : request_.headers.getAllKeys())
      headers << key << ": " << request_.headers[key] << "\r\n";
    if (request_.body.isNotEmpty()) url = url.withPOSTData(request_.body);
    if (request_.contentType.isNotEmpty()) headers << "Content-Type: " << request_.contentType << "\r\n";
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders(headers)
                       .withConnectionTimeoutMs(request_.timeoutMs)
                       .withStatusCode(&response.status)
                       .withHttpRequestCmd(request_.method)
                       .withNumRedirectsToFollow(5)
                       .withProgressCallback([this](int, int) { return !shouldExit(); });
    if (auto stream = url.createInputStream(options)) {
      response.body = stream->readEntireStreamAsString();
    } else {
      response.status = 0;
    }
    if (shouldExit()) return jobHasFinished;
    response.elapsedMs = static_cast<int>(juce::Time::getMillisecondCounter() - started);
    juce::MessageManager::callAsync([owner = owner_, onDone = std::move(onDone_), response]() mutable {
      if (owner != nullptr) onDone(std::move(response));
    });
    return jobHasFinished;
  }

private:
  juce::WeakReference<HttpClient> owner_;
  HttpRequest request_;
  std::function<void(HttpResponse)> onDone_;
};

HttpClient::HttpClient() : pool_(kThreads) {}

HttpClient::~HttpClient() {
  // Cancels in-flight reads through the progress callback; a socket stuck in
  // connect() is abandoned after the grace period rather than blocking the
  // editor's close.
  masterReference.clear();
  pool_.removeAllJobs(true, kShutdownGraceMs);
}

void HttpClient::send(HttpRequest request, std::function<void(HttpResponse)> onDone) {
  pool_.addJob(new Job(juce::WeakReference<HttpClient>(this), std::move(request), std::move(onDone)), true);
}

}  // namespace t3k::ui
