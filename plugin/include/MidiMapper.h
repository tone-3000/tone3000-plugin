#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <vector>

/**
 * MIDI CC / note → target mapping engine with Learn, plus program-change
 * preset switching.
 *
 * Lives in the processor — not the device layer — so the same map, learn flow
 * and settings UI work identically in the standalone app (enabled MIDI inputs
 * are merged into processBlock's MidiBuffer by JUCE's standalone player) and
 * in hosts (the DAW hands us the buffer). The map serializes with plugin
 * state (getStateInformation), so it travels with DAW sessions.
 *
 * Targets come in four kinds:
 *   - APVTS parameters ("gateThreshold").
 *   - Positional block powers ("block1Power" = the first tone block in the
 *     Left lane, "rightBlock1Power" = the first in the Right lane — stereo
 *     only). Positional addressing is deliberate: block ids are ephemeral
 *     (tone swaps, preset loads), but "my second stomp bypasses block 2" is
 *     a pedalboard fact that should survive all of that.
 *   - Stereo mode ("stereoEnabled") — chain state, not a parameter, so it
 *     gets the same virtual-target treatment as block powers.
 *   - Preset steps ("presetPrevious" / "presetNext") — fire-per-press
 *     triggers that walk the preset list, for footswitches programmed with
 *     CC / note buttons instead of program changes.
 *
 * Model: one source per target (mapping identity == targetId); one physical
 * control may drive any number of targets. A global channel filter (omni by
 * default) gates everything, program changes included. Behavior is derived
 * from the pairing instead of configured:
 *   - CC on a continuous parameter → absolute (value/127 → normalized).
 *   - CC on any toggle/trigger target (boolean parameter, block power,
 *     stereo mode, preset step) → fires once per press. Press detection
 *     (see applyEvent): value ≥ 64 fires; a value < 64 also fires unless it
 *     follows a ≥ 64 value from the same control (a momentary switch's
 *     release). Covers momentary pedals (127 then 0), latching values, and
 *     footswitches that only ever send low values — the last kind used to
 *     be dropped entirely by a plain ≥ 64 gate.
 *   - Note-on on any target → toggle (continuous flips 0 ↔ 1).
 *   - Program change n → onProgramChange(n), no mapping needed (the
 *     processor loads the nth preset in list order).
 *
 * Threading: the audio thread applies mappings under a SpinLock try-lock
 * (skipping one buffer on the rare contended edit) and never mutates the map.
 * Parameter writes go through setValueNotifyingHost — the standard MIDI-learn
 * path, which hosts accept from the processing callback. Everything else is
 * deferred to the message thread via AsyncUpdater: learn captures, program
 * changes, preset steps, and block-power / stereo toggles (chain edits take
 * the chain lock and are undoable, so they must never run on the audio
 * thread). Pending toggles are parity-coalesced (XOR per block, a flip
 * counter for stereo, a signed step sum for presets) — two stomps before the
 * async hop still net out to the right state.
 */
class MidiMapper : private juce::AsyncUpdater {
public:
  explicit MidiMapper(juce::AudioProcessorValueTreeState& parameters);
  ~MidiMapper() override;

  // ── Audio thread ──
  void processMidi(const juce::MidiBuffer& midi);

  // ── Message thread (native bridge) ──
  /** { channel, learnTargetId, mappings: [{ targetId, source, number }] } */
  juce::var getState() const;
  /** Global channel filter: 0 = omni, 1–16 = that channel only. */
  void setChannelFilter(int channel);
  /** Arm learn for a target: the next CC / note-on (passing the channel
      filter) wins and replaces any existing mapping for that target. Arming
      a second target re-targets the same armed listen. */
  void startLearn(const juce::String& targetId);
  void cancelLearn();
  bool removeMapping(const juce::String& targetId);

  // ── Plugin state ──
  juce::ValueTree toValueTree() const;
  /** Replaces the whole map (an invalid/missing tree clears it — a project
      without mappings must not inherit the previous session's). Unknown
      target ids are dropped. Safe off the message thread. */
  void restoreFromValueTree(const juce::ValueTree& tree);

