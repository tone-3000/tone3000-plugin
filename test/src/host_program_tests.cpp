// Host program API tests (MIDI program changes in VST3 hosts).
//
// VST3 hosts never deliver program changes as MIDI: the only route is the
// wrapper's program parameter, which JUCE creates only when
// getNumPrograms() > 1, landing host selections in setCurrentProgram. These
// tests pin the contract the wrappers consume (the regression behind
// "program changes work standalone but not in the DAW", GitHub issue #38):
//
//   - the program count is a constant above 1, no matter how the preset
//     list changes (a live count would disable the VST3 mechanism outright
//     for users with fewer than two presets, and leave hosts stale),
//   - setCurrentProgram loads presets by list-order index, the same index
//     MidiMapper's raw-MIDI PC path uses, so PC n means the same tone in
//     every plugin format,
//   - re-selecting the already-active program is a no-op (hosts echo the
//     program parameter after every change; a reload would wipe knob tweaks),
//     while selecting program 0 with *nothing* active still loads (the guard
//     is identity-based, not index-based),
//   - empty program slots and out-of-range indexes are ignored,
//   - setCurrentProgram from a non-message thread defers to the message
//     thread (preset loads are heavyweight, lock-taking work),
//   - getCurrentProgram tracks the active preset through list reorders,
//     deletes and reset-to-default,
//   - program names follow the list (and its renames); slots past the end
//     read "(empty)",
//   - preset changes raise audioProcessorChanged with programChanged, the
//     hook host wrappers use to sync their program parameter/menus.
//
// The processor's preset store is re-rooted into a temp dir per test
// (setPresetStoreForTesting), so nothing touches the user's preset folder.
#include "Processor.h"

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

#include <thread>

namespace {

// Deliver pending AsyncUpdater callbacks (deferred program applies).
void pumpMessages(int ms = 20) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// Fresh temp preset root per test, deleted on destruction.
struct TempPresetDir {
  TempPresetDir()
      : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("t3k-host-program-tests-" + juce::Uuid().toString())) {
    dir.createDirectory();
  }
  ~TempPresetDir() { dir.deleteRecursively(); }
  juce::File dir;
};

float toneBass(TONE3000Processor& proc) {
  return proc.parameters.getRawParameterValue("toneBass")->load();
}

void setToneBass(TONE3000Processor& proc, float value) {
  auto* p = proc.parameters.getParameter("toneBass");
  ASSERT_NE(p, nullptr);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

// Save the current state as a user preset with toneBass at `bass`, so each
// preset's load is observable on the faceplate. Returns the preset id.
// Saving makes the preset active (same as the plugin's save popover).
juce::String savePresetWithBass(TONE3000Processor& proc, const juce::String& name, float bass) {
  setToneBass(proc, bass);
  const juce::var saved = proc.savePreset(name);
  EXPECT_TRUE(saved.isObject()) << "savePreset failed for " << name;
  return saved["id"].toString();
}

TEST(HostProgramTest, ProgramCountIsConstantAndAboveOne) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  // > 1 is the exact gate JUCE's VST3 wrapper applies before creating the
  // program parameter hosts map MIDI program changes onto; returning 1 here
  // is what made PC dead in every VST3 host. 128 covers the MIDI PC range.
  EXPECT_EQ(proc.getNumPrograms(), 128);
  ASSERT_GT(proc.getNumPrograms(), 1);

  // The wrapper sizes its program parameter once at construction, so the
  // count must not follow the (user-editable) preset list.
  const juce::String id = savePresetWithBass(proc, "Alpha", 1.0f);
  EXPECT_EQ(proc.getNumPrograms(), 128);
  EXPECT_TRUE(proc.deletePreset(id));
  EXPECT_EQ(proc.getNumPrograms(), 128);
}

TEST(HostProgramTest, SetCurrentProgramLoadsPresetsInListOrder) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  // User presets sort by name: Alpha = program 0, Beta = program 1. The
  // same order MidiMapper's PC path and the preset browser use.
  savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);  // active, program 1

  proc.setCurrentProgram(0);
  EXPECT_NEAR(toneBass(proc), 1.0f, 0.01f);
  EXPECT_EQ(proc.getCurrentProgram(), 0);

  proc.setCurrentProgram(1);
  EXPECT_NEAR(toneBass(proc), 9.0f, 0.01f);
  EXPECT_EQ(proc.getCurrentProgram(), 1);
}

TEST(HostProgramTest, ReselectingActiveProgramDoesNotReload) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);  // active, program 1

  // Hosts echo the program parameter back after every change and on session
  // restore; that echo must not reload the preset over live knob tweaks.
  setToneBass(proc, 3.0f);
  proc.setCurrentProgram(1);
  EXPECT_NEAR(toneBass(proc), 3.0f, 0.01f) << "echo of the active program reloaded the preset";

  // But the guard is preset-identity, not index: with nothing active,
  // program 0 must load even though getCurrentProgram() already reads 0.
  ASSERT_TRUE(proc.resetToDefault());
  ASSERT_EQ(proc.getCurrentProgram(), 0);
  proc.setCurrentProgram(0);
  EXPECT_NEAR(toneBass(proc), 1.0f, 0.01f) << "program 0 must load when no preset is active";
}

