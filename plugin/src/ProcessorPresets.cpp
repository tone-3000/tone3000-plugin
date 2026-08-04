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
  static const std::vector<juce::String> ids = {
      "inputLevel",     "outputLevel",         "outputBalance",
      "toneBass",       "toneMid",             "toneTreble",
      "gateThreshold",  "gateEnabled",         "toneEqEnabled",
      "spreadEnabled",  "spreadOffset",        "spreadWobble",
      "stereoOffsetEnabled", "stereoOffsetTime",
      "chainPanLeft",   "chainPanRight",       "chainPanLinked",
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

  // Faceplate parameters. Gestured so hosts treat this like a user edit
  // (automation write modes record it instead of fighting it).
  const juce::ValueTree params = preset.getChildWithName("Params");
  for (int i = 0; i < params.getNumChildren(); ++i) {
    const juce::ValueTree paramTree = params.getChild(i);
    if (auto* p = parameters.getParameter(paramTree.getProperty("id").toString())) {
      const auto denormalized =
          static_cast<float>(static_cast<double>(paramTree.getProperty("value")));
      p->beginChangeGesture();
      p->setValueNotifyingHost(p->convertTo0to1(denormalized));
      p->endChangeGesture();
    }
  }

  juce::Logger::writeToLog("[Presets] Loaded preset: " + activePresetName);

  // The restore queued every block's engine build on the background loader;
  // hold the mute until they land (bounded), else the chain fades back in on
  // unloaded pass-through blocks and blasts the raw dry input.
  editFade.releaseWhenChainLoadsSettle();
  return true;
}

bool TONE3000Processor::renamePreset(const juce::String& presetId, const juce::String& newName) {
  if (!presetManager.rename(presetId, newName))
    return false;
  juce::ScopedLock lock(chainMutex);
  if (activePresetId == presetId) {
    activePresetName = newName.trim();
    bumpChainRevision();
  }
  return true;
}

bool TONE3000Processor::deletePreset(const juce::String& presetId) {
  if (!presetManager.remove(presetId))
    return false;
  juce::ScopedLock lock(chainMutex);
  if (activePresetId == presetId) {
    activePresetId.clear();
    activePresetName.clear();
    bumpChainRevision();
  }
  return true;
}

bool TONE3000Processor::movePreset(const juce::String& presetId, int delta) {
  // Pure list-order change: nothing about the loaded chain moves, so no
  // revision bump; the UI re-pulls the preset list after the call.
  return presetManager.move(presetId, delta);
}
