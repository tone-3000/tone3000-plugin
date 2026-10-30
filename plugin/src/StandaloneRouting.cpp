#include "StandaloneRouting.h"
#include "StandaloneIoLease.h"
#include "RoutingMixer.h"
#include "RoutingState.h"
#include "Processor.h"
#include <cmath>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace t3k::routing {
namespace {
juce::StandalonePluginHolder* holder() {
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
  return juce::StandalonePluginHolder::getInstance();
#else
  return nullptr;
#endif
}
int packedIndex(const juce::BigInteger& mask, int ch) {
  if (ch < 0 || !mask[ch]) return -1;
  int n = 0;
  for (int i = 0; i < ch; ++i) if (mask[i]) ++n;
  return n;
}
juce::File settingsFile() {
  return TONE3000Processor::getSettingsFile().getParentDirectory().getChildFile("standalone-routing.json");
}
juce::String renderGraph(TONE3000Processor& p) {
  const auto state = p.getChainState(-1);
  auto* graph = new juce::DynamicObject;
  graph->setProperty("stereo", state["stereoEnabled"]); graph->setProperty("branch", state["branch"]);
  for (const char* key : {"chain", "chainRight"}) {
    juce::Array<juce::var> rows;
    if (const auto* blocks = state[key].getArray()) for (const auto& b : *blocks) {
      if (b["kind"].toString() == "insert") continue;
      auto* row = new juce::DynamicObject;
      row->setProperty("id", b["blockId"]); row->setProperty("tone", b["tone"]["id"]);
      row->setProperty("model", b["activeModelId"]); row->setProperty("slim", b["params"]["slimSize"]);
      row->setProperty("loaded", b["loaded"]); row->setProperty("loading", b["modelLoading"]);
      rows.add(juce::var(row));
    }
    graph->setProperty(key, rows);
  }
  return juce::JSON::toString(juce::var(graph), true);
}
StereoSignal tap(TONE3000Processor& p, Stage stage, int frames) {
  const auto view = p.routingTap(stage);
  StereoSignal result;
  if (view.frames == frames)
    for (size_t ch = 0; ch < 2; ++ch)
      if (view.available[ch]) result.channel[ch] = view.channels[ch];
  return result;
}
bool modelsReady(TONE3000Processor& p) {
  const auto state = p.getChainState(-1);
  for (const auto* key : {"chain", "chainRight"})
    if (const auto* list = state[key].getArray())
      for (const auto& item : *list)
        if (item["kind"].toString() != "insert" &&
            (static_cast<bool>(item["modelLoading"]) || !static_cast<bool>(item["loaded"]))) return false;
  return !p.isChainEditFadeHeld();
}
}

