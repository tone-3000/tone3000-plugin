#include "MockBackend.h"

#include <cmath>

namespace t3k::ui::testbed {

namespace {

juce::var obj(std::initializer_list<std::pair<const char*, juce::var>> props) {
  auto* o = new juce::DynamicObject();
  for (const auto& [k, v] : props)
    o->setProperty(k, v);
  return juce::var(o);
}

juce::var defaultChain() {
  return obj({{"revision", 1},
              {"canUndo", false},
              {"canRedo", false},
              {"stereoEnabled", false},
              {"activeSide", "left"},
              {"stereoInput", false},
              {"standalone", false},
              {"inputMode", "stereo"},
              {"namFullSize", false},
              {"multiCore", true},
              {"sampleRate", 48000},
              {"chain", juce::Array<juce::var>()}});
}

// The relay list from juce-mock.js plus the editor's remaining relays, all
// normalized 0..1 like the bridge shipped them.
struct SliderDefault {
  const char* id;
  float value;
};
constexpr SliderDefault kSliders[] = {
    {"inputLevel", 0.5f},        {"outputLevel", 0.5f},   {"outputBalance", 0.5f},
    {"spreadOffset", 0.62f},     {"spreadWobble", 0.25f}, {"spreadCrossover", 0.5f},
    {"alignOffset", 0.5f},       {"alignWobble", 0.25f},  {"alignCrossover", 0.5f},
    {"chainPanLeft", 0.0f},      {"chainPanRight", 1.0f},
    {"toneBass", 0.5f},          {"toneMid", 0.55f},      {"toneTreble", 0.45f},
    {"gateThreshold", 0.35f},    {"inputCalibrationLevel", 0.5f},
    // The gate deck's real-unit defaults (50 ms / 20 ms / 80 dB) on their
    // normalised maps (KnobScale.h).
    {"gateRelease", 0.5f},       {"gateHold", 0.1f},      {"gateRange", 1.0f},
    // Pitch shift at 0 st (centre) with its deck at the defaults: tonality
    // Off (top), the 30 ms buffer (second of four detents), Mix fully shifted.
    {"pitchSemitones", 0.5f},    {"pitchTonality", 1.0f},  {"pitchWindow", 1.0f / 3.0f},
    {"pitchMix", 1.0f},
};
struct ToggleDefault {
  const char* id;
  bool value;
};
constexpr ToggleDefault kToggles[] = {
    {"spreadEnabled", false},          {"spreadWobbleEnabled", true},
    {"spreadCrossoverEnabled", true},  {"spreadDiffuseEnabled", true},
    {"alignEnabled", false},           {"alignWobbleEnabled", false},
    {"alignCrossoverEnabled", false},  {"alignDiffuseEnabled", false},
    {"chainPanLinked", true},
    {"chainSoloLeft", false},          {"chainSoloRight", false},
    {"chainInvertLeft", false},        {"chainInvertRight", false},
    {"gateEnabled", true},             {"toneEqEnabled", true},
    {"pitchEnabled", false},           {"pitchStep", true},
    {"calibrateInput", false},         {"osEnabled", false},
};

}  // namespace

struct MockBackend::Params : juce::AudioProcessor {
  explicit Params(const juce::var& scenario) {
    const auto& sliders = scenario["sliders"];
    for (const auto& s : kSliders) {
      auto* p = new juce::AudioParameterFloat(juce::ParameterID{s.id, 1}, s.id,
                                              juce::NormalisableRange<float>(0.0f, 1.0f), s.value);
      if (sliders.hasProperty(s.id))
        *p = static_cast<float>(static_cast<double>(sliders[s.id]));
      addParameter(p);
    }
    const auto& toggles = scenario["toggles"];
    for (const auto& t : kToggles) {
      auto* p = new juce::AudioParameterBool(juce::ParameterID{t.id, 1}, t.id, t.value);
      if (toggles.hasProperty(t.id))
        *p = static_cast<bool>(toggles[t.id]);
      addParameter(p);
    }
    auto* os = new juce::AudioParameterChoice(juce::ParameterID{"osFactor", 1}, "osFactor",
                                              juce::StringArray{"2x", "4x", "8x"}, 0);
    if (scenario["comboBoxes"].hasProperty("osFactor"))
      *os = static_cast<int>(scenario["comboBoxes"]["osFactor"]);
    addParameter(os);
  }

  juce::RangedAudioParameter* find(const juce::String& id) {
    for (auto* p : getParameters())
      if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
        if (ranged->getParameterID() == id)
          return ranged;
    return nullptr;
  }

