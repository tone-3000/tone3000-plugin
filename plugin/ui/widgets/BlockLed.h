// Clip latch indicator for gallery tiles (BlockLed.tsx): a red dot that shows
// only while the block's clip is latched; click to clear. No bezel and no
// level metering: the tile's inset energy glow (meter::energy) handles the
// realtime picture.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/MeterStore.h"

namespace t3k::ui {

class BlockLed : public juce::Component, private MeterStore::Listener {
public:
  static constexpr int kSize = 10;

  BlockLed(MeterStore& meters, juce::String id, int size = kSize);
  ~BlockLed() override;

  void paint(juce::Graphics& g) override;
  void mouseDown(const juce::MouseEvent& e) override;
  // Keyboard / screen reader while latched (see clipA11y in DotMeter.h): a
  // "Clear clip" button; Enter, Space or the press action clears it.
  bool keyPressed(const juce::KeyPress& key) override;
  void focusGained(FocusChangeType) override { repaint(); }
  void focusLost(FocusChangeType) override { repaint(); }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  void meterChanged(const juce::String& id) override;
  void sync();

  MeterStore& meters_;
  juce::String id_;
  bool latched_ = false;
};

}  // namespace t3k::ui
