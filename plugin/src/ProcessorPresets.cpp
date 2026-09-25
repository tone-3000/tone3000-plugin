#include "Processor.h"

// #############################
// INTERNAL PRESETS
// #############################
//
// A preset is a self-contained tone: the full chain snapshot (same tree the
// undo system uses, plus embedded model bytes so it loads offline) and the
// faceplate parameter values. Loading goes through the same reconciling
// restore as undo/redo (engines are reused where possible, everything else
// loads in the background seeded from the embedded bytes) and records one
// undo step, so a preset load is itself undoable.

const std::vector<juce::String>& TONE3000Processor::presetParameterIds() {
  // chainSolo* stays out on purpose: solo is monitoring state, not tone,
  // and a preset saved mid-audition must not load with a chain muted.
  static const std::vector<juce::String> ids = {
      "inputLevel",     "outputLevel",      "outputBalance",
      "toneBass",       "toneMid",          "toneTreble",
      "gateThreshold",  "gateEnabled",      "gateRelease",
      "gateHold",       "gateRange",        "toneEqEnabled",
      "spreadEnabled",  "spreadOffset",     "spreadWobble",
      "spreadWobbleEnabled", "spreadCrossover", "spreadCrossoverEnabled",
      "spreadDiffuseEnabled",
      "alignEnabled",   "alignOffset",      "alignWobble",
      "alignWobbleEnabled", "alignCrossover", "alignCrossoverEnabled",
      "alignDiffuseEnabled",
      "chainPanLeft",   "chainPanRight",    "chainPanLinked",
      "chainInvertLeft", "chainInvertRight",
  };
  return ids;
}

void TONE3000Processor::setActivePreset(const juce::String& id, const juce::String& name) {
  juce::ScopedLock lock(chainMutex);
  activePresetId = id;
  activePresetName = name;
  bumpChainRevision();
}

juce::var TONE3000Processor::getPresetList() const {
  juce::Array<juce::var> presetArray;
  for (const auto& info : presetManager.list()) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", info.id);
    obj->setProperty("name", info.name);
    obj->setProperty("factory", info.factory);
    presetArray.add(juce::var(obj.get()));
  }
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("presets", presetArray);
  return root.get();
}

juce::var TONE3000Processor::savePreset(const juce::String& rawName) {
  const juce::String name = rawName.trim();
  if (name.isEmpty())
    return {};

  juce::ValueTree preset(PresetManager::kPresetTag);
  preset.setProperty("schemaVersion", 1, nullptr);

  {
    juce::ScopedLock lock(chainMutex);
    preset.appendChild(captureChainSnapshot(true), nullptr);
  }

  // Values are stored denormalized (real dB/ratio units), so preset files
  // stay meaningful if a parameter's range is ever retuned.
  juce::ValueTree params("Params");
  for (const auto& paramId : presetParameterIds()) {
    if (auto* p = parameters.getParameter(paramId)) {
      juce::ValueTree paramTree("Param");
      paramTree.setProperty("id", paramId, nullptr);
      paramTree.setProperty("value", static_cast<double>(p->convertFrom0to1(p->getValue())),
                            nullptr);
      params.appendChild(paramTree, nullptr);
    }
  }
  preset.appendChild(params, nullptr);

  const PresetManager::Info info = presetManager.save(name, preset);
  if (info.id.isEmpty())
    return {};

  setActivePreset(info.id, info.name);
  // The list gained/renamed an entry and the active program index may have
  // moved with it: refresh host program names and displays.
  hostProgramInfoCache.clear();
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  juce::Logger::writeToLog("[Presets] Saved preset: " + info.name);

  juce::DynamicObject::Ptr obj = new juce::DynamicObject();
  obj->setProperty("id", info.id);
  obj->setProperty("name", info.name);
  return obj.get();
}

bool TONE3000Processor::loadPresetAtIndex(int index) {
  const auto presets = presetManager.list();
  if (index < 0 || index >= static_cast<int>(presets.size()))
    return false;  // controller sent a program beyond the list: ignore
  return loadPreset(presets[static_cast<size_t>(index)].id);
}

bool TONE3000Processor::stepPreset(int delta) {
  const auto presets = presetManager.list();
  const int count = static_cast<int>(presets.size());
  if (count == 0 || delta == 0)
    return false;

  juce::String currentId;
  {
    juce::ScopedLock lock(chainMutex);
    currentId = activePresetId;
  }
  int index = -1;
  for (int i = 0; i < count; ++i) {
    if (presets[static_cast<size_t>(i)].id == currentId) {
      index = i;
      break;
    }
  }

  // Mirrors the preset bar's ‹ › buttons: wrap at the ends; with nothing
  // active (or a deleted preset), next starts at the first and previous at
  // the last.
  const int next = index < 0 ? (delta > 0 ? 0 : count - 1)
                             : ((index + delta) % count + count) % count;
  return loadPreset(presets[static_cast<size_t>(next)].id);
}

