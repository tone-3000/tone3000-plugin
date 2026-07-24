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
 * Targets come in three kinds:
 *   - APVTS parameters ("gateThreshold").
 *   - Positional block powers ("block1Power" = the first tone block in the
 *     chain — Left lane in stereo). Positional addressing is deliberate:
 *     block ids are ephemeral (tone swaps, preset loads), but "my second
 *     stomp bypasses block 2" is a pedalboard fact that should survive all
 *     of that.
 *   - Stereo mode ("stereoEnabled") — chain state, not a parameter, so it
 *     gets the same virtual-target treatment as block powers.
 *
 * Model: one source per target (mapping identity == targetId); one physical
 * control may drive any number of targets. A global channel filter (omni by
 * default) gates everything, program changes included. Behavior is derived
 * from the pairing instead of configured:
 *   - CC on a continuous parameter → absolute (value/127 → normalized).
 *   - CC on any toggle-like target (boolean parameter, block power, stereo
 *     mode) → toggle on press (value ≥ 64), so a momentary footswitch (127
 *     then 0) flips once per stomp.
 *   - Note-on on any target → toggle (continuous flips 0 ↔ 1).
 *   - Program change n → onProgramChange(n), no mapping needed (the
 *     processor loads the nth preset in list order).
 *
 * Threading: the audio thread applies mappings under a SpinLock try-lock
 * (skipping one buffer on the rare contended edit) and never mutates the map.
 * Parameter writes go through setValueNotifyingHost — the standard MIDI-learn
 * path, which hosts accept from the processing callback. Everything else is
 * deferred to the message thread via AsyncUpdater: learn captures, program
 * changes, and block-power / stereo toggles (chain edits take the chain lock
 * and are undoable, so they must never run on the audio thread). Pending
 * toggles are parity-coalesced (XOR per block, a flip counter for stereo) —
 * two stomps before the async hop still net out to the right state.
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
  /** A mapped block-power control fired for the given chain position. */
  std::function<void(int blockIndex)> onBlockPowerToggle;
  /** A mapped stereo-mode control fired (net of parity coalescing). */
  std::function<void()> onStereoToggle;

private:
  enum class Source : int { cc = 0, note = 1 };
  enum class Kind : int { parameter = 0, blockPower = 1, stereoMode = 2 };

  /** Virtual target id for the chain's stereo on/off (chain state, not an
      APVTS parameter — the UI catalog uses the same id). */
  static constexpr const char* kStereoTarget = "stereoEnabled";

  struct Mapping {
    juce::String targetId;
    Kind kind = Kind::parameter;
    juce::RangedAudioParameter* param = nullptr;  // Kind::parameter only
    int blockIndex = -1;                          // Kind::blockPower only
    Source source = Source::cc;
    int number = 0;       // CC number or note number
    bool toggle = false;  // derived: non-parameter kind, boolean param, or note source
  };

  static Source sourceFromString(const juce::String& s) {
    return s == "note" ? Source::note : Source::cc;
  }
  static const char* sourceToString(Source s) { return s == Source::note ? "note" : "cc"; }

  /** "block3Power" → 2; -1 for anything else. Bounded by the width of the
      pending-toggle bitmask. */
  static int blockPowerIndexForTarget(const juce::String& targetId);

  bool isValidTarget(const juce::String& targetId) const {
    return blockPowerIndexForTarget(targetId) >= 0 || targetId == kStereoTarget ||
           parameters.getParameter(targetId) != nullptr;
  }

  Mapping makeMapping(const juce::String& targetId, Source source, int number) const;
  void applyEvent(const Mapping& mapping, const juce::MidiMessage& msg);

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
  std::atomic<int> pendingProgram{-1};               // last PC wins
  std::atomic<juce::uint64> pendingBlockToggles{0};  // XOR-coalesced, bit = block
  std::atomic<int> pendingStereoToggles{0};          // flip count; parity applies
  std::atomic<bool> mapDirty{false};                 // gate for onChanged

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapper)
};