class StandaloneRouting::Impl final : private juce::AudioIODeviceCallback, private juce::Timer {
public:
  explicit Impl(TONE3000Processor& p) : processor(p) {
    if (auto* h = holder()) manager = &h->deviceManager;
    Config stored;
    const auto file = settingsFile();
    if (file.existsAsFile() && fromVar(juce::JSON::parse(file), stored).wasOk()) config = stored;
    config.enabled = false; // Explicit opt-in every launch; never route onto a replacement device.
    owner = std::make_shared<standalone_io::RoutingOwner>();
    owner->suspend = [this] { pausedForCapture = true; suspend(false); };
    owner->resume = [this] {
      pausedForCapture = false;
      if (!config.enabled) return juce::Result::ok();
      return start(config);
    };
    if (manager) {
      standalone_io::registerRoutingOwner(owner);
      startTimer(100);
    }
  }
  ~Impl() override {
    stopTimer(); config.enabled = false; pausedForCapture = false; suspend(true);
    standalone_io::unregisterRoutingOwner(owner); owner.reset();
  }
  juce::String inputDevice() const { return manager ? manager->getAudioDeviceSetup().inputDeviceName : juce::String(); }
  juce::String outputDevice() const { return manager ? manager->getAudioDeviceSetup().outputDeviceName : juce::String(); }
  juce::StringArray inputNames() const {
    if (auto* d = manager ? manager->getCurrentAudioDevice() : nullptr) return d->getInputChannelNames();
    return {};
  }
  juce::StringArray outputNames() const {
    if (auto* d = manager ? manager->getCurrentAudioDevice() : nullptr) return d->getOutputChannelNames();
    return {};
  }
  std::vector<StageChoice> choices() const {
    std::vector<StageChoice> result;
    const auto state = processor.getChainState(-1);
    for (int lane = 0; lane < 2; ++lane) {
      if (const auto* list = state[lane == 0 ? "chain" : "chainRight"].getArray())
        for (const auto& item : *list) {
          if (item["kind"].toString() == "insert") continue;
          const auto tone = item["tone"];
          const auto gear = tone["gear"].toString();
          const bool cab = gear == "cab" ||
              (tone["format"].toString() == "ir" && gear != "space" &&
               !static_cast<bool>(item["irLong"]) && static_cast<bool>(item["loaded"]));
          result.push_back({item["blockId"].toString().toStdString(),
            juce::String(lane == 0 ? "L: " : "R: ") + tone["title"].toString(), lane, cab});
        }
    }
    return result;
  }
  juce::Result apply(const Config& next) {
    if (standalone_io::captureOwnsDevice() && !next.enabled) {
      config.enabled = false; message = "Routing disabled; raw capture still owns the interface.";
      save(); return juce::Result::ok();
    }
    if (standalone_io::captureOwnsDevice()) return juce::Result::fail("Finish the raw capture before changing routing.");
    if (!next.enabled) {
      config = next; pausedForCapture = false; suspend(true);
      if (restorationError.isEmpty()) message = "Routing stopped. Assign interface channels and Apply to start.";
      save(); return juce::Result::ok();
    }
    auto errors = validate(next, inputNames().size(), outputNames().size());
    if (!errors.isEmpty()) return juce::Result::fail(errors.joinIntoString("\n"));
    if ((!next.inputDevice.isEmpty() && next.inputDevice != inputDevice()) ||
        (!next.outputDevice.isEmpty() && next.outputDevice != outputDevice()))
      return juce::Result::fail("The selected interface differs from the saved route. Refresh and bind the plan to the current interface first.");
    suspend(true);
    config = next; config.inputDevice = inputDevice(); config.outputDevice = outputDevice();
    auto result = start(config);
    if (result.failed()) { config.enabled = false; message = result.getErrorMessage(); }
    save();
    return result;
  }
  void save() {
    const auto file = settingsFile();
    if (file.getParentDirectory().createDirectory().failed() || !file.replaceWithText(juce::JSON::toString(toVar(config))))
      warnings.addIfNotAlreadyThere("Routing works for this session, but its preferences could not be saved.");
  }
  void buildMirror() {
    mirrorReady.store(false); mirror.reset();
    pendingMirror = false;
    bool needed = false;
    for (const auto& o : config.outputs) needed |= o.source != Source::off && o.cabinetOff;
    if (!needed) return;
    if (!modelsReady(processor)) {
      pendingMirror = true;
      warnings.add("Monitor Cab Off is waiting for the current chain models to finish loading.");
      return;
    }
    const auto list = choices();
    const int lanes = processor.isStereoMode() ? 2 : 1;
    for (int lane = 0; lane < lanes; ++lane) {
      const auto& id = config.cabinet[static_cast<size_t>(lane)];
      const auto found = std::find_if(list.begin(), list.end(), [&](const auto& b) { return b.id == id && b.cabinetCandidate; });
      if (id.empty() || found == list.end()) {
        warnings.add("Monitor Cab Off is silent: select a distinct cabinet block for each rendered lane. A cabinet baked into an amp NAM cannot be removed.");
        return;
      }
    }
    juce::MemoryBlock bytes; processor.getStateInformation(bytes);
    // Keeping the bypassed block retains branch anchors and downstream FX.
    juce::MemoryBlock shadowBytes;
    const auto result = cabinetBypassState(bytes, config.cabinet, shadowBytes);
    if (result.failed()) { warnings.add("Monitor Cab Off is silent: " + result.getErrorMessage()); return; }
    mirror = std::make_unique<TONE3000Processor>();
    mirror->setRoutingIoOverride(true, config.stereoInputRight >= 0);
    mirror->setPlayConfigDetails(2, 2, rate, maximumFrames);
    mirror->configureRoutingTaps({true, config.stackEnd, config.modEnd, config.wetStart});
    mirror->prepareToPlay(rate, maximumFrames);
    mirror->setStateInformation(shadowBytes.getData(), static_cast<int>(shadowBytes.getSize()));
    mirror->followRoutingControlsFrom(&processor, config.cabinet);
    warnings.add("Cabinet-free renderer is loading; Monitor Cab Off remains silent until it is ready.");
  }
  juce::Result start(const Config& next) {
    auto* h = holder(); auto* d = manager ? manager->getCurrentAudioDevice() : nullptr;
    if (!h || !d) return juce::Result::fail("Select an audio interface in System Settings first.");
    if (next.inputDevice != inputDevice() || next.outputDevice != outputDevice())
      return juce::Result::fail("The routing interface disconnected or changed. Audio routing remains stopped.");
    warnings.clear(); fault.store(false); processing = true;
    savedSetup = manager->getAudioDeviceSetup(); savedMode = processor.getInputMode();
    auto setup = savedSetup;
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear(); setup.outputChannels.clear();
    auto add = [&](int ch) { if (ch >= 0) setup.inputChannels.setBit(ch); };
    add(next.inputChannels[static_cast<size_t>(next.inputSource)]); add(next.stereoInputRight); add(next.diInput);
    for (int ch : next.returnInputs) add(ch);
    for (int ch : next.auxInputs) add(ch);
    for (const auto& o : next.outputs) if (o.source != Source::off) {
      setup.outputChannels.setBit(o.left);
      if ((stereoSource(o.source) && o.stereo) || (o.source >= Source::diStack && o.source <= Source::diMasterMono)) setup.outputChannels.setBit(o.right);
    }
    h->player.setProcessor(nullptr); suspendedPlayer = true;
    const auto error = manager->setAudioDeviceSetup(setup, false);
    d = manager->getCurrentAudioDevice();
    if (error.isNotEmpty() || !d) { suspend(true); return juce::Result::fail("Routing device could not open: " + error); }
    inMask = d->getActiveInputChannels(); outMask = d->getActiveOutputChannels();
    if ((inMask & setup.inputChannels) != setup.inputChannels || (outMask & setup.outputChannels) != setup.outputChannels) {
      suspend(true); return juce::Result::fail("The driver did not enable every selected channel. No partial route was started.");
    }
    inPacked = packedIndex(inMask, next.inputChannels[static_cast<size_t>(next.inputSource)]);
    rightPacked = packedIndex(inMask, next.stereoInputRight); diPacked = packedIndex(inMask, next.diInput);
    for (size_t ch = 0; ch < 2; ++ch) { returnPacked[ch] = packedIndex(inMask, next.returnInputs[ch]); auxPacked[ch] = packedIndex(inMask, next.auxInputs[ch]); }
    rate = d->getCurrentSampleRate(); maximumFrames = std::max(1, d->getCurrentBufferSizeSamples());
    if (!std::isfinite(rate) || rate < 32000 || rate > 192000 || maximumFrames > 32768) {
      suspend(true); return juce::Result::fail("Routing requires a 32–192 kHz interface with a buffer no larger than 32768 frames.");
    }
    audio.setSize(2, maximumFrames); shadow.setSize(2, maximumFrames); zero.assign(static_cast<size_t>(maximumFrames), 0);
    midi.ensureSize(8192); collector.reset(rate);
    inputGain = juce::Decibels::decibelsToGain(next.inputGainDb +
      (next.inputSource == InputSource::returnReamp || next.inputSource == InputSource::digitalReamp ? next.reampSensitivityDb : 0));
    std::array<std::array<int, 2>, portCount> packed;
    for (size_t i = 0; i < portCount; ++i) packed[i] = {packedIndex(outMask, next.outputs[i].left), packedIndex(outMask, next.outputs[i].right)};
    mixer.prepare(next, rate, packed);
    processor.setRoutingIoOverride(true, next.stereoInputRight >= 0);
    processor.setInputMode(next.stereoInputRight >= 0 ? TONE3000Processor::InputMode::Stereo : TONE3000Processor::InputMode::Left);
    processor.setPlayConfigDetails(2, 2, rate, maximumFrames);
    processor.configureRoutingTaps({true, next.stackEnd, next.modEnd, next.wetStart});
    processor.prepareToPlay(rate, maximumFrames);
    buildMirror();
    revision = processor.getCurrentChainRevision();
    graph = renderGraph(processor);
    osEnabled = processor.parameters.getRawParameterValue("osEnabled")->load();
    osFactor = processor.parameters.getRawParameterValue("osFactor")->load();
    manager->addMidiInputDeviceCallback({}, &collector);
    attached.store(true); standalone_io::setRoutingActive(true);
    manager->addAudioCallback(this);
    processing = false; message = "Routing active. Hear Yourself in System Settings controls monitoring.";
    diagnoseStages();
    return juce::Result::ok();
  }
  void suspend(bool restorePlayer) {
    if (attached.load()) {
      mixer.fadeOut();
      for (int n = 0; n < 30 && !mixer.faded() && !fault.load(); ++n) juce::Thread::sleep(1);
      attached.store(false); manager->removeAudioCallback(this);
      manager->removeMidiInputDeviceCallback({}, &collector);
    }
    standalone_io::setRoutingActive(false);
    mirrorReady.store(false); mirror.reset();
    if (suspendedPlayer) {
      restorationError.clear();
      processor.configureRoutingTaps({}); processor.setRoutingIoOverride(false, false);
      processor.setInputMode(savedMode);
      const auto error = manager->setAudioDeviceSetup(savedSetup, false);
      if (error.isNotEmpty()) { if (auto* h = holder()) h->muteInput.store(true); restorationError = error; message = "Routing stopped; the previous interface could not be restored: " + error; }
      if (restorePlayer) if (auto* h = holder()) h->player.setProcessor(h->processor.get());
      suspendedPlayer = false;
    }
    processing = false;
  }
  void diagnoseStages() {
    const auto state = processor.getChainState(-1);
    auto check = [&](const std::array<std::string, 2>& ids, const char* label) {
      const int lanes = processor.isStereoMode() ? 2 : 1;
      for (int i = 0; i < lanes; ++i) {
        const auto& id = ids[static_cast<size_t>(i)];
        const auto path = effectivePath(state, i);
        if (id.empty() || std::find(path.begin(), path.end(), id) == path.end()) {
          warnings.addIfNotAlreadyThere(juce::String(label) + " needs a reachable block on every rendered lane; missing or unreachable taps are silent."); return;
        }
      }
    };
    for (const auto& o : config.outputs) {
      if (o.source == Source::stack || o.source == Source::diStack) check(config.stackEnd, "Stack");
      if ((o.source >= Source::modLeft && o.source <= Source::modStereo) || o.source == Source::diModLeft || o.source == Source::diModMono) check(config.modEnd, "Mod");
      if (o.source == Source::wetFx) check(config.wetStart, "Wet FX start");
    }
  }
  void rebuildRenderGroup(bool replaceMirror) {
    processing = true; mirrorReady.store(false);
    mixer.fadeOut();
    for (int n = 0; n < 30 && !mixer.faded() && !fault.load(); ++n) juce::Thread::sleep(1);
    attached.store(false); manager->removeAudioCallback(this);
    if (replaceMirror) mirror.reset();
    processor.configureRoutingTaps({true, config.stackEnd, config.modEnd, config.wetStart});
    processor.prepareToPlay(rate, maximumFrames);
    warnings.clear();
    if (replaceMirror) buildMirror();
    else if (mirror) {
      // Models are already loaded. Reset both conversion clocks together,
      // then expose the cabinet-free renderer at the same callback origin.
      mirror->prepareToPlay(rate, maximumFrames);
      mirror->followRoutingControlsFrom(&processor, config.cabinet);
      mirrorReady.store(true);
    }
    std::array<std::array<int, 2>, portCount> packed;
    for (size_t i = 0; i < portCount; ++i)
      packed[i] = {packedIndex(outMask, config.outputs[i].left), packedIndex(outMask, config.outputs[i].right)};
    mixer.prepare(config, rate, packed);
    revision = processor.getCurrentChainRevision(); graph = renderGraph(processor);
    osEnabled = processor.parameters.getRawParameterValue("osEnabled")->load();
    osFactor = processor.parameters.getRawParameterValue("osFactor")->load();
    attached.store(true); manager->addAudioCallback(this);
    processing = false; diagnoseStages();
  }
  void refresh() {
    if (pausedForCapture || standalone_io::captureOwnsDevice()) return;
    if (!attached.load()) return;
    if (fault.load()) { config.enabled = false; suspend(true); message = "Routing stopped because the device clock, channel map, or connection changed. Select the interface and Apply again."; return; }
    if (inputDevice() != config.inputDevice || outputDevice() != config.outputDevice) {
      config.enabled = false; suspend(true); message = "Routing stopped because the selected interface changed."; return;
    }
    const auto current = processor.getCurrentChainRevision();
    const bool osChanged = osEnabled != processor.parameters.getRawParameterValue("osEnabled")->load() ||
                           osFactor != processor.parameters.getRawParameterValue("osFactor")->load();
    const bool mirrorInvalid = mirror && mirror->routingMirrorNeedsRebuild();
    const bool pendingReady = pendingMirror && modelsReady(processor);
    if (current != revision || osChanged || mirrorInvalid || pendingReady) {
      const auto nextGraph = renderGraph(processor);
      // Clipboard/edit-lane/history metadata must never reset DSP or reopen
      // the interface. The normal processor handles its own live edits.
      if (osChanged || pendingReady || (mirror && nextGraph != graph)) {
        mirrorReady.store(false);
        rebuildRenderGroup(true); // Keep the driver open; only renderers restart.
      } else if (mirrorInvalid) {
        mirrorReady.store(false);
        if (modelsReady(processor) && modelsReady(*mirror)) rebuildRenderGroup(false);
      } else {
        revision = current; graph = nextGraph;
        warnings.clear(); diagnoseStages();
      }
    }
    if (mirror && !mirrorReady.load() && modelsReady(processor) && modelsReady(*mirror))
      rebuildRenderGroup(false);
  }
  Config config;
  juce::String message = "Routing stopped. Assign interface channels and Apply to start.";
  juce::StringArray warnings;
  std::atomic<bool> attached{false}, fault{false}, mirrorReady{false};
  std::atomic<float> peak{0};
  bool processing = false, pausedForCapture = false;
  RoutingMixer mixer;
private:
  void timerCallback() override { refresh(); }
  void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
    if (!d || std::abs(d->getCurrentSampleRate() - rate) > 0.1 || d->getCurrentBufferSizeSamples() > maximumFrames ||
        d->getActiveInputChannels() != inMask || d->getActiveOutputChannels() != outMask) fault.store(true);
  }
  void audioDeviceStopped() override { if (attached.load()) fault.store(true); }
  void audioDeviceError(const juce::String&) override { fault.store(true); }
  void audioDeviceIOCallbackWithContext(const float* const* inputs, int inputCount,
    float* const* outputs, int outputCount, int frames, const juce::AudioIODeviceCallbackContext&) override {
    juce::ScopedNoDenormals noDenormals;
    for (int ch = 0; ch < outputCount; ++ch) if (outputs[ch]) juce::FloatVectorOperations::clear(outputs[ch], frames);
    if (fault.load() || !attached.load() || outputCount > 128) return;
    float inputPeak = 0;
    for (int offset = 0; offset < frames; offset += maximumFrames) {
      const int count = std::min(maximumFrames, frames - offset);
      auto input = [&](int ch) -> const float* { return ch >= 0 && ch < inputCount && inputs[ch] ? inputs[ch] + offset : zero.data(); };
      const float* left = input(inPacked); const float* right = rightPacked < 0 ? left : input(rightPacked);
      audio.copyFrom(0, 0, left, count); audio.copyFrom(1, 0, right, count);
      audio.applyGain(0, count, inputGain);
      if (mirror) { shadow.copyFrom(0, 0, audio, 0, 0, count); shadow.copyFrom(1, 0, audio, 1, 0, count); }
      for (int i = 0; i < count; ++i) inputPeak = std::max(inputPeak, std::abs(left[i]));
      juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, count);
      midi.clear(); collector.removeNextBlockOfMessages(midi, count);
      const juce::ScopedTryLock lock(processor.getCallbackLock());
      if (!lock.isLocked() || processor.isSuspended()) continue;
      processor.processBlock(block, midi);
      Signals s;
      s.di = input(diPacked); s.master = {{audio.getReadPointer(0), audio.getReadPointer(1)}};
      s.stack = tap(processor, Stage::stack, count); s.mod = tap(processor, Stage::mod, count); s.wet = tap(processor, Stage::wetFx, count);
      for (size_t ch = 0; ch < 2; ++ch) {
        s.returns.channel[ch] = returnPacked[ch] < 0 ? nullptr : input(returnPacked[ch]);
        s.aux.channel[ch] = auxPacked[ch] < 0 ? nullptr : input(auxPacked[ch]);
      }
      Signals noCab = s; Signals* cabinet = nullptr;
      if (mirror && mirrorReady.load()) {
        juce::AudioBuffer<float> shadowBlock(shadow.getArrayOfWritePointers(), 2, count);
        emptyMidi.clear(); mirror->processBlock(shadowBlock, emptyMidi);
        if (mirrorReady.load() && !processor.isChainEditFadeHeld()) {
          noCab.master = {{shadow.getReadPointer(0), shadow.getReadPointer(1)}};
          noCab.stack = tap(*mirror, Stage::stack, count); noCab.mod = tap(*mirror, Stage::mod, count); noCab.wet = tap(*mirror, Stage::wetFx, count);
          cabinet = &noCab;
        }
      }
      std::array<float*, 128> out{};
      for (int ch = 0; ch < outputCount; ++ch) out[static_cast<size_t>(ch)] = outputs[ch] ? outputs[ch] + offset : nullptr;
      mixer.process(s, cabinet, out.data(), outputCount, count, holder() && !holder()->muteInput.load());
    }
    peak.store(inputPeak);
  }
  TONE3000Processor& processor;
  juce::AudioDeviceManager* manager = nullptr;
  juce::AudioDeviceManager::AudioDeviceSetup savedSetup;
  TONE3000Processor::InputMode savedMode = TONE3000Processor::InputMode::Stereo;
  std::shared_ptr<standalone_io::RoutingOwner> owner;
  std::unique_ptr<TONE3000Processor> mirror;
  juce::BigInteger inMask, outMask;
  juce::AudioBuffer<float> audio, shadow;
  std::vector<float> zero;
  juce::MidiMessageCollector collector;
  juce::MidiBuffer midi, emptyMidi;
  int inPacked = -1, rightPacked = -1, diPacked = -1, maximumFrames = 0;
  std::array<int, 2> returnPacked{-1, -1}, auxPacked{-1, -1};
  double rate = 0;
  float inputGain = 1;
  juce::uint32 revision = 0;
  bool suspendedPlayer = false, pendingMirror = false;
  juce::String restorationError, graph;
  float osEnabled = 0, osFactor = 0;
};

