#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>

namespace t3k::ui {

// Two touch sources control the selected band's bandwidth. Kept independent
// of JUCE so source ordering and finger-release behaviour can be tested.
class EqPinchGesture {
public:
  struct Point { double x, y; };

  void down(int source, Point position, int band, double q) {
    contacts_.insert_or_assign(source, position);
    if (engaged_ || contacts_.size() != 2) return;
    const auto first = contacts_.begin();
    const auto second = std::next(first);
    pinch_ = Pinch{first->first, second->first, band, q,
                   distance(first->second, second->second)};
    engaged_ = true;
  }

  std::optional<double> move(int source, Point position, double minQ, double maxQ) {
    const auto contact = contacts_.find(source);
    if (contact == contacts_.end()) return std::nullopt;
    contact->second = position;
    if (!pinch_ || (source != pinch_->first && source != pinch_->second)) return std::nullopt;
    const double separation = distance(contacts_.at(pinch_->first), contacts_.at(pinch_->second));
    // Spreading widens the band (lower Q); pinching narrows it (higher Q).
    return std::clamp(pinch_->q * pinch_->distance / separation, minQ, maxQ);
  }

  void up(int source) {
    contacts_.erase(source);
    if (pinch_ && (source == pinch_->first || source == pinch_->second)) pinch_.reset();
    // Never turn the remaining finger into a freq/gain drag. A new gesture
    // starts only after every contact from this one has been released.
    if (contacts_.empty()) engaged_ = false;
  }

  bool blocksSingleDrag() const { return engaged_; }
  bool hasContacts() const { return !contacts_.empty(); }
  std::optional<int> band() const { return pinch_ ? std::optional<int>(pinch_->band) : std::nullopt; }

private:
  struct Pinch {
    int first, second, band;
    double q, distance;
  };
  static double distance(Point a, Point b) {
    // Nearly coincident touches must not divide by zero or amplify jitter.
    return std::max(12.0, std::hypot(a.x - b.x, a.y - b.y));
  }
  std::map<int, Point> contacts_;
  std::optional<Pinch> pinch_;
  bool engaged_ = false;
};

}  // namespace t3k::ui
