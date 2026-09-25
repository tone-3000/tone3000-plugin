#include "Scenarios.h"


#include <map>

#include "Drive.h"
#include "views/PluginRoot.h"
#include "views/browser/FilterChip.h"
#include "widgets/DbMeter.h"
#include "widgets/DragScroller.h"

namespace t3k::ui::testbed {

Fixtures Fixtures::load(const juce::File& scenariosJson) {
  Fixtures f;
  f.root = juce::JSON::parse(scenariosJson);
  if (const auto* arr = f.root["scenarios"].getArray()) {
    for (const auto& entry : *arr) {
      Scenario s;
      s.id = entry["id"].toString();
      s.data = entry;
      s.hasDrive = static_cast<bool>(entry.getProperty("hasDrive", false));
      f.scenarios.push_back(std::move(s));
    }
  }
  return f;
}

const Scenario* Fixtures::find(const juce::String& id) const {
  for (const auto& s : scenarios)
    if (s.id == id)
      return &s;
  return nullptr;
}

namespace {

// Drive steps keyed by scenario id: what a user does from the scenario's
// initial state to reach the screen being captured.
const std::map<juce::String, Drive>& drives() {
  using namespace drive;
  static const std::map<juce::String, Drive> table = {
      {"chrome-account-signed-out",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(200);
       }},
      {"chrome-account-signed-in",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(200);
       }},
      {"chrome-preset-save",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Save Preset");
         fill(root, "Name", "Stadium Lead");
         wait(200);
       }},
      {"chrome-preset-browse",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Presets:");
         wait(300);
       }},
      {"chrome-auto-balance",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Auto Balance");
         wait(100);
       }},
      {"chrome-input-mode",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Input Mode");
         wait(200);
       }},
      {"main-meters-hot",
       [](PluginRoot& root, MockBackend&) {
         wait(150);  // a meter tick, so the latch is set
         auto* meter = dynamic_cast<DbMeter*>(
             find(root, [](juce::Component& c) { return c.getName() == "input meter"; }));
         if (meter != nullptr) hoverAt(root, *meter, meter->clipDotCentre(0));
         wait(100);
       }},
      {"chrome-gate-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Gate:", /*right=*/true);
         wait(200);
       }},
      {"chrome-spread-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Offset:", /*right=*/true);
         wait(200);
       }},
      {"chrome-align-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Align:", /*right=*/true);
         wait(200);
       }},
      {"chrome-tile-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "'02 Vox AC30/6 Top Boost.", /*right=*/true);
         wait(200);
       }},
      // The detail card opens from the seeded t3k.detailBlockId; the suite's
      // tile click (the web dropped the seed before the chain arrived) only
      // re-opens it, leaving no hover behind once the gallery is gone.
      {"main-detail", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"load-detail-loading", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"load-detail-failed", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"main-detail-info",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Info:");
         wait(400);
       }},
      {"main-detail-info-signed-out",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Info:");
         wait(400);
       }},
      {"main-detail-eq-sliders",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "EQ:");
         wait(300);
       }},
      {"main-detail-eq-curve",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "EQ:");
         wait(200);
         clickByHelp(root, "Curve:");
         wait(400);
       }},
      {"main-detail-model-select",
       [](PluginRoot& root, MockBackend&) {
         wait(200);  // the catalog list lands
         if (auto* trigger = find(root, [](juce::Component& c) { return c.getName() == "model select"; }))
           click(root, *trigger);
         wait(400);
       }},
      {"main-tuner-idle", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(200); }},
      {"main-tuner-intune", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-flat", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-sharp", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-accidental", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"load-offline-modal",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Add Tone");
         wait(300);  // the modal is rebuilt on the next loop turn
         unhover(root);
       }},
      {"load-update-notice", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"load-oauth-leaving",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         unhover(root);
       }},
      // Tone browser. Chips carry their filter's help copy; menu rows are
      // buttons named by their label.
      {"browser-picking",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "'02 Vox AC30/6 Top Boost")) click(root, *card);
         wait(600);
         unhover(root);
       }},
      {"browser-scrolled",
       [](PluginRoot& root, MockBackend&) {
         // The browser's vertical DragScroller: the one whose content is taller than it.
         auto* scroller = find(root, [](juce::Component& c) {
           auto* v = dynamic_cast<DragScroller*>(&c);
           return v != nullptr && v->getViewedComponent() != nullptr &&
                  v->getViewedComponent()->getHeight() > v->getHeight();
         });
         if (auto* v = dynamic_cast<DragScroller*>(scroller)) v->setViewPosition(0, 100);
         wait(100);
       }},
      {"browser-search-typed",
       [](PluginRoot& root, MockBackend&) {
         submit(root, juce::String::fromUTF8("Search\xe2\x80\xa6"), "vox");
         wait(400);
         unhover(root);
       }},
      {"browser-gear",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Pedal")) click(root, *chip);
         wait(400);
       }},
      {"browser-verified",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Verified:");
         wait(400);
       }},
      {"browser-profile-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(300);
       }},
      {"browser-profile-recent",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Recently used")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-profile-search",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Recently used")) click(root, *row);
         wait(300);
         submit(root, juce::String::fromUTF8("Search\xe2\x80\xa6"), "vox");
         wait(400);
         unhover(root);
       }},
      {"browser-profile-empty",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Favorites")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-filters-expanded",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(300);
         unhover(root);
       }},
      {"browser-sort-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(300);
       }},
      {"browser-sort-cleared",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(200);
         if (auto* newest = buttonNamed(root, "Newest")) click(root, *newest);
         wait(300);
         // The chip now reads Newest ×, with the clear-filter hint. Its menu
         // again, then a press on its ×.
         auto* chip = dynamic_cast<FilterChip*>(buttonNamed(root, "Newest"));
         if (chip) click(root, *chip);
         wait(200);
         if (chip && chip->onClear) chip->onClear();
         wait(300);
       }},
      {"browser-zoom-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(300);
       }},
      {"browser-sort-newest",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(200);
         if (auto* row = buttonNamed(root, "Newest")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-calibrated-ir",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Cabinet")) click(root, *chip);
         wait(300);
         clickByHelp(root, "Filters:");
         wait(400);
         unhover(root);
       }},
      {"browser-format-ir",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(200);
         if (auto* row = buttonNamed(root, "Impulse Response (IR)")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-tags-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Tags:");
         wait(400);
       }},
      {"browser-tags-picked",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Tags:");
         wait(300);
         if (auto* row = buttonNamed(root, "rock")) click(root, *row);
         wait(300);
         if (auto* chip = buttonNamed(root, "rock")) click(root, *chip);  // the chip reopens the menu, rock ticked
         wait(400);
       }},
      {"browser-scrolled",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "AMS Neve 88R LB")) {
           scrollIntoView(*card);
           hover(root, *card);
         }
         wait(300);
       }},
      {"browser-creators-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Creators:");
         wait(400);
       }},
      {"browser-format-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(300);
       }},
      // Leave the browser with a gear chip set and come back: the chip and
      // its results are as they were, with no fetch.
      {"browser-return",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Pedal")) click(root, *chip);
         wait(400);
         clickByHelp(root, "Close:");
         wait(300);
         clickByHelp(root, "Add Tone");
         wait(300);
         unhover(root);
       }},
      {"browser-filters-active-collapsed",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(200);
         if (auto* row = buttonNamed(root, "Impulse Response (IR)")) click(root, *row);
         wait(300);
         clickByHelp(root, "Fewer filters:");
         wait(400);
         unhover(root);
       }},
      {"chrome-toast-saved",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Save Preset");
         fill(root, "Name", "Stadium Lead");
         if (auto* save = buttonNamed(root, "Save")) click(root, *save);
         wait(100);
       }},

      // Settings
      {"settings-plugin", [](PluginRoot& root, MockBackend&) { openSettings(root); }},
      {"settings-advanced",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "Calibration");
       }},
      {"settings-midi-empty",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
       }},
      {"settings-midi-learning",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
       }},
      {"settings-midi-picker",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
         if (auto* picker = find(root, [](juce::Component& c) { return c.getName() == "Control to map"; }))
           click(root, *picker);
         wait(400);
       }},
      {"settings-system", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-mic-denied", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-feedback-risk", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-input-muted", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-rate96",
       [](PluginRoot& root, MockBackend&) {
         openSystemSettings(root);
         if (auto* settings = root.settings()) settings->scrollToHeading("MIDI Inputs", /*centre=*/true);
         wait(300);
       }},
  };
  return table;
}

}  // namespace

const Drive* driveFor(const juce::String& scenarioId) {
  const auto& table = drives();
  const auto it = table.find(scenarioId);
  return it == table.end() ? nullptr : &it->second;
}

}  // namespace t3k::ui::testbed
