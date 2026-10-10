#include "LoadingDots.h"

#include <cmath>

namespace t3k::ui {

namespace {
constexpr double kPeriodS = 1.4;
constexpr double kStaggerS = 0.2;
constexpr float kDim = 0.2f;
const juce::Colour kDotColour(0xffe4e4e7);
constexpr int kFps = 30;
}  // namespace

LoadingDots::LoadingDots() {
  setSize(kWidth, kDot);
  setInterceptsMouseClicks(false, false);
  setAccessible(false);
}

// @keyframes: 0% → 0.2, 20% → 1, 100% → 0.2, with `both` fill so a dot
// still waiting on its delay holds the 0% value.
float LoadingDots::opacityAt(int i, double t) {
  const double local = t - i * kStaggerS;
  if (local < 0) return kDim;
  const double phase = std::fmod(local, kPeriodS) / kPeriodS;
  if (phase < 0.2) return kDim + static_cast<float>(phase / 0.2) * (1.0f - kDim);
  return 1.0f - static_cast<float>((phase - 0.2) / 0.8) * (1.0f - kDim);
}

void LoadingDots::paint(juce::Graphics& g) {
  const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startSeconds_;
  for (int i = 0; i < 3; ++i) {
    g.setColour(kDotColour.withAlpha(opacityAt(i, t)));
    g.fillEllipse(static_cast<float>(i * (kDot + 2 * kMargin) + kMargin), 0.0f,
                  static_cast<float>(kDot), static_cast<float>(kDot));
  }
}

void LoadingDots::visibilityChanged() { syncTimer(); }
void LoadingDots::parentHierarchyChanged() { syncTimer(); }

void LoadingDots::syncTimer() {
  const bool animate = isShowing() || (isVisible() && getParentComponent() != nullptr);
  if (animate && !isTimerRunning()) {
    startSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz(kFps);
  } else if (!animate) {
    stopTimer();
  }
}

}  // namespace t3k::ui
