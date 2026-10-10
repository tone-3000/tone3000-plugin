#include "HintBar.h"

#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPadX = 24;
constexpr int kGap = 16;
// Fixed footprint sized for the widest value ("100.0%") so digit count
// changes never shift the row.
constexpr int kCpuMinWidth = 72;
constexpr int kCpuGap = 6;
}  // namespace

// CPU readout
HintBar::CpuReadout::CpuReadout(MeterStore& meters) : meters_(meters) {
  setHelpText(help::text(help::Key::cpuLoad));
  meters_.addListener(this);
}

HintBar::CpuReadout::~CpuReadout() { meters_.removeListener(this); }

void HintBar::CpuReadout::paint(juce::Graphics& g) {
  const auto font = Fonts::sans(12);
  const auto value = juce::String(meters_.cpuPercent(), 1) + "%";
  const float wLabel = Fonts::width(font, "CPU");
  const float wValue = Fonts::width(font, value);
  const float x0 = getWidth() / 2.0f - (wLabel + kCpuGap + wValue) / 2;
  const auto row = getLocalBounds();
  paint::text(g, "CPU", row.withX(juce::roundToInt(x0)).withWidth(juce::roundToInt(wLabel) + 2), font,
              theme::kMuted);
  paint::text(g, value,
              row.withX(juce::roundToInt(x0 + wLabel + kCpuGap)).withWidth(juce::roundToInt(wValue) + 2),
              font, theme::kMuted);
}

// Bar
HintBar::HintBar(Services& services) : services_(services), cpu_(services.meters) {
  addAndMakeVisible(cpu_);
  hide_.setHelpText(help::text(help::Key::hideHints));
  hide_.onClick = [this] { services_.hints.setEnabled(false); };
  addAndMakeVisible(hide_);
  services_.hints.addListener(this);
  setSize(design::kWidth, design::kHintHeight);
}

HintBar::~HintBar() { services_.hints.removeListener(this); }

void HintBar::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  paint::hairlineH(g, 0, static_cast<float>(getWidth()), 0, theme::kBorder);
  auto area = getLocalBounds().reduced(kPadX, 0);
  area.setRight(cpu_.getX() - kGap);
  paint::text(g, services_.hints.current(), area, Fonts::sans(13), theme::kMuted);
}

void HintBar::resized() {
  auto area = getLocalBounds().reduced(kPadX, 0);
  hide_.setBounds(area.removeFromRight(hide_.getWidth()).withSizeKeepingCentre(hide_.getWidth(), hide_.getHeight()));
  area.removeFromRight(kGap);
  cpu_.setBounds(area.removeFromRight(kCpuMinWidth));
}

}  // namespace t3k::ui