StandaloneRouting::StandaloneRouting(TONE3000Processor& p) : impl_(std::make_unique<Impl>(p)) {}
StandaloneRouting::~StandaloneRouting() = default;
bool StandaloneRouting::isAvailable() { return holder() != nullptr; }
const Config& StandaloneRouting::config() const { return impl_->config; }
bool StandaloneRouting::running() const { return impl_->attached.load(); }
bool StandaloneRouting::busy() const { return standalone_io::captureOwnsDevice() || impl_->processing || impl_->pausedForCapture; }
juce::String StandaloneRouting::status() const {
  return impl_->pausedForCapture && impl_->config.enabled ? "Routing suspended for raw capture; it will resume afterwards." : impl_->message;
}
juce::StringArray StandaloneRouting::diagnostics() const { return impl_->warnings; }
juce::StringArray StandaloneRouting::inputNames() const { return impl_->inputNames(); }
juce::StringArray StandaloneRouting::outputNames() const { return impl_->outputNames(); }
juce::String StandaloneRouting::deviceDescription() const { return "Input: " + currentInputDevice() + " | Output: " + currentOutputDevice(); }
juce::String StandaloneRouting::currentInputDevice() const { return impl_->inputDevice(); }
juce::String StandaloneRouting::currentOutputDevice() const { return impl_->outputDevice(); }
std::vector<StageChoice> StandaloneRouting::stageChoices() const { return impl_->choices(); }
juce::Result StandaloneRouting::apply(const Config& c) { return impl_->apply(c); }
void StandaloneRouting::stop() { Config c = impl_->config; c.enabled = false; impl_->apply(c); }
float StandaloneRouting::outputPeak(Port p) const { return running() ? impl_->mixer.peak(p) : 0; }
float StandaloneRouting::inputPeak() const { return running() ? impl_->peak.load() : 0; }
void StandaloneRouting::refresh() { impl_->refresh(); }
} // namespace t3k::routing