  // ── Message-thread hooks (set once by the owning processor / editor) ──
  /** Fired after any change to the map, channel filter or learn state. The
      editor forwards it to the webview so the settings UI refreshes without
      polling. */
  std::function<void()> onChanged;
  /** Program change n arrived (post channel filter). */
  std::function<void(int program)> onProgramChange;
  /** A mapped block-power control fired for the given chain position
      (rightLane = the Right lane's Nth tone block, stereo mode only). */
  std::function<void(int blockIndex, bool rightLane)> onBlockPowerToggle;
  /** A mapped stereo-mode control fired (net of parity coalescing). */
  std::function<void()> onStereoToggle;
  /** Mapped preset prev/next controls fired; delta is the net step count
      (+1 per next press, -1 per previous — coalesced like the toggles). */
  std::function<void(int delta)> onPresetStep;

private:
  enum class Source : int { cc = 0, note = 1 };
  enum class Kind : int { parameter = 0, blockPower = 1, stereoMode = 2, presetStep = 3 };

  /** Virtual target id for the chain's stereo on/off (chain state, not an
      APVTS parameter — the UI catalog uses the same id). */
  static constexpr const char* kStereoTarget = "stereoEnabled";
  /** Virtual target ids for preset stepping (the UI catalog uses the same
      ids). Triggers, not toggles: each press walks the preset list. */
  static constexpr const char* kPresetPrevTarget = "presetPrevious";
  static constexpr const char* kPresetNextTarget = "presetNext";

  struct Mapping {
    juce::String targetId;
    Kind kind = Kind::parameter;
    juce::RangedAudioParameter* param = nullptr;  // Kind::parameter only
    int blockIndex = -1;                          // Kind::blockPower only
    bool rightBlock = false;                      // Kind::blockPower only: Right lane
    int presetDelta = 0;                          // Kind::presetStep only: +1 / -1
    Source source = Source::cc;
    int number = 0;       // CC number or note number
    bool toggle = false;  // derived: non-parameter kind, boolean param, or note source
    // Audio-thread scratch for CC press detection (see applyEvent): the last
    // controller value seen for this mapping, -1 before the first event.
    int lastCcValue = -1;
  };

  static Source sourceFromString(const juce::String& s) {
    return s == "note" ? Source::note : Source::cc;
  }
  static const char* sourceToString(Source s) { return s == Source::note ? "note" : "cc"; }

  /** Parsed block-power target: index -1 for anything else. */
  struct BlockPowerTarget {
    int index = -1;
    bool right = false;
  };

  /** "block3Power" → {2, left}, "rightBlock3Power" → {2, right}; index -1
      for anything else. Bounded by the width of the pending-toggle bitmask. */
  static BlockPowerTarget blockPowerTargetFor(const juce::String& targetId);

  bool isValidTarget(const juce::String& targetId) const {
    return blockPowerTargetFor(targetId).index >= 0 || targetId == kStereoTarget ||
           targetId == kPresetPrevTarget || targetId == kPresetNextTarget ||
           parameters.getParameter(targetId) != nullptr;
  }

  Mapping makeMapping(const juce::String& targetId, Source source, int number) const;
  void applyEvent(Mapping& mapping, const juce::MidiMessage& msg);

  /** All deferred work lands here on the message thread: commit a pending
      learn capture, deliver program changes and block toggles, and fire
      onChanged when the map/learn state actually moved. */
  void handleAsyncUpdate() override;

  /** Mark the map/learn state dirty and schedule the async notification. */
  void notifyChanged();

  juce::AudioProcessorValueTreeState& parameters;

  mutable juce::SpinLock mapLock;
  std::vector<Mapping> mappings;

  std::atomic<int> channelFilter{0};  // 0 = omni

  // Learn handshake. The audio thread disarms on first capture and records
  // the source into the atomics; handleAsyncUpdate turns them into a mapping.
  // learnTargetId is message-thread-only (set before arming, read on commit).
  std::atomic<bool> learnArmed{false};
  std::atomic<int> capturedSource{0};
  std::atomic<int> capturedNumber{-1};  // -1 = nothing captured
  juce::String learnTargetId;

  // Audio → message thread deliveries (see class comment).
  std::atomic<int> pendingProgram{-1};  // last PC wins
  // XOR-coalesced block-power toggles, bit = block position, one mask per lane.
  std::atomic<juce::uint64> pendingBlockToggles{0};
  std::atomic<juce::uint64> pendingRightBlockToggles{0};
  std::atomic<int> pendingStereoToggles{0};  // flip count; parity applies
  std::atomic<int> pendingPresetSteps{0};    // signed sum of ±1 steps
  std::atomic<bool> mapDirty{false};                 // gate for onChanged

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapper)
};
