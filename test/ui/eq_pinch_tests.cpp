#include "core/EqPinchGesture.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using t3k::ui::EqPinchGesture;

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void near(std::optional<double> q, double expected) {
  require(q && std::abs(*q - expected) < 1e-9, "unexpected Q");
}

int main() {
  try {
    EqPinchGesture singleTouch;
    require(!singleTouch.move(0, {100, 100}, 0.1, 10) && !singleTouch.blocksSingleDrag(),
            "without touch-down events the gesture must remain inactive");
    for (int tap = 0; tap < 3; ++tap) {
      singleTouch.down(0, {0, 0}, 1, 2);
      singleTouch.down(0, {0, 0}, 1, 2); // same source is still only one finger
      for (int step = 0; step < 100; ++step) {
        require(!singleTouch.move(0, {double(step), double(step)}, 0.1, 10),
                "single-touch drag must never emit pinch Q");
        require(!singleTouch.blocksSingleDrag() && !singleTouch.band(),
                "single-touch drag must not be intercepted");
      }
      singleTouch.up(0);
      require(!singleTouch.hasContacts() && !singleTouch.blocksSingleDrag(),
              "single-touch taps and re-grabs must release normally");
    }

    EqPinchGesture gesture;
    gesture.down(7, {0, 0}, 1, 2);
    require(!gesture.blocksSingleDrag(), "one finger must still drag a dot");
    require(!gesture.move(7, {10, 0}, 0.1, 10), "one finger must not change Q");
    gesture.down(2, {110, 0}, 1, 2);
    require(gesture.blocksSingleDrag() && gesture.band() == 1, "second finger starts pinch on selected band");
    near(gesture.move(2, {210, 0}, 0.1, 10), 1);  // spread: twice the width
    near(gesture.move(7, {160, 0}, 0.1, 10), 4);  // either finger can narrow

    // Translating/rotating the pair without changing its separation restores Q.
    gesture.move(7, {30, 40}, 0.1, 10);
    near(gesture.move(2, {90, 120}, 0.1, 10), 2);
    near(gesture.move(2, {30, 40}, 0.1, 10), 10); // coincident fingers: finite, clamped
    near(gesture.move(2, {100000, 40}, 0.1, 10), 0.1);

    gesture.down(9, {0, 0}, 5, 9);
    require(gesture.band() == 1, "third finger must not retarget pinch");
    require(!gesture.move(9, {500, 500}, 0.1, 10), "third finger must not change Q");
    gesture.up(2);
    require(gesture.blocksSingleDrag() && !gesture.band(), "lifting one finger suspends editing");
    require(!gesture.move(7, {500, 0}, 0.1, 10), "remaining finger must not edit");
    gesture.down(4, {600, 0}, 3, 8);
    require(!gesture.band(), "replacing a lifted finger must not restart mid-gesture");
    gesture.up(7);
    gesture.up(9);
    gesture.up(4);
    require(!gesture.blocksSingleDrag() && !gesture.hasContacts(), "all releases clear gesture");

    gesture.down(2, {0, 0}, 4, 3);
    gesture.down(7, {100, 0}, 4, 3);
    near(gesture.move(7, {200, 0}, 0.1, 10), 1.5);
    require(gesture.band() == 4, "new gesture takes current band and Q");
    gesture.up(2); // opposite release order
    gesture.up(7);
    require(!gesture.blocksSingleDrag(), "release order must not matter");

    gesture.down(0, {0, 0}, 2, 0.71);
    gesture.down(1, {0, 0}, 2, 0.71);
    near(gesture.move(1, {1, 0}, 0.1, 10), 0.71);
    require(!gesture.move(99, {1, 0}, 0.1, 10), "unknown source ignored");
    std::cout << "EQ pinch gesture tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
