// Stale-reply guard for async work (the web's `seq` refs and `alive` flags):
// a view owns a scope, wraps the callbacks it hands to a service in it, and
// every callback bound before the last reset() (or the scope's death) is
// dropped on arrival. A swap mid-flight can't surface the old tone's info,
// and nothing ever calls back into a destroyed component.
//
// Message-thread only; services already hop to the message thread before
// invoking callbacks.
#pragma once

#include <functional>
#include <memory>
#include <utility>

namespace t3k::ui {

class AsyncScope {
public:
  AsyncScope() : alive_(std::make_shared<bool>(true)) {}
  ~AsyncScope() { *alive_ = false; }
  AsyncScope(const AsyncScope&) = delete;
  AsyncScope& operator=(const AsyncScope&) = delete;

  // Orphan everything bound so far.
  void reset() {
    *alive_ = false;
    alive_ = std::make_shared<bool>(true);
  }

  // Wrap any callable; the result converts to whatever std::function the
  // service takes.
  template <typename Fn>
  auto wrap(Fn fn) const {
    return [alive = alive_, callable = std::move(fn)](auto&&... args) {
      if (*alive) callable(std::forward<decltype(args)>(args)...);
    };
  }

private:
  std::shared_ptr<bool> alive_;
};

}  // namespace t3k::ui