TEST(HostProgramTest, EmptySlotsAndOutOfRangeProgramsAreIgnored) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);  // active, program 1

  // In range but past the list end (a controller sending PC 6 with two
  // presets), and outside the program range entirely.
  for (const int program : {5, 127, -1, 128, 1000}) {
    proc.setCurrentProgram(program);
    EXPECT_NEAR(toneBass(proc), 9.0f, 0.01f) << "program " << program << " changed the state";
    EXPECT_EQ(proc.getCurrentProgram(), 1) << "program " << program << " moved the active preset";
  }
}

TEST(HostProgramTest, SetCurrentProgramDefersOffTheMessageThread) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);  // active, program 1

  // Preset loads take the chain lock and touch the undo history: a host
  // calling from another thread must not run that inline.
  std::thread worker([&proc] { proc.setCurrentProgram(0); });
  worker.join();
  EXPECT_NEAR(toneBass(proc), 9.0f, 0.01f) << "program applied off the message thread";

  pumpMessages();
  EXPECT_NEAR(toneBass(proc), 1.0f, 0.01f);
  EXPECT_EQ(proc.getCurrentProgram(), 0);
}

TEST(HostProgramTest, CurrentProgramTracksListOrderAndReset) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  const juce::String alpha = savePresetWithBass(proc, "Alpha", 1.0f);
  EXPECT_EQ(proc.getCurrentProgram(), 0);  // active Alpha at index 0

  const juce::String zulu = savePresetWithBass(proc, "Zulu", 9.0f);
  EXPECT_EQ(proc.getCurrentProgram(), 1);  // active Zulu after Alpha

  // Program numbers follow the user-facing list order, so a reorder moves
  // the active preset's program index with it.
  ASSERT_TRUE(proc.movePreset(zulu, -1));
  EXPECT_EQ(proc.getCurrentProgram(), 0);

  // Deleting the other preset keeps Zulu at index 0.
  ASSERT_TRUE(proc.deletePreset(alpha));
  EXPECT_EQ(proc.getCurrentProgram(), 0);

  // No active preset falls back to 0 (the API must return a valid index).
  ASSERT_TRUE(proc.resetToDefault());
  EXPECT_EQ(proc.getCurrentProgram(), 0);
}

TEST(HostProgramTest, ProgramNamesFollowTheListAndItsRenames) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  const juce::String alpha = savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);

  EXPECT_EQ(proc.getProgramName(0), juce::String("Alpha"));
  EXPECT_EQ(proc.getProgramName(1), juce::String("Beta"));
  EXPECT_EQ(proc.getProgramName(2), juce::String("(empty)"));
  EXPECT_EQ(proc.getProgramName(127), juce::String("(empty)"));

  // Host-side program renaming isn't supported; the call must be inert.
  proc.changeProgramName(0, "Hacked");
  EXPECT_EQ(proc.getProgramName(0), juce::String("Alpha"));

  // A rename re-sorts the section (name order), and the name snapshot the
  // menu queries go through must refresh immediately, not on its TTL.
  ASSERT_TRUE(proc.renamePreset(alpha, "Zulu"));
  EXPECT_EQ(proc.getProgramName(0), juce::String("Beta"));
  EXPECT_EQ(proc.getProgramName(1), juce::String("Zulu"));
}

TEST(HostProgramTest, RawMidiProgramChangeMatchesHostProgramIndexing) {
  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  savePresetWithBass(proc, "Alpha", 1.0f);
  savePresetWithBass(proc, "Beta", 9.0f);  // active, program 1

  // The raw-MIDI route (Standalone/AU/LV2/CLAP): PC 0 through MidiMapper
  // must land on the same preset as host program 0, end to end.
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::programChange(1, 0), 0);
  proc.midiMapper.processMidi(midi);
  pumpMessages();

  EXPECT_NEAR(toneBass(proc), 1.0f, 0.01f);
  EXPECT_EQ(proc.getCurrentProgram(), 0);
}

TEST(HostProgramTest, PresetChangesRaiseProgramChangedForHosts) {
  // updateHostDisplay(programChanged) is the hook plugin wrappers use to
  // sync their program parameter and menus; every path that moves the
  // active preset or the list order must raise it.
  struct Recorder final : juce::AudioProcessorListener {
    int programChanges = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*,
                               const juce::AudioProcessorListener::ChangeDetails& details) override {
      if (details.programChanged)
        ++programChanges;
    }
  };

  TempPresetDir tmp;
  TONE3000Processor proc;
  proc.setPresetStoreForTesting(tmp.dir);

  const juce::String alpha = savePresetWithBass(proc, "Alpha", 1.0f);
  const juce::String beta = savePresetWithBass(proc, "Beta", 9.0f);

  Recorder recorder;
  proc.addListener(&recorder);

  proc.setCurrentProgram(0);  // loads Alpha
  EXPECT_EQ(recorder.programChanges, 1);

  ASSERT_TRUE(proc.movePreset(alpha, 1));  // reorder shifts program numbers
  EXPECT_EQ(recorder.programChanges, 2);

  ASSERT_TRUE(proc.deletePreset(beta));  // later slots shift down
  EXPECT_EQ(recorder.programChanges, 3);

  ASSERT_TRUE(proc.resetToDefault());  // active cleared, index falls back
  EXPECT_EQ(recorder.programChanges, 4);

  proc.removeListener(&recorder);
}

}  // namespace
