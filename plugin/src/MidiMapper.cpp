#include "MidiMapper.h"

MidiMapper::MidiMapper(juce::AudioProcessorValueTreeState& params) : parameters(params) {}

MidiMapper::~MidiMapper() {
  cancelPendingUpdate();
}

int MidiMapper::blockPowerIndexForTarget(const juce::String& targetId) {
  if (!targetId.startsWith("block") || !targetId.endsWith("Power"))
    return -1;
  const auto middle = targetId.substring(5, targetId.length() - 5);
  if (middle.isEmpty() || !middle.containsOnly("0123456789"))
    return -1;
  const int position = middle.getIntValue();  // 1-based in the id
  return position >= 1 && position <= 64 ? position - 1 : -1;
}

//==============================================================================
// Audio thread

void MidiMapper::processMidi(const juce::MidiBuffer& midi) {
  if (midi.isEmpty())
    return;

  const int channel = channelFilter.load(std::memory_order_relaxed);

  if (learnArmed.load(std::memory_order_relaxed)) {
    // Learn mode: capture the first eligible event and drive nothing — the
    // control being wiggled may already be mapped elsewhere, and jerking that
    // target mid-learn would be hostile.
    for (const auto metadata : midi) {
      const auto msg = metadata.getMessage();
      if (channel != 0 && msg.getChannel() != channel)
        continue;
      if (!msg.isController() && !msg.isNoteOn())
        continue;
      if (!learnArmed.exchange(false))
        break;  // another callback captured first
      capturedSource.store(static_cast<int>(msg.isController() ? Source::cc : Source::note));
      capturedNumber.store(msg.isController() ? msg.getControllerNumber() : msg.getNoteNumber());
      triggerAsyncUpdate();
      break;
    }
    return;
  }

  const juce::SpinLock::ScopedTryLockType tryLock(mapLock);
  if (!tryLock.isLocked())
    return;  // rare contended edit: drop one buffer of MIDI, never block audio

  for (const auto metadata : midi) {
    const auto msg = metadata.getMessage();
    if (channel != 0 && msg.getChannel() != channel)
      continue;

    // Program changes switch presets — always live, no mapping needed.
    if (msg.isProgramChange()) {
      pendingProgram.store(msg.getProgramChangeNumber());
      triggerAsyncUpdate();
      continue;
    }

    if (!msg.isController() && !msg.isNoteOn())
      continue;
    const auto source = msg.isController() ? Source::cc : Source::note;
    const int number = msg.isController() ? msg.getControllerNumber() : msg.getNoteNumber();
    for (auto& mapping : mappings)
      if (mapping.source == source && mapping.number == number)
        applyEvent(mapping, msg);
  }
}

void MidiMapper::applyEvent(const Mapping& mapping, const juce::MidiMessage& msg) {
  if (mapping.toggle) {
    // Note-ons always fire; CCs fire on press only (value ≥ 64), so momentary
    // switches sending 127-then-0 flip once per stomp instead of twice.
    if (msg.isController() && msg.getControllerValue() < 64)
      return;
    switch (mapping.kind) {
      case Kind::blockPower:
        // Block enable is a locked, undoable chain edit — hop to the message
        // thread. XOR keeps rapid double-stomps parity-correct if they land
        // before the async update runs.
        pendingBlockToggles.fetch_xor(juce::uint64(1) << mapping.blockIndex);
        triggerAsyncUpdate();
        return;
      case Kind::stereoMode:
        // Same deferral as block powers; a flip counter's parity is the XOR
        // equivalent for a single toggle.
        pendingStereoToggles.fetch_add(1);
        triggerAsyncUpdate();
        return;
      case Kind::parameter: {
        auto& param = *mapping.param;
        param.setValueNotifyingHost(param.getValue() < 0.5f ? 1.0f : 0.0f);
        return;
      }
    }
    return;
  }

  mapping.param->setValueNotifyingHost(static_cast<float>(msg.getControllerValue()) / 127.0f);
}

//==============================================================================
// Message thread

juce::var MidiMapper::getState() const {
  auto* obj = new juce::DynamicObject();
  juce::var state(obj);

  obj->setProperty("channel", channelFilter.load());
  obj->setProperty("learnTargetId", learnArmed.load() ? learnTargetId : juce::String());

  juce::Array<juce::var> list;
  {
    const juce::SpinLock::ScopedLockType lock(mapLock);
    for (const auto& mapping : mappings) {
      auto* m = new juce::DynamicObject();
      m->setProperty("targetId", mapping.targetId);
      m->setProperty("source", sourceToString(mapping.source));
      m->setProperty("number", mapping.number);
      list.add(juce::var(m));
    }
  }
  obj->setProperty("mappings", list);
  return state;
}

