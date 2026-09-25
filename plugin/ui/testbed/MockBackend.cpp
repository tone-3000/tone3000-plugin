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
    // Transpose at 0 st (centre) with its deck at the defaults: 0 cents,
    // tonality Off (top), the 60 ms window (middle detent).
    {"transposeSemitones", 0.5f}, {"transposeFine", 0.5f}, {"transposeTonality", 1.0f},
    {"transposeWindow", 0.5f},
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
    {"transposeEnabled", false},
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
  chain_.getDynamicObject()->setProperty("inputMode", mode);
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