  // Inert AudioProcessor boilerplate.
  const juce::String getName() const override { return "MockParams"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
  double getTailLengthSeconds() const override { return 0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  juce::AudioProcessorEditor* createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock&) override {}
  void setStateInformation(const void*, int) override {}
};

MockBackend::MockBackend(const juce::var& scenario)
    : params_(std::make_unique<Params>(scenario)),
      chain_(scenario["chain"].isObject() ? scenario["chain"] : defaultChain()),
      device_(scenario["device"]),
      midiMap_(scenario["midiMap"].isObject()
                   ? scenario["midiMap"]
                   : obj({{"channel", 0}, {"learnTargetId", ""}, {"mappings", juce::Array<juce::var>()}})),
      presets_(scenario["presets"].isArray() ? scenario["presets"] : juce::var(juce::Array<juce::var>())),
      meters_(scenario["meters"]),
      tuner_(scenario["tuner"].isObject() ? scenario["tuner"]
                                          : obj({{"frequency", 0}, {"confidence", 0}, {"level", -60}})),
      autoMeasure_(obj({{"state", "listening"}})),
      version_(scenario["version"].isVoid() ? "1.4.2" : scenario["version"].toString()) {
  if (!chain_["revision"].isInt() && !chain_["revision"].isDouble())
    chain_.getDynamicObject()->setProperty("revision", 1);
}

MockBackend::~MockBackend() = default;

juce::RangedAudioParameter* MockBackend::parameter(const juce::String& id) {
  return params_->find(id);
}

juce::var MockBackend::okResult() { return obj({{"ok", true}, {"error", ""}}); }

void MockBackend::bumpChain() {
  auto* o = chain_.getDynamicObject();
  o->setProperty("revision", static_cast<int>(o->getProperty("revision")) + 1);
}

void MockBackend::notifyMidiMapChanged() {
  listeners.call([](Listener& l) { l.midiMapChanged(); });
}

void MockBackend::setChain(juce::var chain) {
  chain_ = std::move(chain);
  bumpChain();
}

void MockBackend::setDevice(juce::var device) {
  device_ = std::move(device);
  listeners.call([](Listener& l) { l.audioDeviceChanged(); });
}

juce::var MockBackend::getChainState(int knownRevision) {
  if (knownRevision == static_cast<int>(chain_["revision"]))
    return obj({{"revision", chain_["revision"]}, {"unchanged", true}});
  return chain_;
}

juce::uint32 MockBackend::chainRevision() {
  return static_cast<juce::uint32>(static_cast<int>(chain_["revision"]));
}

juce::var MockBackend::loadLocalTonePath(const juce::File&, const std::string&) {
  return obj({{"blockId", "blk-local"}});
}

juce::var MockBackend::loadLocalToneUrls(const juce::Array<juce::URL>&, const std::string&) {
  return obj({{"blockId", "blk-local"}});
}

void MockBackend::setInputMode(const juce::String& mode) {
  auto* chain = chain_.getDynamicObject();
  chain->setProperty("inputMode", mode);
  // Mirrors the processor's dualMonoEngaged(): the mode runs only on a mono
  // chain with a stereo source and a stereo rig (stereoOutput defaults on).
  chain->setProperty("dualMonoActive",
                     mode == "dual" && !static_cast<bool>(chain_["stereoEnabled"]) &&
                         static_cast<bool>(chain_["stereoInput"]) &&
                         static_cast<bool>(chain_.getProperty("stereoOutput", true)));
  bumpChain();
}

void MockBackend::setNamSlimSizeDefault(double slimSize) {
  chain_.getDynamicObject()->setProperty("namSlimSizeDefault", slimSize);
  bumpChain();
}

// Written through like the processor does it, so the store's synchronous
// refresh after the call replays into the open block card.
bool MockBackend::setBlockSlimSize(const std::string& blockId, double slimSize) {
  for (const auto* lane : {"chain", "chainRight"}) {
    if (auto* blocks = chain_[lane].getArray()) {
      for (auto& block : *blocks) {
        auto* params = block["params"].getDynamicObject();
        if (block["blockId"].toString().toStdString() != blockId || params == nullptr) continue;
        params->setProperty("slimSize", slimSize);
        bumpChain();
        return true;
      }
    }
  }
  return false;
}

bool MockBackend::moveBlockToChain(const std::string& blockId, const juce::String& side, int index) {
  chainMoves_.push_back({blockId, side, index});
  auto* from = chain_[side == "left" ? "chainRight" : "chain"].getArray();
  auto* to = chain_[side == "left" ? "chain" : "chainRight"].getArray();
  if (from == nullptr || to == nullptr) return false;
  for (int i = 0; i < from->size(); ++i) {
    if ((*from)[i]["blockId"].toString().toStdString() != blockId) continue;
    const auto block = (*from)[i];
    from->remove(i);
    to->insert(juce::jlimit(0, to->size(), index), block);
    bumpChain();
    return true;
  }
  return false;
}

void MockBackend::setMultiCore(bool enabled) {
  chain_.getDynamicObject()->setProperty("multiCore", enabled);
  bumpChain();
}

juce::var MockBackend::getBlockSpectrum(const std::string& blockId) {
  if (signal_ != nullptr) return signal_->spectrum(blockId);
  // The JS mock's static guitar-ish spectrum: low-mid hump falling off.
  juce::Array<juce::var> bins;
  for (int i = 0; i < 64; ++i) {
    const double x = i / 63.0;
    const double hump = -18 - 55 * std::pow(x - 0.28, 2) * 4;
    const double wiggle = 4 * std::sin(i * 1.7) * std::exp(-x * 1.5);
    bins.add(juce::jlimit(-100.0, -8.0, hump + wiggle));
  }
  return bins;
}

juce::var MockBackend::getPresetList() { return obj({{"presets", presets_}}); }

juce::var MockBackend::savePreset(const juce::String& name) {
  const juce::String id = "user-" + juce::String(juce::Time::currentTimeMillis());
  auto preset = obj({{"id", id}, {"name", name}, {"factory", false}});
  presets_.append(preset);
  chain_.getDynamicObject()->setProperty("preset", obj({{"id", id}, {"name", name}}));
  bumpChain();
  return obj({{"id", id}, {"name", name}});
}

bool MockBackend::loadPreset(const juce::String& presetId) {
  for (const auto& p : *presets_.getArray()) {
    if (p["id"].toString() == presetId) {
      chain_.getDynamicObject()->setProperty("preset", obj({{"id", p["id"]}, {"name", p["name"]}}));
      bumpChain();
      break;
    }
  }
  return true;
}

bool MockBackend::movePreset(const juce::String& presetId, int delta) {
  presetMoves_.push_back({presetId, delta});
  auto* all = presets_.getArray();
  if (all == nullptr || delta == 0) return false;
  // Same rule as PresetManager::move: shift by `delta` within the preset's
  // own section, clamped to that section's ends, then rewrite the whole
  // list user-first / factory-second the way list() returns it.
  int index = -1;
  for (int i = 0; i < all->size(); ++i)
    if ((*all)[i]["id"].toString() == presetId) index = i;
  if (index < 0) return false;
  const bool factory = (*all)[index]["factory"];
  juce::Array<juce::var> section, others;
  for (const auto& p : *all) (static_cast<bool>(p["factory"]) == factory ? section : others).add(p);
  int from = -1;
  for (int i = 0; i < section.size(); ++i)
    if (section[i]["id"].toString() == presetId) from = i;
  const int to = juce::jlimit(0, section.size() - 1, from + delta);
  if (to == from) return false;
  section.move(from, to);
  juce::Array<juce::var> ordered;
  ordered.addArray(factory ? others : section);
  ordered.addArray(factory ? section : others);
  presets_ = ordered;
  return true;
}

juce::var MockBackend::getAudioInputLevels() {
  if (signal_ != nullptr) {
    const auto* channels = device_["inputChannels"].getArray();
    return signal_->inputLevels(channels != nullptr ? channels->size() : 0);
  }
  juce::Array<juce::var> levels;
  if (const auto* channels = device_["inputChannels"].getArray())
    for (int i = 0; i < channels->size(); ++i)
      levels.add(-24);
  return levels;
}

void MockBackend::setMidiChannelFilter(int channel) {
  midiMap_.getDynamicObject()->setProperty("channel", channel);
  notifyMidiMapChanged();
}

void MockBackend::startMidiLearn(const juce::String& targetId) {
  midiMap_.getDynamicObject()->setProperty("learnTargetId", targetId);
  notifyMidiMapChanged();
}

void MockBackend::cancelMidiLearn() {
  midiMap_.getDynamicObject()->setProperty("learnTargetId", "");
  notifyMidiMapChanged();
}

bool MockBackend::removeMidiMapping(const juce::String& targetId) {
  juce::Array<juce::var> kept;
  for (const auto& m : *midiMap_["mappings"].getArray())
    if (m["targetId"].toString() != targetId)
      kept.add(m);
  midiMap_.getDynamicObject()->setProperty("mappings", kept);
  notifyMidiMapChanged();
  return true;
}

// As MidiMapper::setCcMapping: replace any mapping for the target, and a
// typed number supersedes a learn armed for the same target.
bool MockBackend::setMidiCcMapping(const juce::String& targetId, int cc) {
  if (cc < 0 || cc > 127) return false;
  juce::Array<juce::var> kept;
  for (const auto& m : *midiMap_["mappings"].getArray())
    if (m["targetId"].toString() != targetId)
      kept.add(m);
  auto* mapping = new juce::DynamicObject();
  mapping->setProperty("targetId", targetId);
  mapping->setProperty("source", "cc");
  mapping->setProperty("number", cc);
  kept.add(juce::var(mapping));
  auto* map = midiMap_.getDynamicObject();
  map->setProperty("mappings", kept);
  if (map->getProperty("learnTargetId").toString() == targetId) map->setProperty("learnTargetId", "");
  notifyMidiMapChanged();
  return true;
}

juce::var MockBackend::getMeterLevels() {
  if (signal_ != nullptr)
    return signal_->meters(chain_);
  if (meters_.isObject())
    return meters_;
  auto* blocks = new juce::DynamicObject();
  for (const char* lane : {"chain", "chainRight"})
    if (const auto* items = chain_[lane].getArray())
      for (const auto& item : *items)
        if (item["kind"].toString() == "tone")
          blocks->setProperty(item["blockId"].toString(), obj({{"in", -22}, {"out", -16}}));
  return obj({{"input", juce::Array<juce::var>{-18, -21}},
              {"output", juce::Array<juce::var>{-12, -14}},
              {"blocks", juce::var(blocks)},
              {"cpu", 0.11},
              {"correlation", 0.85}});
}

}  // namespace t3k::ui::testbed