bool TONE3000Processor::loadPreset(const juce::String& presetId) {
  const juce::ValueTree preset = presetManager.load(presetId);
  const juce::ValueTree snapshot = preset.getChildWithName("ChainSnapshot");
  if (!snapshot.isValid())
    return false;

  // A preset replaces the whole chain (and jumps the faceplate parameters
  // below), so mute-splice the transition like any structural edit. The fade
  // holds until everything is in place, then glides back in on the new rig.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock; see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    pushChainHistory();
    retired = restoreChainSnapshot(snapshot);  // bumps the revision
    activePresetId = presetId;
    activePresetName = preset.getProperty("name").toString();
  }
  retired.clear();

  // Faceplate parameters: every preset-managed id is set, entries missing
  // from the file (saved before a parameter existed) land on the parameter
  // default, so a preset always restores the same rig. Gestured so hosts
  // treat this like a user edit (automation write modes record it instead
  // of fighting it).
  const juce::ValueTree params = preset.getChildWithName("Params");
  for (const auto& paramId : presetParameterIds()) {
    auto* p = parameters.getParameter(paramId);
    if (p == nullptr)
      continue;
    const juce::ValueTree paramTree = params.getChildWithProperty("id", paramId);
    const float norm =
        paramTree.isValid()
            ? p->convertTo0to1(
                  static_cast<float>(static_cast<double>(paramTree.getProperty("value"))))
            : p->getDefaultValue();
    p->beginChangeGesture();
    p->setValueNotifyingHost(norm);
    p->endChangeGesture();
  }

  juce::Logger::writeToLog("[Presets] Loaded preset: " + activePresetName);

  // Tell hosts the current program moved so their program parameter/menus
  // follow (the VST3 wrapper syncs its program parameter off this; its echo
  // is suppressed by applyHostProgram's already-active guard).
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));

  // The restore queued every block's engine build on the background loader;
  // hold the mute until they land (bounded), else the chain fades back in on
  // unloaded pass-through blocks and blasts the raw dry input.
  editFade.releaseWhenChainLoadsSettle();
  return true;
}

bool TONE3000Processor::renamePreset(const juce::String& presetId, const juce::String& newName) {
  if (!presetManager.rename(presetId, newName))
    return false;
  hostProgramInfoCache.clear();  // host program names follow the rename
  {
    juce::ScopedLock lock(chainMutex);
    if (activePresetId == presetId) {
      activePresetName = newName.trim();
      bumpChainRevision();
    }
  }
  // A rename can reorder the list (name order within a section), moving the
  // active preset's program index.
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  return true;
}

bool TONE3000Processor::deletePreset(const juce::String& presetId) {
  if (!presetManager.remove(presetId))
    return false;
  hostProgramInfoCache.clear();
  {
    juce::ScopedLock lock(chainMutex);
    if (activePresetId == presetId) {
      activePresetId.clear();
      activePresetName.clear();
      bumpChainRevision();
    }
  }
  // Every preset after the deleted one shifted down a program slot (and the
  // active one may be gone entirely), so host program state moved.
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  return true;
}

bool TONE3000Processor::movePreset(const juce::String& presetId, int delta) {
  // Pure list-order change: nothing about the loaded chain moves, so no
  // revision bump; the UI re-pulls the preset list after the call. Program
  // numbers follow the list order though, so host displays must refresh.
  if (!presetManager.move(presetId, delta))
    return false;
  hostProgramInfoCache.clear();
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  return true;
}

bool TONE3000Processor::isChainAtDefault() const {
  if (activePresetId.isNotEmpty() || stereoEnabled.load())
    return false;
  for (const auto& l : lanes)
    for (const auto& b : l)
      if (b->type != ChainBlockType::INSERT)
        return false;
  // Normalized-space tolerance: UI resets and preset loads land exactly on
  // the default, but a knob dragged back or host automation can be a hair
  // off. 1e-4 of a ±24 dB range is 0.005 dB, far below any UI step.
  for (const auto& paramId : presetParameterIds())
    if (auto* p = parameters.getParameter(paramId))
      if (std::abs(p->getValue() - p->getDefaultValue()) > 1.0e-4f)
        return false;
  return true;
}

