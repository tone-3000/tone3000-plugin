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
      "gateThreshold",  "gateEnabled",      "toneEqEnabled",
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
    obj->setProperty("category", info.category);
    obj->setProperty("favorite", info.favorite);
    presetArray.add(juce::var(obj.get()));
  }
  juce::Array<juce::var> categoryArray;
  for (const auto& cat : presetManager.listCategories()) {
    categoryArray.add(cat);
  }
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("presets", presetArray);
  root->setProperty("categories", categoryArray);
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
  // No deferred fade release: an empty chain queues no model loads.
  return true;
}

bool TONE3000Processor::addPresetCategory(const juce::String& name) {
  return presetManager.addCategory(name);
}

bool TONE3000Processor::deletePresetCategory(const juce::String& name) {
  return presetManager.deleteCategory(name);
}

bool TONE3000Processor::setPresetCategory(const juce::String& id, const juce::String& category) {
  return presetManager.setPresetCategory(id, category);
}

bool TONE3000Processor::movePresetsToCategory(const juce::StringArray& ids, const juce::String& category) {
  return presetManager.movePresetsToCategory(ids, category);
}

bool TONE3000Processor::setPresetFavorite(const juce::String& id, bool isFavorite) {
  return presetManager.setPresetFavorite(id, isFavorite);
}

bool TONE3000Processor::setPresetsFavorite(const juce::StringArray& ids, bool isFavorite) {
  return presetManager.setPresetsFavorite(ids, isFavorite);
}

juce::var TONE3000Processor::duplicatePresets(const juce::StringArray& ids) {
  const auto created = presetManager.duplicatePresets(ids);
  juce::Array<juce::var> result;
  for (const auto& info : created) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", info.id);
    obj->setProperty("name", info.name);
    obj->setProperty("category", info.category);
    obj->setProperty("favorite", info.favorite);
    obj->setProperty("factory", info.factory);
    result.add(juce::var(obj.get()));
  }
  return result;
}

bool TONE3000Processor::deletePresets(const juce::StringArray& ids) {
  bool activeDeleted = false;
  {
    juce::ScopedLock lock(chainMutex);
    for (const auto& id : ids) {
      if (activePresetId == id) {
        activePresetId.clear();
        activePresetName.clear();
        activeDeleted = true;
        break;
      }
    }
  }
  if (activeDeleted)
    bumpChainRevision();

  return presetManager.removePresets(ids);
}
