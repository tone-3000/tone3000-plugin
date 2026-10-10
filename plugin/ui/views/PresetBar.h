// Top-bar preset controls (port of PresetBar.tsx): ‹ name › pill, save and
// New buttons, with two anchored panels: the save popover (name + save) and
// the preset browser (views/preset/PresetBrowsePanel: favourites, user
// categories, the TONE3000 factory section, drag-and-drop filing, reorder
// and bulk-select modes).
//
// Pure view over PresetStore + the active preset from ChainStore. Prev/next
// walk the list in its shown order (user section first, then factory, same
// as the native list), which is also what MIDI program-change numbers follow.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class PresetBrowsePanel;

class PresetBar : public juce::Component,
                  private PresetStore::Listener,
                  private ChainStore::Listener,
                  private UiPrefs::Listener {
public:
  static constexpr int kHeight = 36;
  static constexpr int kPillWidth = 202;
  static constexpr int kWidth = kPillWidth + 8 + 28 + 8 + 28;

  explicit PresetBar(Services& services);
  ~PresetBar() override;

  // Save / load leave the tuner (and load leaves any takeover) first; the
  // owner wires these so the result is visible. Defaults: plain store calls.
  std::function<void()> beforeSave;
  std::function<void()> beforeLoad;
  // New: back to the factory-default state.
  std::function<void()> onReset;

  void openSavePanel();
  void openBrowsePanel();
  void closePanels();

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class SavePanel;
  class Chevron;
  class NameButton;

  void presetsChanged(const std::vector<PresetInfo>&) override;
  void chainChanged(const ChainState&) override;
  void prefChanged(const juce::String& key) override;

  const std::optional<ActivePreset>& active() const { return services_.chain.state().preset; }
  const std::vector<PresetInfo>& presets() const { return services_.presets.presets(); }
  void step(int direction);
  void loadAndClose(const juce::String& id);
  void refreshChrome();

  Services& services_;
  std::unique_ptr<Chevron> prev_, next_;
  std::unique_ptr<NameButton> name_;
  IconButton save_{Icon::Save, 28};
  IconButton newButton_{Icon::Plus, 28};
  std::unique_ptr<SavePanel> savePanel_;
  std::unique_ptr<PresetBrowsePanel> browsePanel_;
};

}  // namespace t3k::ui
