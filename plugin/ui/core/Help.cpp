#include "Help.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "Design.h"

namespace t3k::ui::help {
namespace {

using juce::String;

constexpr bool kTouch = design::kCoarsePointer;

// The copy is full of typographic characters; juce::String(const char*)
// asserts on non-ASCII, so every literal goes through here.
String U(const char* utf8) { return String::fromUTF8(utf8); }

// OS-correct modifier chords: glyphs + hyphen on Apple platforms, spelled
// out + plus elsewhere.
String chord(const char* macGlyph, const char* name, const char* gesture) {
  return design::kAppleModifierGlyphs ? U(macGlyph) + "-" + gesture : String(name) + "+" + gesture;
}
String shift(const char* gesture) { return chord("\u21e7", "Shift", gesture); }
String alt(const char* gesture) { return chord("\u2325", "Alt", gesture); }

// Shared legend for every knob (touch has no modifiers or second button).
const String& knobKeys() {
  static const String keys =
      kTouch ? U("drag up or down: adjust · double tap: reset · tap the name: type")
             : shift("drag") + U(": fine · double-click: type · ") + alt("click") + ": reset";
  return keys;
}

String knobDesktop(const char* name, const char* desc) {
  return U(name) + ": " + U(desc) + " " + knobKeys();
}

// Desktop pointer vocabulary rewritten for touch. `Right-click` first, since
// it contains `click`; everything a right-click reaches answers a touch and
// hold on a touch screen.
String touchify(String s) {
  if (!kTouch) return s;
  return s.replace("Right-click", "Touch and hold")
      .replace("right-click", "touch and hold")
      .replace("Click", "Tap")
      .replace("click", "tap");
}

std::map<Key, String> buildTable() {
  std::map<Key, String> t;

  // Faceplate: gains
  t[Key::inputLevel] = knobDesktop("Input", "chain input level, ±24 dB.");
  t[Key::inputMode] = U("Input Mode: how the stereo source feeds the chain. Click: choose.");
  // Menu rows. The first two are the same routing (the natural one for the
  // chain mode), worded for what it does there: a mono chain sums, stereo
  // chains take one channel each.
  t[Key::inputModeSum] = U("Stereo SUM (L+R): both channels folded to mono into the chain.");
  t[Key::inputModeStereo] = U("Stereo: channel 1 feeds the Left chain, channel 2 the Right.");
  t[Key::inputModeDualMono] =
      U("Dual Mono (L&R): L and R each run the chain separately, identical stereo chains. "
        "2× CPU (spread across cores with Multi-core). Spread off.");
  t[Key::inputModeLeft] = U("Left: channel 1 on both sides.");
  t[Key::inputModeRight] = U("Right: channel 2 on both sides.");
  t[Key::outputLevel] = knobDesktop("Output", "master output level, ±24 dB.");
  t[Key::outputBalance] = knobDesktop("Balance", "level trim between chains, ±12 dB (pre-pan). Center: off.");
  t[Key::autoBalance] = U("Auto Balance: click, play ~2 s to match chain levels. Click again: cancel.");

  // Faceplate: gate, tone stack, stereo image
  t[Key::gate] = knobDesktop("Gate", "noise gate threshold, -100 to 0 dB. Right-click: advanced.");
  // Same touch caveat as the spread power below: the deck answers a hold on
  // the Gate knob only.
  t[Key::gatePower] = kTouch ? U("Gate Power: noise gate on/off.")
                             : U("Gate Power: noise gate on/off. Right-click: advanced.");
  t[Key::gateRelease] =
      knobDesktop("Release", "how fast the gate closes, 5-500 ms. Short: tight · long: natural tails.");
  t[Key::gateHold] = knobDesktop("Hold", "time the gate stays open after the signal drops, 0-200 ms.");
  t[Key::gateRange] = knobDesktop("Range", "how deep the gate closes, 20-80 dB. 80: mute · 20: tame.");
  t[Key::pitch] = knobDesktop("Pitch Shift",
                              "transpose the input ±24 semitones. STEP off: whammy sweep. Right-click: advanced.");
  t[Key::pitchPower] = kTouch ? U("Pitch Power: pitch shift on/off. On adds latency.")
                              : U("Pitch Power: pitch shift on/off. On adds latency. Right-click: advanced.");
  t[Key::pitchStep] = U("STEP: snap the shift to whole semitones (transpose). Off: smooth sweep, like a whammy.");
  t[Key::pitchTonality] =
      knobDesktop("Tonality", "highs above this stay put, 1-20 kHz. Keeps pick attack bright; Off: pure shift.");
  t[Key::pitchWindow] = knobDesktop(
      "Buffer", "shifter delay buffer, 20-60 ms. Short: less latency · long: cleaner, more latency.");
  t[Key::toneBass] = knobDesktop("Bass", "tone stack lows, 0-10: ±20 dB shelf at 150 Hz.");
  t[Key::toneMiddle] = knobDesktop("Middle", "tone stack mids, 0-10: ±15 dB bell at 425 Hz.");
  t[Key::toneTreble] = knobDesktop("Treble", "tone stack highs, 0-10: ±10 dB shelf at 1.8 kHz.");
  t[Key::tonePower] = U("Tone Stack Power: Bass/Middle/Treble on/off.");
  t[Key::spreadOffset] =
      knobDesktop("Offset", "double-track lag, ≤24 ms toward L or R. Center: off. Right-click: advanced.");
  t[Key::spreadWobble] = knobDesktop("Wobble", "humanizing delay drift, up to ±1.2 ms.");
  t[Key::spreadWobblePower] =
      U("Wobble Power: delay drift on/off. Off: a static, more comb-like double.");
  t[Key::spreadCrossover] = knobDesktop("Crossover", "lows below the cutoff stay dual-mono, 33-520 Hz.");
  t[Key::spreadCrossoverPower] =
      U("Crossover Power: off doubles the full band (lows lose mono safety).");
  t[Key::spreadDiffuse] = U("Diffuse Power: phase-decorrelates the lagged side. Off: a pure delay.");
  t[Key::spreadAdvert] = U("Spread: mono-to-stereo double via a wobbling short lag. Click: enable.");
  // On touch the advanced deck answers a hold on the Offset knob only, so the
  // power rows drop the tail touchify would turn into a false promise.
  t[Key::spreadPower] = kTouch ? U("Spread Power: spread off; collapses its controls.")
                               : U("Spread Power: spread off; collapses its controls. Right-click: advanced.");
  t[Key::imageCorrelation] =
      U("Mono safety: dim: safe · yellow: caution · red: cancellation on mono sum.");
  t[Key::spreadMonoOutput] =
      U("Spread: unavailable, the output is mono (mono track or one-channel output device).");
  t[Key::spreadDualMono] =
      U("Spread off in Dual Mono: the chain already outputs two real channels.");
  t[Key::alignOffset] = knobDesktop(
      "Offset", "corrective chain alignment, ≤24 ms toward L or R. Center: off. Right-click: advanced.");
  t[Key::alignWobble] = knobDesktop("Wobble", "humanizing drift of the align delay, up to ±1.2 ms.");
  t[Key::alignWobblePower] = U("Wobble Power: drifts the delayed chain like an ADT double-track.");
  t[Key::alignCrossover] = knobDesktop("Crossover", "lows below the cutoff skip the deck, 33-520 Hz.");
  t[Key::alignCrossoverPower] = U("Crossover Power: on keeps lows out of the delay and diffusion.");
  t[Key::alignDiffuse] = U("Diffuse Power: phase-decorrelates the delayed chain for width.");
  t[Key::alignAdvert] = U("Align: corrective chain time alignment. Click: enable.");
  t[Key::alignPower] = kTouch ? U("Align Power: align off; collapses its controls.")
                              : U("Align Power: align off; collapses its controls. Right-click: advanced.");
  t[Key::autoAlign] =
      U("Auto Align: a ½ s internal sweep time-aligns the chains and fixes inverted polarity. Click again: cancel.");

  // Top bar
  t[Key::tuner] = U("Tuner: chromatic tuner. Click again: back.");
  t[Key::undo] = U("Undo: revert last chain edit.");
  t[Key::redo] = U("Redo: re-apply undone edit.");
  t[Key::settings] = U("Settings: plugin and audio options.");
  t[Key::account] = U("Account: settings and TONE3000 sign-out.");
  t[Key::monoMode] = U("Mono: one chain, both outputs.");
  t[Key::stereoMode] = U("Stereo: independent Left/Right chains.");

  // Presets
  t[Key::presetPrev] = U("Previous Preset: step back through the list, or the Library folder the preset came from.");
  t[Key::presetNext] = U("Next Preset: step forward through the list, or the Library folder the preset came from.");
  t[Key::presetBrowse] = U("Presets: browse factory and user presets.");
  t[Key::presetSave] = U("Save Preset: store the current chain. Same name: overwrite.");
  t[Key::presetNew] = U("New: clear the chain and reset every control to its default.");
  t[Key::presetRename] = U("Rename: edit name. Enter: commit · Esc: cancel.");
  t[Key::presetDelete] = U("Delete: remove this preset.");
  t[Key::presetReorder] = U("Reorder: drag presets into a custom order. Prev/Next and MIDI follow it.");
  t[Key::presetDrag] = U("Drag: move this preset within its section.");
  t[Key::presetPcToggle] =
      U("MIDI PC: show each preset’s program change number. Prev/Next and PC follow the list order.");
  t[Key::presetPc] = U("PC: the MIDI program change number that loads this preset.");

  // Library drawer
  t[Key::library] = U("Library: your presets, captures and tones, and the libraries you imported.");
  t[Key::libraryClose] = U("Close: hide the Library.");
  t[Key::librarySearch] = U("Search: filter every library by name.");
  t[Key::libraryMenu] = U("More: link or import folders, import a library file, set the Library folder, missing files.");
  t[Key::libraryLibrary] = U("Your library: your captures, presets, favorites and linked folders. Right-click for more.");
  t[Key::librarySite] = U("TONE3000: the factory presets and the captures TONE3000 published.");
  t[Key::librarySiteCaptures] = U("Captures: tones TONE3000 published. Double-click to try one; drag it into yours to keep it.");
  t[Key::librarySitePresets] = U("Presets: the factory presets that come with the plugin.");
  t[Key::libraryLocal] = U("Local: captures made by others, on this computer, and the folders you linked.");
  t[Key::libraryMoveUp] = U("Move Up: list this library higher. You can also drag it.");
  t[Key::libraryMoveDown] = U("Move Down: list this library lower. You can also drag it.");
  t[Key::librarySetPicture] = U("Set Picture: an image for this folder, shown on blocks loaded from it.");
  t[Key::libraryCardPicture] = U("Picture: set the image for this capture's folder, shown on blocks loaded from it.");
  t[Key::libraryRemovePicture] = U("Remove Picture: go back to the TONE3000 artwork, if there is any.");
  t[Key::libraryOtherLibrary] = U("Imported library: read-only. Drag or right-click to copy things into yours.");
  t[Key::libraryFolder] = U("Folder: click to open \xc2\xb7 drag onto a block to load all its captures \xc2\xb7 right-click: more.");
  t[Key::libraryPreset] = U("Preset: double-click to load. Prev/Next then step through this folder.");
  t[Key::libraryTone] = U("TONE3000 tone: double-click to try it in the chain \xc2\xb7 drag onto a slot or block.");
  t[Key::libraryCapture] = U("Capture: double-click to try it \xc2\xb7 type a number to jump in its folder \xc2\xb7 drag onto a block.");
  t[Key::libraryAddBlock] = U("Add to Library: save this tone in a Library folder.");
  t[Key::libraryPickFolder] = U("Choose a folder: click Add here on one of your folders.");
  t[Key::libraryAddHere] = U("Add here: put the tone in this folder.");
  t[Key::libraryCancelAdd] = U("Cancel: don\xe2\x80\x99t add the tone.");
  t[Key::libraryNewFolder] = U("New Folder: make a folder here.");
  t[Key::librarySaveRig] = U("Save Here: save the current chain as a preset in this folder.");
  t[Key::libraryRename] = U("Rename: change the name. The file is renamed too.");
  t[Key::libraryDelete] = U("Delete: move it to the trash.");
  t[Key::libraryCopyToMine] = U("Copy to Mine: make your own copy that you can change.");
  t[Key::libraryExport] = U("Export Backup: save everything to a .t3klibrary file. Import it to restore.");
  t[Key::libraryShare] = U("Export for Sharing: TONE3000 captures go as links. Local-only models are not exported.");
  t[Key::libraryImport] = U("Import File: add a .t3klibrary file. A newer copy of a library replaces the older one.");
  t[Key::libraryKeepHere] = U("Keep Here: make this the folder KEEP saves to.");
  t[Key::libraryMissing] = U("Files the Library uses were moved or deleted outside the plugin.");
  t[Key::libraryChooseBlock] = U("More than one block plays this: choose the one to replace, or add a new block.");
  t[Key::libraryShowMissing] = U("Show the missing files: find them, download them again, or forget them.");
  t[Key::libraryFindMissing] = U("Find: choose the folder the files are in now.");
  t[Key::libraryDownloadMissing] = U("Download Again: get this capture from TONE3000 again, in the same place.");
  t[Key::libraryHideMissing] = U("Hide: until more files go missing. The list stays in the ... menu.");
  t[Key::libraryForgetMissing] = U("Forget: stop looking for files you deleted on purpose.");
  t[Key::libraryStopKeeping] = U("Stop: KEEP asks for a folder again. What you kept stays.");
  t[Key::libraryKeeping] = U("Keeping here: KEEP on a block card saves the model it plays into this folder.");
  t[Key::libraryKeep] = U("Keep: save the model this block plays into your Library.");
  t[Key::libraryOriginal] = U("Source: load the capture this one was kept from.");
  t[Key::libraryKept] = U("Kept: load the copy you kept of this capture.");
  t[Key::libraryRefreshBlock] = U("Refresh: add the captures new in this block's folder.");
  t[Key::libraryGoOriginal] = U("Go to Source: show the capture this one was kept from.");
  t[Key::libraryGoKept] = U("Go to Kept: show the copy you kept of this capture.");
  t[Key::libraryOwnFolder] = U("Put in Own Folder: move this into a new folder named after it.");
  t[Key::libraryShowBlock] = U("Show in Library: open the Library on the file this block plays.");
  t[Key::libraryOpenSite] = U("Open on TONE3000: open this tone's page in your browser.");
  t[Key::libraryKeepMore] = U("More ways to keep this tone: one capture, a reference, or all its captures.");
  t[Key::libraryKeepCapture] = U("Keep Capture: save the capture this block plays as a file.");
  t[Key::libraryKeepTone] = U("Keep as Reference: save a small file that plays this tone from TONE3000.");
  t[Key::libraryDownloadTone] = U("Download All Captures: save every capture of this tone into a new folder.");
  t[Key::libraryLoadOriginal] = U("Load Source: load the TONE3000 tone this capture came from.");
  t[Key::libraryLoadFolder] = U("Load as Block: one block with all this folder's captures; switch them in its picker.");
  t[Key::libraryImportFolder] = U("Import Folder: copy a folder of captures into the Library.");
  t[Key::libraryChooseRoot] = U("Set Folder: choose where the Library is stored.");
  t[Key::libraryReveal] = U("Reveal: show it in your file browser.");
  t[Key::libraryRefresh] = U("Refresh: read the Library folder from disk again.");
  t[Key::libraryUse] = U("Load: put it in the chain.");
  t[Key::libraryLink] = U("Link Folder: show a folder from elsewhere in the Library. Nothing is copied.");
  t[Key::libraryUnlink] = U("Unlink: remove this folder from the Library. Its files are not touched.");
  t[Key::libraryFavorites] = U("Favorites: your TONE3000 favorites. Drop a tone here to favorite it.");
  t[Key::libraryFavorite] = U("Favorite: double-click to try it \xc2\xb7 drag into a folder to keep a copy \xc2\xb7 right-click: more.");
  t[Key::libraryUnfavorite] = U("Unfavorite: remove it from your TONE3000 favorites.");
  t[Key::libraryAudition] = U("Load: try it in the chain, in place of the block of the same kind.");
  t[Key::libraryAddNew] = U("Add as Block: add it as a new block at the end of the chain.");
  t[Key::libraryCapturesRoot] = U("Captures: the captures you made yourself.");
  t[Key::libraryPresetsRoot] = U("Presets: your rigs. Folders here are setlists: Prev/Next steps through them.");
  t[Key::libraryLinked] = U("Linked folder: files that stay where they are. Changes here change those files.");

  // Tone browser
  t[Key::browserSearch] = U("Search: find tones on TONE3000 by title, gear, tags or creator.");
  t[Key::browserSearchProfile] = U("Search: find tones in this list by title.");
  t[Key::browserMoreFilters] = U("Filters: sort, and narrow by format, tags, makes, creators or calibration.");
  t[Key::browserFewerFilters] = U("Fewer filters: fold these away. They stay applied.");
  t[Key::browserVerified] = U("Verified: only tones from verified creators.");
  t[Key::browserProfile] = U("Profile: your recently used, favorited or created tones.");
  t[Key::browserGear] = U("Gear: only tones of this type. Click again: all types.");
  t[Key::browserSort] = U("Sort: order the results.");
  t[Key::browserFormat] = U("Format: only NAM captures or impulse responses.");
  t[Key::browserTags] = U("Tags: only tones carrying any of the chosen tags.");
  t[Key::browserMakes] = U("Makes: only tones of the chosen makes and models.");
  t[Key::browserCreators] = U("Creators: only tones by the chosen creators.");
  t[Key::browserCalibrated] = U("Calibrated: only tones with a calibrated model.");
  t[Key::browserClearFilter] = U("Click the name: change this filter · ×: clear it.");
  t[Key::browserProfileLocked] =
      U("Unavailable while a profile filter is set: your own tones list by title and gear only. Clear the profile to use it.");
  t[Key::browserCalibratedIr] = U("Not for impulse responses: cabinets and spaces carry no calibration data.");
  t[Key::browserBackToTrending] = U("Back: to the trending tones.");

  // Sign-in screen
  t[Key::signInBack] = U("Back: stop signing in and return.");
  t[Key::signInCopyLink] = U("Copy Link: copy the sign-in link to paste into any browser on this machine.");
  t[Key::signInPhone] =
      U("Sign in on a different device: scan a QR code or type a code on your phone, no browser needed here.");
  t[Key::signInNewCode] = U("New code: get a fresh code for your phone.");
  t[Key::signInRetry] = U("Try again: start the sign-in over.");
  t[Key::signInDismiss] = U("Dismiss: drop the error and return.");

  // Chain gallery
  t[Key::addTile] =
      U("Add Tone: browse TONE3000 for this slot, or drop a .nam or IR .wav file (or a folder of them). Right-click: paste / load file · drag: move.");
  t[Key::closeToneBrowser] = U("Close: back to the chain.");
  t[Key::copyBlock] = U("Copy: copy this block (tone, model and all settings).");
  t[Key::pasteBlock] = U("Paste: add a copy of the copied block in this slot.");
  t[Key::loadFileTile] =
      U("Load File: pick a local .nam or IR .wav file to load here. No account needed.");
  t[Key::loadFolderTile] =
      U("Load Folder: pick a folder of .nam or .wav files; loads as one multi-model block.");
  t[Key::blockPower] = U("Power: bypass this block.");
  t[Key::retryLoad] = U("Retry: re-download this model.");
  t[Key::swapTone] = U("Swap: replace this tone, keeping its slot.");
  t[Key::removeBlock] = U("Remove: delete this block.");
  t[Key::addBefore] = U("Add Before: browse TONE3000 for a block in front of this one (or drop on its left edge).");
  t[Key::addAfter] = U("Add After: browse TONE3000 for a block right after this one (or drop on its right edge).");
  t[Key::panLeft] = knobDesktop("Pan L", "Left chain, hard left ↔ center.");
  t[Key::panRight] = knobDesktop("Pan R", "Right chain, center ↔ hard right.");
  t[Key::panLink] = U("Link Pans: mirror both pan knobs.");
  t[Key::monoSum] =
      U("Mono output: both chains summed to one channel. Bal/S/Ø still shape the blend; pans are off.");
  t[Key::panMonoSum] =
      U("Pan: unavailable, the output is mono. The chains are summed instead (see MONO).");
  t[Key::soloLeft] = U("Solo L: hear the Left chain alone.");
  t[Key::soloRight] = U("Solo R: hear the Right chain alone.");
  t[Key::invertLeft] =
      U("Invert L: flip the Left chain polarity. Fixes chains that hollow out or cancel.");
  t[Key::invertRight] =
      U("Invert R: flip the Right chain polarity. Fixes chains that hollow out or cancel.");
  t[Key::swapChains] = U("Swap Chains: exchange Left/Right chains.");
  t[Key::branchGap] = U("Branch: feed the other chain from this point in this chain.");
  t[Key::branchJunction] = U("Branch Point: the other chain starts here. Click: make chains independent.");

  // Block card
  t[Key::blockIn] = knobDesktop("In", "block input gain, ±24 dB.");
  t[Key::blockOut] = knobDesktop("Out", "block output gain, ±24 dB.");
  t[Key::blockOutIr] = knobDesktop("Out", "block output gain, ±24 dB (IR pre-trimmed -18 dB).");
  t[Key::blockMix] = knobDesktop("Mix", "dry/wet blend.");
  t[Key::blockNormalize] = U("Normalize: level this block’s loudness. Off: raw capture level.");
  t[Key::blockNormalizeOverridden] =
      U("Normalize: overridden — calibration hands this model’s true output level to the next NAM block.");
  t[Key::blockSize] = U("NAM Size: LITE saves CPU · FULL is highest quality. Sets this block only.");
  t[Key::blockSizeChip] =
      U("NAM Size: this block’s size differs from your default. To choose per block, enable it in Settings.");
  t[Key::blockCalibrated] =
      U("Calibration: active — levels set from this model’s calibration data.");
  t[Key::blockUncalibrated] = U("Calibration: inactive — this model has no calibration data.");
  t[Key::eqToggle] = U("EQ: 6-band EQ editor. Outline: EQ shaping the sound.");
  t[Key::toneInfo] = U("Info: tone description, makes, and tags from TONE3000.");
  t[Key::toneInfoLogin] = U("Log In: sign in to TONE3000 to see tone details.");
  t[Key::viewOnT3k] = U("View on TONE3000: open this tone in your browser.");
  t[Key::favoriteTone] = U("Favorite: save this tone to your TONE3000 favorites.");
  t[Key::unfavoriteTone] = U("Favorited: click to remove from your TONE3000 favorites.");
  t[Key::eqSlidersView] = U("Sliders: gain-only fader view.");
  t[Key::eqCurveView] = U("Curve: parametric freq/gain/Q editor.");
  t[Key::eqReset] = U("Reset EQ: all bands flat, position post.");
  t[Key::eqPre] = U("PRE: EQ before the model. Off: after the model (wet only).");
  t[Key::eqPower] = U("EQ Power: bypass EQ, keep settings.");
  t[Key::shareTone] = U("Share: copy TONE3000 link.");
  t[Key::modelSelectSignedOut] = U("Models: sign in to TONE3000 to switch models.");
  t[Key::backToChain] = U("Back: chain overview.");

  // EQ editor
  t[Key::eqFader] = kTouch ? U("Band Fader: gain, ±15 dB. drag: adjust · double tap: reset.")
                           : U("Band Fader: gain, ±15 dB. ") + shift("drag") +
                                 U(": fine · double-click / ") + alt("click") + ": reset.";
  t[Key::eqFaderPass] = U("Pass Band: no gain. Shape it in Curve view.");
  t[Key::eqDot] = kTouch ? U("Band Dot: drag: freq + gain · double tap: reset. Q: use the Q chip.")
                         : U("Band Dot: drag: freq + gain · scroll: Q · ") + shift("drag") +
                               U(": fine · ") + alt("click") + ": reset.";
  t[Key::eqFreqChip] = U("Freq: click to type (“800”, “1.2k”). Enter: commit · Esc: cancel.");
  t[Key::eqGainChip] = U("Gain: click to type, ±15 dB. Enter: commit · Esc: cancel.");
  t[Key::eqQChip] = kTouch ? U("Q: tap to type. Enter: commit · Esc: cancel.")
                           : "Q: scroll the graph (" + shift("scroll") + ": fine) or click to type.";

  // Meters
  t[Key::clipDot] = U("Clip: latches on clipping. Click: clear.");

  // The hint bar itself
  t[Key::cpuLoad] = U("CPU: audio engine load.");
  t[Key::hideHints] = U("Hide Info Bar: hide this bar. Re-enable in Settings.");

  for (auto& [key, value] : t) value = touchify(value);
  return t;
}

const std::map<Key, String>& table() {
  static const auto t = buildTable();
  return t;
}

}  // namespace

const String& text(Key key) {
  auto& t = table();
  auto it = t.find(key);
  jassert(it != t.end());
  static const String empty;
  return it == t.end() ? empty : it->second;
}

String toneTile(const String& title) {
  return kTouch ? title + U(". Tap: open · drag: reorder · touch and hold: menu.")
                : title + U(". Click: open · drag: reorder · ") + alt("drag") +
                      ": duplicate · right-click: copy / load file / add before or after · drop on an edge: add there.";
}

String bandType(const String& label) { return label + ": band curve shape."; }

String lead(const String& hint) {
  const int colon = hint.indexOf(": ");
  return (colon > 0 ? hint.substring(0, colon) : hint).trim();
}

void announce(const String& text) {
  if (text.isNotEmpty())
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

}  // namespace t3k::ui::help
