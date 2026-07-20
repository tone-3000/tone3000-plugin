#include "Processor.h"

// #############################
// INTERNAL PRESETS
// #############################
//
// A preset is a self-contained tone: the full chain snapshot (same tree the
// undo system uses, plus embedded model bytes so it loads offline) and the
// faceplate parameter values. Loading goes through the same reconciling
// restore as undo/redo — engines are reused where possible, everything else
// loads in the background seeded from the embedded bytes — and records one
// undo step, so a preset load is itself undoable.

const std::vector<juce::String>& TONE3000Processor::presetParameterIds() {
  static const std::vector<juce::String> ids = {
      "inputLevel",    "inputBalance", "outputLevel",   "outputBalance",
      "toneBass",      "toneMid",      "toneTreble",
      "gateThreshold", "gateEnabled",  "toneEqEnabled", "toneEqPre",
      "spreadEnabled", "spreadAmount",  "spreadJitter",
      "chainPanLeft",  "chainPanRight", "chainPanLinked",
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
  preset.setProperty("version", 1, nullptr);

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

bool TONE3000Processor::loadPreset(const juce::String& presetId) {
  const juce::ValueTree preset = presetManager.load(presetId);
  const juce::ValueTree snapshot = preset.getChildWithName("ChainSnapshot");
  if (!snapshot.isValid())
    return false;

  // A preset replaces the whole chain (and jumps the faceplate parameters
  // below) — mute-splice the transition like any structural edit. The fade
  // holds until everything is in place, then glides back in on the new rig.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock — see restoreChainSnapshot
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
