// Async artwork loader: URL → decoded juce::Image, fetched and resampled on
// a small thread pool and cached (the browser's image cache and its parallel
// connections, minus the disk). Consumers ask for a URL and get called back
// on the message thread when it resolves; a failed fetch resolves to a null
// image so the caller can fall back to a glyph (ToneImage.tsx onError).
//
// The cache holds one square per URL, cover-cropped to a canonical side for
// its kind (kArtSide, kAvatarSide): the server's originals are ~1000 px
// JPEGs the UI shows at 22-224 logical px, so caching them decoded would
// cost ~5 MB each for a 200 KB download. Consumers resample that square
// once more to their own box and pixel scale (a sub-millisecond pass from a
// 512 px source), so a window zoom or DPI change never reaches the loader.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace t3k::ui {

class ImageLoader {
public:
  ImageLoader();
  ~ImageLoader();

  // Canonical cached sides, in device pixels. Art: the 224 px gallery tile
  // on a 2x display with a little zoom headroom (beyond that it upscales
  // mildly). Avatars: the 32 px block avatar on a 2x display.
  static constexpr int kArtSide = 512;
  static constexpr int kAvatarSide = 64;

  // Result for a URL, if already known (null Image = load failed).
  std::optional<juce::Image> cached(const juce::String& url) const;

  // Handle for a pending request; destroying it drops the callback.
  class Request {
  public:
    Request() = default;
    ~Request() { cancel(); }
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    void cancel();

  private:
    friend class ImageLoader;
    ImageLoader* loader_ = nullptr;
    juce::uint64 id_ = 0;
  };

  // Resolve `url` as a `side` × `side` cover-cropped square (never
  // upscaled: a smaller original stays its own size); `onDone` runs on the
  // message thread (synchronously when the result is already cached). The
  // cache is keyed by URL alone, so a URL should always be asked for at one
  // side: art URLs and avatar URLs are disjoint, which keeps that true.
  void load(const juce::String& url, int side, Request& request, std::function<void(const juce::Image&)> onDone);

  // Testbed hooks: answer a URL locally instead of fetching (a fixture file,
  // or the suite's synthesized placeholder), and simulate the network being
  // down (fixtures' `imagesOffline`). Run on the worker threads.
  std::function<std::optional<juce::Image>(const juce::String& url)> localOverride;
  bool offline = false;

private:
  class Job;

  struct Pending {
    juce::uint64 id;
    juce::String url;
    std::function<void(const juce::Image&)> onDone;
  };

  juce::Image produce(const juce::String& url, int side, Job& job);  // worker thread
  void deliver(const juce::String& url, juce::Image image);           // message thread

  // Enough to overlap the per-request latency of a card grid (the server
  // answers in ~150-300 ms whatever the size) without a burst of 24 sockets.
  static constexpr int kThreads = 4;
  // ~45 tones at kArtSide plus any number of avatars; oldest first out.
  static constexpr size_t kBudgetBytes = 48u << 20;
  static constexpr size_t kMaxEntries = 512;  // bounds the null (failed) entries too

  juce::CriticalSection lock_;
  std::map<juce::String, juce::Image> cache_;  // null Image = failed
  std::vector<juce::String> order_;            // insertion order, for eviction
  size_t totalBytes_ = 0;
  std::set<juce::String> queued_;  // URLs with a job in the pool
  std::vector<Pending> pending_;
  juce::uint64 nextId_ = 1;
  // Declared last: its destructor stops the workers before anything they
  // touch is torn down.
  juce::ThreadPool pool_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(ImageLoader)
};

}  // namespace t3k::ui