bool TONE3000Processor::resetToDefault() {
  {
    juce::ScopedLock lock(chainMutex);
    if (isChainAtDefault())
      return false;  // nothing to reset; spare the audio the edit fade
  }

  // The whole chain is replaced, so mute-splice the swap like a preset load.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock; see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);
    pushChainHistory();
    // A bare snapshot *is* the default state: the restore retires every
    // block (lanes pad back to fresh insert slots), turns stereo off and
    // clears the branch, in one undoable step.
    retired = restoreChainSnapshot(juce::ValueTree("ChainSnapshot"));
    activePresetId.clear();
    activePresetName.clear();
  }
  retired.clear();

  // Gestured like loadPreset, so hosts treat the jumps as user edits.
  for (const auto& paramId : presetParameterIds()) {
    if (auto* p = parameters.getParameter(paramId)) {
      p->beginChangeGesture();
      p->setValueNotifyingHost(p->getDefaultValue());
      p->endChangeGesture();
    }
  }

  juce::Logger::writeToLog("[Presets] Reset to default");
  // The active preset is gone, so the host program index fell back (see
  // getCurrentProgram); keep host program displays in step.
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
  // No deferred fade release: an empty chain queues no model loads.
  return true;
}

// #############################
// HOST PROGRAM API
// #############################
//
// The internal preset list, exposed as the JUCE/host program list. This is
// what makes MIDI program changes work in VST3 hosts (Cubase, Ableton, VST
// Live; GitHub issue #38): VST3 never delivers PC as MIDI. JUCE's wrapper
// reconstructs CCs/notes/pitch bend/aftertouch from IMidiMapping emulation
// parameters, but a program change can only arrive as a change of the
// wrapper's program parameter, which exists only when getNumPrograms() > 1.
// Hosts map incoming PC n onto that parameter and the wrapper lands it in
// setCurrentProgram(n) on the message thread. Raw-MIDI formats (Standalone,
// AU, LV2, CLAP) keep delivering PC through MidiMapper::processMidi →
// loadPresetAtIndex; both routes load the preset at index n in list order,
// so a preset's PC number (shown in the preset browser) means the same tone
// everywhere.
//
// Known wrapper edge, accepted: on an instance that never had a preset
// active, getCurrentProgram() necessarily reports 0, and JUCE's VST3
// wrapper drops program changes that match the current program, so the
// very first PC targeting program 0 is swallowed until any other program
// (or any preset via UI/MIDI) has been selected. Every JUCE plugin with
// programs shares this; the alternative (reserving slot 0) would shift the
// advertised "PC n = nth preset" numbering.

int TONE3000Processor::getNumPrograms() {
  return kNumHostPrograms;
}

int TONE3000Processor::getCurrentProgram() {
  juce::String id;
  {
    juce::ScopedLock lock(chainMutex);
    id = activePresetId;
  }
  if (id.isEmpty())
    return 0;  // no active preset; the API still needs a valid index
  // Fresh list, not the name cache: guards compare against this (see
  // applyHostProgram and the wrapper's own echo suppression), so it must
  // track the store exactly. One scan per call, event-rate only.
  const auto presets = presetManager.list();
  const int count = juce::jmin(static_cast<int>(presets.size()), kNumHostPrograms);
  for (int i = 0; i < count; ++i)
    if (presets[static_cast<size_t>(i)].id == id)
      return i;
  return 0;  // active preset deleted elsewhere or beyond the program range
}

void TONE3000Processor::setCurrentProgram(int index) {
  if (index < 0 || index >= kNumHostPrograms)
    return;
  // Preset loads are heavyweight message-thread work (chain lock, undo
  // history, background model builds). VST3 lands here on the message
  // thread, but the AudioProcessor contract doesn't promise one, so defer
  // exactly like MidiMapper defers its PC deliveries.
  if (juce::MessageManager::existsAndIsCurrentThread()) {
    applyHostProgram(index);
  } else {
    pendingHostProgram.store(index);
    triggerAsyncUpdate();
  }
}

void TONE3000Processor::applyHostProgram(int index) {
  const auto presets = presetManager.list();
  if (index < 0 || index >= static_cast<int>(presets.size()))
    return;  // empty program slot: ignore, like an out-of-range PC
  {
    juce::ScopedLock lock(chainMutex);
    if (activePresetId == presets[static_cast<size_t>(index)].id)
      return;  // already active: host echo / re-select, don't reload
  }
  loadPreset(presets[static_cast<size_t>(index)].id);
}

const juce::String TONE3000Processor::getProgramName(int index) {
  // Mutations clear the snapshot; the 500 ms expiry re-reads names saved by
  // another instance sharing the preset folder.
  const auto now = juce::Time::getMillisecondCounter();
  if (hostProgramInfoCache.empty() || now - hostProgramInfoCacheTime > 500) {
    hostProgramInfoCache = presetManager.list();
    hostProgramInfoCacheTime = now;
  }
  if (index >= 0 && index < static_cast<int>(hostProgramInfoCache.size()))
    return hostProgramInfoCache[static_cast<size_t>(index)].name;
  return "(empty)";
}

void TONE3000Processor::changeProgramName(int, const juce::String&) {
  // Preset renames go through renamePreset (the preset browser); host
  // program-name edits are not supported.
}
