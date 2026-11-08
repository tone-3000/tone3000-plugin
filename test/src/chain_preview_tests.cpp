// Chain preview session tests (the browser's audition)
//
// beginChainPreview / commitChainPreview / cancelChainPreview: the contract
// is that a whole browsing session is one undo step or none.
//
//   - edits inside a session record no history; the chain state says
//     `previewing` while one is open,
//   - cancel puts the chain back to where the session began, and leaves
//     the undo stack as it was,
//   - commit pushes the session's starting point as one undo step, so undo
//     after a commit returns to before the browser opened, and redo brings
//     the auditioned result back,
//   - a session that changed nothing commits without an undo step,
//   - begin inside a session is a no-op, commit / cancel outside one fail,
//   - undo / redo are refused while a session is open (the chain state
//     says so, for the header's buttons), and work again after it,
//   - a host state load ends an open session with the history it clears.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

float mixOf(const TONE3000Processor& proc, const char* blockId) {
  const auto state = proc.getChainState(-1);
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["blockId"].toString() == blockId) return static_cast<float>(item["params"]["mix"]);
  ADD_FAILURE() << "block " << blockId << " not in chain state";
  return -1.0f;
}

bool canUndo(const TONE3000Processor& proc) { return static_cast<bool>(proc.getChainState(-1)["canUndo"]); }
bool canRedo(const TONE3000Processor& proc) { return static_cast<bool>(proc.getChainState(-1)["canRedo"]); }
bool previewing(const TONE3000Processor& proc) {
  return static_cast<bool>(proc.getChainState(-1)["previewing"]);
}

// A mono chain with one IR block at mix 1.0, loaded from cache.
void loadRig(ChainTestProcessor& proc) {
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeIrBlockTree("blk-a", 1, 100), nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc)) << "rig never finished loading from cache";
  ASSERT_FLOAT_EQ(mixOf(proc, "blk-a"), 1.0f);
  ASSERT_FALSE(canUndo(proc));
}

}  // namespace

TEST(ChainPreviewTest, EditsInsideASessionRecordNoHistoryAndCancelPutsThemBack) {
  ChainTestProcessor proc;
  loadRig(proc);

  EXPECT_FALSE(previewing(proc));
  EXPECT_TRUE(proc.beginChainPreview());
  EXPECT_TRUE(proc.isChainPreviewing());
  EXPECT_TRUE(previewing(proc));

  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.3));
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.6));
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 0.6f);
  EXPECT_FALSE(canUndo(proc)) << "a previewed edit must not land on the undo stack";

  EXPECT_TRUE(proc.cancelChainPreview());
  EXPECT_FALSE(proc.isChainPreviewing());
  EXPECT_FALSE(previewing(proc));
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 1.0f);
  EXPECT_FALSE(canUndo(proc)) << "cancel leaves the stack as it was";
}

TEST(ChainPreviewTest, CommitIsOneUndoStepBackToBeforeTheSession) {
  ChainTestProcessor proc;
  loadRig(proc);

  EXPECT_TRUE(proc.beginChainPreview());
  EXPECT_TRUE(proc.beginChainPreview()) << "begin inside a session is a no-op";
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.3));
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.5));

  EXPECT_TRUE(proc.commitChainPreview());
  EXPECT_FALSE(previewing(proc));
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 0.5f);
  EXPECT_TRUE(canUndo(proc));

  EXPECT_TRUE(proc.undoChain());
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 1.0f) << "one undo spans the whole session";
  EXPECT_FALSE(canUndo(proc));

  EXPECT_TRUE(proc.redoChain());
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 0.5f);
}

TEST(ChainPreviewTest, AnUnchangedSessionCommitsWithoutAnUndoStep) {
  ChainTestProcessor proc;
  loadRig(proc);

  EXPECT_TRUE(proc.beginChainPreview());
  EXPECT_TRUE(proc.commitChainPreview());
  EXPECT_FALSE(canUndo(proc));
  EXPECT_FALSE(proc.undoChain());
}

TEST(ChainPreviewTest, CommitAndCancelOutsideASessionFail) {
  ChainTestProcessor proc;
  loadRig(proc);

  EXPECT_FALSE(proc.commitChainPreview());
  EXPECT_FALSE(proc.cancelChainPreview());
  EXPECT_FALSE(previewing(proc));

  // And the ordinary path still records history outside a session.
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.2));
  EXPECT_TRUE(canUndo(proc));
}

TEST(ChainPreviewTest, UndoAndRedoAreRefusedWhileASessionIsOpen) {
  ChainTestProcessor proc;
  loadRig(proc);
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.2));
  ASSERT_TRUE(proc.undoChain());
  ASSERT_TRUE(canRedo(proc));
  ASSERT_TRUE(proc.redoChain());
  ASSERT_TRUE(canUndo(proc));

  EXPECT_TRUE(proc.beginChainPreview());
  EXPECT_FALSE(canUndo(proc)) << "the header greys undo for the session";
  EXPECT_FALSE(canRedo(proc));
  EXPECT_FALSE(proc.undoChain()) << "an undo under the session is refused";
  EXPECT_FALSE(proc.redoChain());
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 0.2f);

  EXPECT_TRUE(proc.cancelChainPreview());
  EXPECT_TRUE(canUndo(proc)) << "and comes back when the session ends";
  EXPECT_TRUE(proc.undoChain());
  EXPECT_FLOAT_EQ(mixOf(proc, "blk-a"), 1.0f);
}

TEST(ChainPreviewTest, AHostStateLoadEndsAnOpenSession) {
  ChainTestProcessor proc;
  loadRig(proc);
  EXPECT_TRUE(proc.beginChainPreview());
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.3));

  // The host restores a project: the chain is replaced wholesale and the
  // undo stack cleared; the session must not outlive either.
  loadRig(proc);
  EXPECT_FALSE(previewing(proc));
  EXPECT_FALSE(proc.commitChainPreview());
  EXPECT_FALSE(proc.cancelChainPreview());
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 0.4));
  EXPECT_TRUE(canUndo(proc)) << "history records again after the load";
}
