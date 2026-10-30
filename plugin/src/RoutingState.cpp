#include "RoutingState.h"
#include <juce_data_structures/juce_data_structures.h>
#include <cstring>

namespace t3k::routing {
juce::Result cabinetBypassState(const juce::MemoryBlock& bytes,
                               const std::array<std::string, 2>& ids,
                               juce::MemoryBlock& result) {
  const auto fail = [](const juce::String& error) { return juce::Result::fail(error); };
  if (bytes.getSize() <= 4 || std::memcmp(bytes.getData(), "T3KB", 4) != 0)
    return fail("The processor state has an unsupported format.");
  auto tree = juce::ValueTree::readFromData(static_cast<const char*>(bytes.getData()) + 4,
                                           bytes.getSize() - 4);
  if (!tree.hasType("TONE3000State") ||
      static_cast<int>(tree.getProperty("schemaVersion", 1)) > 1)
    return fail("The processor state has an unsupported schema.");
  const auto snapshot = tree.getChildWithName("ChainSnapshot");
  if (!snapshot.isValid())
    return fail("The processor state has no chain snapshot.");

  const bool stereo = static_cast<bool>(snapshot.getProperty("stereoEnabled", false));
  const int renderedLanes = stereo ? 2 : 1;
  const std::array<juce::ValueTree, 2> lanes = {
      snapshot.getChildWithName("ChainBlocks"),
      snapshot.getChildWithName("RightChainBlocks")};
  const int trunk = snapshot["branchSide"].toString() == "right" ? 1 : 0;
  const auto anchorId = snapshot["branchAfterBlockId"].toString();
  int anchor = -1;
  if (stereo && anchorId.isNotEmpty()) {
    const auto& blocks = lanes[static_cast<size_t>(trunk)];
    for (int i = 0; i < blocks.getNumChildren(); ++i)
      if (blocks.getChild(i)["id"].toString() == anchorId &&
          blocks.getChild(i)["type"].toString() != "insert") {
        anchor = i;
        break;
      }
    if (anchor < 0)
      return fail("The chain branch anchor no longer exists. Reassign routing before cabinet bypass.");
  }

  std::array<juce::ValueTree, 2> selected;
  for (int output = 0; output < renderedLanes; ++output) {
    const auto& id = ids[static_cast<size_t>(output)];
    const auto laneName = output == 0 ? "Left" : "Right";
    if (id.empty())
      return fail(juce::String("Select a separate cabinet for the ") + laneName + " rendered lane.");

    // Reject duplicate physical UUIDs rather than accidentally disabling
    // more than one block. A shared prefix is one node used by two paths.
    juce::ValueTree physicalBlock;
    int physicalLane = -1, physicalIndex = -1, matches = 0;
    for (int lane = 0; lane < 2; ++lane) {
      const auto& blocks = lanes[static_cast<size_t>(lane)];
      for (int i = 0; i < blocks.getNumChildren(); ++i)
        if (blocks.getChild(i)["id"].toString().toStdString() == id) {
          physicalBlock = blocks.getChild(i);
          physicalLane = lane;
          physicalIndex = i;
          ++matches;
        }
    }
    if (matches == 0)
      return fail("A selected cabinet block no longer exists. Assign it again.");
    if (matches > 1)
      return fail("A selected cabinet UUID is duplicated in the state. Assign unambiguous blocks first.");

    const bool ownLane = physicalLane == output;
    const bool sharedPrefix = stereo && anchor >= 0 && output != trunk &&
        physicalLane == trunk && physicalIndex <= anchor;
    if (!ownLane && !sharedPrefix)
      return fail(juce::String("The selected cabinet is not on the ") + laneName +
                  " rendered lane's effective signal path.");

    const auto type = physicalBlock["type"].toString();
    const auto tone = juce::JSON::parse(physicalBlock["toneJson"].toString());
    if (type != "ir" && !(type == "nam" && tone["gear"].toString() == "cab"))
      return fail("The selected block is not a separate cabinet. A cabinet baked into an amp NAM cannot be removed.");
    selected[static_cast<size_t>(output)] = physicalBlock;
  }

  // Validate every required lane before changing this temporary tree.
  // Retain the selected nodes themselves so disabling a branch-anchor cab
  // never destroys or moves the anchor UUID.
  selected[0].setProperty("enabled", false, nullptr);
  if (stereo && selected[1] != selected[0])
    selected[1].setProperty("enabled", false, nullptr);
  juce::MemoryBlock next;
  {
    // External MemoryOutputStream trims its backing block to the written
    // byte count on destruction. Finish it before moving that block;
    // moving first would retain uninitialized capacity padding in result.
    juce::MemoryOutputStream stream(next, false);
    stream.write("T3KB", 4);
    tree.writeToStream(stream);
  }
  result = std::move(next);
  return juce::Result::ok();
}
}
