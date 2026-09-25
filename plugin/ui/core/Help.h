// Hint-bar copy (port of helpText.ts). Every control
// publishes a one-line hint while hovered; all wording lives here so it stays
// consistent. Desktop copy is authored once and re-worded for touch devices
// in a single pass (`Right-click` -> `Touch and hold`, `click` -> `tap`).
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui::help {

enum class Key {
  // Faceplate: gains
  inputLevel, inputMode, outputLevel, outputBalance, autoBalance,
  // Faceplate: gate, transpose, tone stack, stereo image (spread / align)
  gate, gatePower, gateRelease, gateHold, gateRange,
  transpose, transposePower, transposeFine, transposeTonality, transposeWindow,
  toneBass, toneMiddle, toneTreble, tonePower,
  spreadOffset, spreadWobble, spreadWobblePower, spreadCrossover, spreadCrossoverPower,
  spreadDiffuse, spreadAdvert, spreadPower, imageCorrelation, spreadMonoOutput,
  alignOffset, alignWobble, alignWobblePower, alignCrossover, alignCrossoverPower,
  alignDiffuse, alignAdvert, alignPower, autoAlign,
  // Top bar
  tuner, undo, redo, settings, account, monoMode, stereoMode,
  // Presets
  presetPrev, presetNext, presetBrowse, presetSave, presetNew, presetRename, presetDelete,
  presetReorder, presetDrag, presetPcToggle, presetPc,
  // Tone browser
  browserSearch, browserSearchProfile, browserMoreFilters, browserFewerFilters, browserVerified, browserProfile, browserGear,
  browserSort, browserFormat, browserTags, browserMakes, browserCreators, browserCalibrated,
  browserClearFilter, browserProfileLocked, browserCalibratedIr,
  // Chain gallery
  addTile, closeToneBrowser, copyBlock, pasteBlock, loadFileTile, loadFolderTile, blockPower,
  retryLoad, swapTone, removeBlock, panLeft, panRight, panLink, monoSum, panMonoSum, soloLeft,
  soloRight, invertLeft, invertRight, swapChains, branchGap, branchJunction,
  // Block card
  blockIn, blockOut, blockOutIr, blockMix, blockNormalize, blockNormalizeOverridden, blockSize,
  blockSizeChip, blockCalibrated, blockUncalibrated, eqToggle, toneInfo, toneInfoLogin, viewOnT3k,
  favoriteTone, unfavoriteTone, eqSlidersView, eqCurveView, eqReset, eqPre, eqPower, shareTone,
  modelSelectSignedOut, backToChain,
  // EQ editor
  eqFader, eqFaderPass, eqDot, eqFreqChip, eqGainChip, eqQChip,
  // Meters
  clipDot,
  // The hint bar itself
  cpuLoad, hideHints,
};

// The (touch-reworded when applicable) copy for a key.
const juce::String& text(Key key);

// Gallery tile: leads with the tone's own name.
juce::String toneTile(const juce::String& title);
// Curve-type selector buttons in the EQ editor.
juce::String bandType(const juce::String& label);

// A control's name from its hint, for screen readers: the lead before the
// first ": " ("Undo: step back..." -> "Undo"), or the whole hint without one.
juce::String lead(const juce::String& hint);

// Speak a status change that only paints somewhere else (a toast, a banner,
// results landing) through the platform's screen reader, if one is running.
// Interrupts nothing: it queues after whatever is being read.
void announce(const juce::String& text);

}  // namespace t3k::ui::help
