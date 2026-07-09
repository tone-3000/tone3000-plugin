#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
 * Bounded undo/redo history of chain snapshots.
 *
 * Ownership/threading: owned by the processor and only touched while
 * `chainMutex` is held, so it needs no locking of its own. Entries are
 * settings-only ValueTrees (tone JSON + params, never model bytes), so a full
 * stack costs at most a few hundred KB.
 *
 * Undo model: mutators push the *pre-mutation* snapshot. Undo hands back the
 * top entry and stores the caller's current snapshot on the redo stack; redo
 * mirrors it. A new push clears the redo stack (branching discards the
 * redoable future, like every editor).
 *
 * Coalescing: continuous gestures (knob and EQ-dot drags) arrive as dozens of
 * mutations per second. Each passes a stable key ("param:<blockId>:<name>");
 * while the top undo entry carries the same key and keeps being touched
 * within the window, no new entry is pushed — one gesture, one undo step.
 * Structural edits pass an empty key and always push.
 */
class ChainHistory {
public:
  static constexpr size_t kMaxDepth = 64;
  static constexpr juce::int64 kCoalesceWindowMs = 1500;

  bool canUndo() const noexcept { return !undoStack.empty(); }
  bool canRedo() const noexcept { return !redoStack.empty(); }

  /** True when this mutation continues the gesture already captured on top of
      the undo stack — the caller should skip snapshotting entirely. */
  bool shouldCoalesce(const juce::String& key) {
    if (key.isEmpty() || undoStack.empty() || !redoStack.empty())
      return false;
    Entry& top = undoStack.back();
    const juce::int64 now = juce::Time::currentTimeMillis();
    if (top.key != key || now - top.lastTouchMs > kCoalesceWindowMs)
      return false;
    top.lastTouchMs = now;  // sliding window: the gesture is still going
    return true;
  }

  /** Push the pre-mutation snapshot. Clears the redo stack and trims the
      oldest entries beyond kMaxDepth. */
  void push(juce::ValueTree preMutationState, const juce::String& key) {
    redoStack.clear();
    undoStack.push_back({std::move(preMutationState), key, juce::Time::currentTimeMillis()});
    if (undoStack.size() > kMaxDepth)
      undoStack.erase(undoStack.begin());
  }

  /** Only call when canUndo(). Stores `currentState` for redo and returns the
      snapshot to restore. */
  juce::ValueTree undo(juce::ValueTree currentState) {
    redoStack.push_back({std::move(currentState), {}, 0});
    juce::ValueTree out = std::move(undoStack.back().state);
    undoStack.pop_back();
    return out;
  }

  /** Only call when canRedo(). Stores `currentState` for undo and returns the
      snapshot to restore. */
  juce::ValueTree redo(juce::ValueTree currentState) {
    undoStack.push_back({std::move(currentState), {}, 0});
    juce::ValueTree out = std::move(redoStack.back().state);
    redoStack.pop_back();
    return out;
  }

  /** Drop everything (e.g. after a project/state load — undoing across it
      would resurrect a chain the user never saw in this session). */
  void clear() {
    undoStack.clear();
    redoStack.clear();
  }

private:
  struct Entry {
    juce::ValueTree state;
    juce::String key;          // coalesce key ("" = never coalesce)
    juce::int64 lastTouchMs;   // last time this gesture touched the entry
  };

  std::vector<Entry> undoStack;
  std::vector<Entry> redoStack;
};
