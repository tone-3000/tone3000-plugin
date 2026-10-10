#include "AutoMeasure.h"

#include <cmath>

namespace t3k::ui {

namespace {
constexpr int kPollMs = 200;
const juce::String kSeparator = juce::String::fromUTF8(" \xc2\xb7 ");  // " · "
}  // namespace

AutoMeasure::AutoMeasure(Backend& backend, Toast& toast, Kind kind)
    : backend_(backend), toast_(toast), kind_(kind) {}

AutoMeasure::~AutoMeasure() { stopTimer(); }

juce::String AutoMeasure::doneMessage(Kind kind, const AutoMeasureResult& result) {
  if (kind == Kind::balance) {
    // matchedDb is the measured L/R energy diff (positive = left louder), so
    // the correction lifts the quieter side by that amount relative.
    const double db = result.matchedDb.value_or(0.0);
    if (std::abs(db) < 0.05) return "Balanced";
    return "Balanced" + kSeparator + (db > 0 ? "R" : "L") + " +" + juce::String(std::abs(db), 1) + " dB";
  }
  // matchedMs is the measured inter-chain lag (positive = the right chain
  // gets delayed); below the native "already aligned" floor no delay
  // correction was applied. Two decimals: the probe measures to sub-sample
  // precision. polarityFlipped reports a chain Ø toggled alongside.
  const double ms = result.matchedMs.value_or(0.0);
  const bool flipped = result.polarityFlipped;
  juce::String line = "Aligned";
  if (std::abs(ms) >= 0.05)
    line += kSeparator + (ms > 0 ? "R" : "L") + " +" + juce::String(std::abs(ms), 2) + " ms";
  if (flipped) line += kSeparator + juce::String::fromUTF8("\xc3\x98 flipped");
  return line;
}

void AutoMeasure::setListening(bool listening) {
  if (listening_ == listening) return;
  listening_ = listening;
  if (listening)
    startTimer(kPollMs);
  else
    stopTimer();
  listeners.call([](Listener& l) { l.autoMeasureChanged(); });
}

void AutoMeasure::toggle() {
  if (listening_) {
    if (kind_ == Kind::balance)
      backend_.cancelAutoBalance();
    else
      backend_.cancelAutoOffset();
    setListening(false);
    toast_.clear();
    return;
  }
  if (kind_ == Kind::balance)
    backend_.startAutoBalance();
  else
    backend_.startAutoOffset();
  setListening(true);
  toast_.pin(kind_ == Kind::balance ? "Listening" : "Measuring");
}

void AutoMeasure::timerCallback() {
  const auto raw = kind_ == Kind::balance ? backend_.pollAutoBalance() : backend_.pollAutoOffset();
  if (raw.isVoid()) return;
  const auto res = AutoMeasureResult::parse(raw);
  if (res.state == AutoMeasureResult::State::listening) return;
  setListening(false);
  if (res.state == AutoMeasureResult::State::done)
    toast_.show(doneMessage(kind_, res));
  else
    toast_.clear();
}

}  // namespace t3k::ui
