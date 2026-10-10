#include "ImageLoader.h"

#include <algorithm>

#include "core/Bitmap.h"

namespace t3k::ui {

namespace {
constexpr int kConnectTimeoutMs = 8000;
constexpr int kShutdownGraceMs = 2000;

size_t bytesOf(const juce::Image& image) {
  return image.isValid() ? static_cast<size_t>(image.getWidth()) * static_cast<size_t>(image.getHeight()) * 4u : 0u;
}
}  // namespace

// One URL's fetch + decode + resample. Skips itself when every requester
// cancelled before it started (a page the user already left).
class ImageLoader::Job : public juce::ThreadPoolJob {
public:
  Job(ImageLoader& owner, juce::String url, int side)
      : ThreadPoolJob("t3k image"), owner_(owner), url_(std::move(url)), side_(side) {}

  JobStatus runJob() override {
    {
      const juce::ScopedLock sl(owner_.lock_);
      const bool wanted = std::any_of(owner_.pending_.begin(), owner_.pending_.end(),
                                      [this](const Pending& p) { return p.url == url_; });
      if (!wanted) {
        owner_.queued_.erase(url_);
        return jobHasFinished;
      }
    }
    auto image = owner_.produce(url_, side_, *this);
    if (shouldExit()) return jobHasFinished;  // the loader is going away; nothing to tell
    juce::WeakReference<ImageLoader> self(&owner_);
    juce::MessageManager::callAsync([self, url = url_, image] {
      if (self != nullptr) self->deliver(url, image);
    });
    return jobHasFinished;
  }

private:
  ImageLoader& owner_;
  juce::String url_;
  int side_;
};

ImageLoader::ImageLoader() : pool_(kThreads) {}

ImageLoader::~ImageLoader() {
  // Cancels in-flight reads through the progress callback; a socket stuck in
  // connect() is abandoned after the grace period rather than blocking the
  // editor's close (HttpClient does the same).
  masterReference.clear();
  pool_.removeAllJobs(true, kShutdownGraceMs);
}

std::optional<juce::Image> ImageLoader::cached(const juce::String& url) const {
  const juce::ScopedLock sl(lock_);
  auto it = cache_.find(url);
  if (it == cache_.end()) return std::nullopt;
  return it->second;
}

void ImageLoader::Request::cancel() {
  if (loader_ == nullptr) return;
  const juce::ScopedLock sl(loader_->lock_);
  auto& pending = loader_->pending_;
  pending.erase(std::remove_if(pending.begin(), pending.end(), [this](const Pending& p) { return p.id == id_; }),
                pending.end());
  loader_ = nullptr;
}

void ImageLoader::load(const juce::String& url, int side, Request& request,
                       std::function<void(const juce::Image&)> onDone) {
  request.cancel();
  if (url.isEmpty() || side <= 0) {
    onDone({});
    return;
  }
  if (auto hit = cached(url)) {
    onDone(*hit);
    return;
  }
  bool enqueue = false;
  {
    const juce::ScopedLock sl(lock_);
    request.loader_ = this;
    request.id_ = nextId_++;
    pending_.push_back({request.id_, url, std::move(onDone)});
    enqueue = queued_.insert(url).second;
  }
  if (enqueue) pool_.addJob(new Job(*this, url, side), true);
}

// Worker thread.
juce::Image ImageLoader::produce(const juce::String& url, int side, Job& job) {
  if (offline) return {};
  juce::Image source;
  std::optional<juce::Image> local;
  if (localOverride) local = localOverride(url);
  if (local) {
    source = *local;
  } else {
    juce::URL target(url);
    auto stream = target.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                               .withConnectionTimeoutMs(kConnectTimeoutMs)
                                               .withProgressCallback([&job](int, int) { return !job.shouldExit(); }));
    if (stream == nullptr) return {};
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes);
    source = juce::ImageFileFormat::loadFrom(bytes.getData(), bytes.getSize());
  }
  if (!source.isValid()) return {};
  const int s = std::min({side, source.getWidth(), source.getHeight()});
  return bitmap::cover(source, s, s, 1.0f);
}

// Message thread: cache the result and fire every waiter for this URL, one
// at a time so a callback that destroys another waiter's Request still
// cancels it.
void ImageLoader::deliver(const juce::String& url, juce::Image image) {
  {
    const juce::ScopedLock sl(lock_);
    queued_.erase(url);
    if (cache_.emplace(url, image).second) {
      order_.push_back(url);
      totalBytes_ += bytesOf(image);
      while ((totalBytes_ > kBudgetBytes || order_.size() > kMaxEntries) && order_.size() > 1) {
        auto oldest = cache_.find(order_.front());
        totalBytes_ -= bytesOf(oldest->second);
        cache_.erase(oldest);
        order_.erase(order_.begin());
      }
    }
  }
  for (;;) {
    std::function<void(const juce::Image&)> onDone;
    {
      const juce::ScopedLock sl(lock_);
      auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.url == url; });
      if (it == pending_.end()) return;
      onDone = std::move(it->onDone);
      pending_.erase(it);
    }
    onDone(image);
  }
}

}  // namespace t3k::ui