void MidiMapper::setChannelFilter(int channel) {
  channelFilter.store(juce::jlimit(0, 16, channel));
  notifyChanged();
}

void MidiMapper::startLearn(const juce::String& targetId) {
  if (!isValidTarget(targetId))
    return;
  learnArmed.store(false);  // quiesce the audio thread while re-targeting
  capturedNumber.store(-1);
  learnTargetId = targetId;
  learnArmed.store(true);
  notifyChanged();
}

void MidiMapper::cancelLearn() {
  learnArmed.store(false);
  capturedNumber.store(-1);
  learnTargetId.clear();
  notifyChanged();
}

bool MidiMapper::removeMapping(const juce::String& targetId) {
  bool removed = false;
  {
    const juce::SpinLock::ScopedLockType lock(mapLock);
    const auto before = mappings.size();
    std::erase_if(mappings, [&](const Mapping& m) { return m.targetId == targetId; });
    removed = mappings.size() != before;
  }
  if (removed)
    notifyChanged();
  return removed;
}

MidiMapper::Mapping MidiMapper::makeMapping(const juce::String& targetId, Source source,
                                            int number) const {
  const int blockIndex = blockPowerIndexForTarget(targetId);
  const Kind kind = targetId == kStereoTarget ? Kind::stereoMode
                    : blockIndex >= 0         ? Kind::blockPower
                                              : Kind::parameter;
  auto* param = kind == Kind::parameter ? parameters.getParameter(targetId) : nullptr;
  jassert(kind != Kind::parameter || param != nullptr);  // callers validate the id first
  const bool toggle = kind != Kind::parameter || source == Source::note ||
                      (param != nullptr && param->isBoolean());
  return {targetId, kind, param, blockIndex, source, number, toggle};
}

void MidiMapper::notifyChanged() {
  mapDirty.store(true);
  triggerAsyncUpdate();
}

void MidiMapper::handleAsyncUpdate() {
  // 1. Commit a pending learn capture.
  const int number = capturedNumber.exchange(-1);
  if (number >= 0 && learnTargetId.isNotEmpty()) {
    const auto source = static_cast<Source>(capturedSource.load());
    if (isValidTarget(learnTargetId)) {
      auto mapping = makeMapping(learnTargetId, source, number);
      const juce::SpinLock::ScopedLockType lock(mapLock);
      std::erase_if(mappings, [&](const Mapping& m) { return m.targetId == learnTargetId; });
      mappings.push_back(std::move(mapping));
    }
    learnTargetId.clear();
    mapDirty.store(true);
  }

  // 2. Deliver deferred performance events (presets, block powers).
  const int program = pendingProgram.exchange(-1);
  if (program >= 0 && onProgramChange)
    onProgramChange(program);

  auto toggles = pendingBlockToggles.exchange(0);
  for (int index = 0; toggles != 0; ++index, toggles >>= 1)
    if ((toggles & 1) != 0 && onBlockPowerToggle)
      onBlockPowerToggle(index);

  if (pendingStereoToggles.exchange(0) % 2 != 0 && onStereoToggle)
    onStereoToggle();

  // 3. Tell the UI, but only when the map/learn state actually moved —
  //    performance events alone shouldn't cause settings re-pulls.
  if (mapDirty.exchange(false) && onChanged)
    onChanged();
}

//==============================================================================
// Plugin state

juce::ValueTree MidiMapper::toValueTree() const {
  juce::ValueTree tree("MidiMappings");
  tree.setProperty("channel", channelFilter.load(), nullptr);

  const juce::SpinLock::ScopedLockType lock(mapLock);
  for (const auto& mapping : mappings) {
    juce::ValueTree child("Mapping");
    child.setProperty("targetId", mapping.targetId, nullptr);
    child.setProperty("source", sourceToString(mapping.source), nullptr);
    child.setProperty("number", mapping.number, nullptr);
    tree.appendChild(child, nullptr);
  }
  return tree;
}

void MidiMapper::restoreFromValueTree(const juce::ValueTree& tree) {
  std::vector<Mapping> restored;
  int channel = 0;

  if (tree.hasType("MidiMappings")) {
    channel = juce::jlimit(0, 16, static_cast<int>(tree.getProperty("channel", 0)));
    for (const auto& child : tree) {
      if (!child.hasType("Mapping"))
        continue;
      const auto targetId = child.getProperty("targetId").toString();
      if (!isValidTarget(targetId))
        continue;  // unknown/renamed target in an old project: drop it
      restored.push_back(makeMapping(targetId,
                                     sourceFromString(child.getProperty("source").toString()),
                                     static_cast<int>(child.getProperty("number", 0))));
    }
  }

  channelFilter.store(channel);
  {
    const juce::SpinLock::ScopedLockType lock(mapLock);
    mappings = std::move(restored);
  }
  notifyChanged();
}
