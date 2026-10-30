#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>

#include "model/ToneTableSort.h"
#include "services/ToneSession.h"

namespace t3k::ui {

/** Virtualized, page-local catalog table. Selection inspects a tone;
    explicit Load, Return or double-click requests loading it. The widget
    owns a page copy and never mutates catalog/raw payloads or makes calls.
    Programmatic page/sort changes never emit user callbacks. */
class ToneTable : public juce::Component {
public:
  ToneTable();
  ~ToneTable() override;

  void setPage(const TonePage&, std::optional<int> selected = {});
  void setSort(std::optional<ToneColumnSort>);
  void setBusy(bool, std::optional<int> pickingId = {});
  void setAuthenticated(bool);
  void setFavoriteBusy(std::optional<int>);
  void setMessage(const juce::String&, bool error = false);

  std::optional<int> selectedToneId() const;
  const Tone* selectedTone() const;
  bool handleNavigationKey(const juce::KeyPress&);

  std::function<void(int)> onSelect, onLoad, onToggleFavorite;
  std::function<void(std::optional<ToneColumnSort>)> onSort;

  void paint(juce::Graphics&) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress&) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTable)
};

} // namespace t3k::ui
