#include "MidiMapSection.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/MidiCatalog.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/IconButton.h"
#include "widgets/TextField.h"

namespace t3k::ui {

namespace {
constexpr float kListenPulseMs = 1200;
constexpr float kListenDimAlpha = 0.45f;
constexpr int kListenDot = 7, kListenGap = 8;
constexpr int kCcFieldWidth = 54;

const juce::String kEmptyCopy =
    "No mappings yet. Choose a control, then move it on your MIDI device or type its CC number.";
const juce::String kPresetsCaption =
    "Map Previous / Next Preset to step through presets from CC or note buttons. Program change messages also "
    "switch presets directly; each preset shows its PC number in the preset browser.";

int lineHeight(float px) { return Fonts::normalLineHeight(px); }
int monoLineHeight(float px) { return Fonts::normalLineHeight(Fonts::mono(px)); }
}  // namespace

// Row
// One list row: the target cell (name over its context) on the left; derived
// rows add their right-hand controls. Non-first rows draw a hairline on top.
class MidiMapSection::Row : public juce::Component {
public:
  Row(juce::String targetId, juce::String context) : targetId_(std::move(targetId)), context_(std::move(context)) {
    if (const auto* target = midi::targetById(targetId_)) title_ = target->name;
    else title_ = targetId_;
  }

  const juce::String& targetId() const { return targetId_; }
  void setFirst(bool first) { first_ = first; }

  // Two lines: title and subtitle.
  static int targetCellHeight() { return lineHeight(kTitlePx) + kSubtitleGap + lineHeight(kSubtitlePx); }
  // The row's content height (flex align center) before padding.
  virtual int contentHeight() const { return juce::jmax(targetCellHeight(), kIconButton); }
  int height() const { return 2 * kRowPadY + contentHeight() + (first_ ? 0 : 1); }

  void paint(juce::Graphics& g) override {
    if (!first_) {
      g.setColour(theme::kBorder);
      g.fillRect(0, 0, getWidth(), 1);
    }
    const auto cell = targetCell();
    const float top = static_cast<float>(cell.getY());
    paint::cssLine(g, title_, static_cast<float>(cell.getX()), top, static_cast<float>(lineHeight(kTitlePx)),
                   static_cast<float>(cell.getWidth()), Fonts::sans(kTitlePx, true), theme::kWhite);
    paint::cssLine(g, context_, static_cast<float>(cell.getX()), top + lineHeight(kTitlePx) + kSubtitleGap,
                   static_cast<float>(lineHeight(kSubtitlePx)), static_cast<float>(cell.getWidth()),
                   Fonts::sans(kSubtitlePx), theme::kSubtle);
  }

protected:
  // Content box: inside the padding and the top hairline.
  juce::Rectangle<int> contentBox() const {
    return getLocalBounds().withTrimmedTop(first_ ? 0 : 1).reduced(kRowPadX, kRowPadY);
  }
  // Width taken by the right-hand controls (including the gaps before them).
  virtual int trailingWidth() const = 0;
  juce::Rectangle<int> targetCell() const {
    const auto box = contentBox();
    const int h = targetCellHeight();
    return {box.getX(), box.getY() + (box.getHeight() - h) / 2, box.getWidth() - trailingWidth(), h};
  }

private:
  juce::String targetId_, title_, context_;
  bool first_ = true;
};

// MappingRow
// Source + behaviour on the right, then the borderless re-learn / remove
// icon buttons (muted at rest, white on hover).
class MidiMapSection::MappingRow : public Row {
public:
  MappingRow(const MidiMapping& mapping, juce::String context, std::function<void()> onRelearn,
             std::function<void()> onRemove)
      : Row(mapping.targetId, std::move(context)),
        source_(midi::sourceLabel(mapping)),
        behaviour_(midi::behaviorLabel(mapping)),
        relearn_(Icon::RotateCcw, kIconButton, 14),
        remove_(Icon::X, kIconButton, 15) {
    for (auto* b : {&relearn_, &remove_}) {
      b->setActive(false);
      addAndMakeVisible(*b);
    }
    relearn_.setName("Re-learn");
    remove_.setName("Remove");
    relearn_.onClick = std::move(onRelearn);
    remove_.onClick = std::move(onRemove);
  }

  int contentHeight() const override { return juce::jmax(Row::contentHeight(), sourceCellHeight()); }

  void paint(juce::Graphics& g) override {
    Row::paint(g);
    const auto box = contentBox();
    const int w = sourceCellWidth(), h = sourceCellHeight();
    const int x = box.getRight() - iconsWidth() - kRowGap - w;
    const float top = static_cast<float>(box.getY() + (box.getHeight() - h) / 2);
    paint::cssLine(g, source_, static_cast<float>(x), top, static_cast<float>(monoLineHeight(kSourcePx)),
                   static_cast<float>(w), Fonts::mono(kSourcePx), theme::kWhite, juce::Justification::right);
    paint::cssLine(g, behaviour_, static_cast<float>(x), top + monoLineHeight(kSourcePx) + kSubtitleGap,
                   static_cast<float>(lineHeight(kSubtitlePx)), static_cast<float>(w), Fonts::sans(kSubtitlePx),
                   theme::kSubtle, juce::Justification::right);
  }

  void resized() override {
    const auto box = contentBox();
    const int y = box.getY() + (box.getHeight() - kIconButton) / 2;
    remove_.setBounds(box.getRight() - kIconButton, y, kIconButton, kIconButton);
    relearn_.setBounds(remove_.getX() - kIconGap - kIconButton, y, kIconButton, kIconButton);
  }

private:
  static int iconsWidth() { return 2 * kIconButton + kIconGap; }
  int sourceCellWidth() const {
    return juce::roundToInt(juce::jmax(Fonts::width(Fonts::mono(kSourcePx), source_),
                                       Fonts::width(Fonts::sans(kSubtitlePx), behaviour_)));
  }
  static int sourceCellHeight() { return monoLineHeight(kSourcePx) + kSubtitleGap + lineHeight(kSubtitlePx); }
  int trailingWidth() const override { return kRowGap + sourceCellWidth() + kRowGap + iconsWidth(); }

  juce::String source_, behaviour_;
  IconButton relearn_, remove_;
};

// LearningRow
// The armed row: a pulsing "● Listening…", a typed-CC field (Enter commits)
// and Cancel.
class MidiMapSection::LearningRow : public Row, private juce::Timer {
public:
  LearningRow(juce::String targetId, juce::String context, std::function<void(const juce::String&)> onDraft,
              std::function<void()> onCommit, std::function<void()> onCancel)
      : Row(std::move(targetId), std::move(context)),
        cancel_("Cancel", FormButton::text(kSmallPx, true, theme::kWhite, 2, 4, juce::Justification::centred)) {
    cc_.setPlaceholder("CC #");
    cc_.setFont(Fonts::mono(kSmallPx));
    cc_.setCornerRadius(form::kFieldRadius);
    cc_.setBorder(form::kFieldBorder);
    cc_.setPadding(4, 6, 6);
    cc_.setJustification(juce::Justification::centred);
    cc_.setName("CC number");
    cc_.onChange = [this, draft = std::move(onDraft)](const juce::String& text) {
      // Digits only, at most three.
      const auto digits = text.retainCharacters("0123456789").substring(0, 3);
      if (digits != text) cc_.setText(digits);
      draft(digits);
    };
    cc_.onEnter = std::move(onCommit);
    cancel_.onClick = std::move(onCancel);
    cancel_.fitToContent();
    addAndMakeVisible(cc_);
    addAndMakeVisible(cancel_);
    startTimerHz(30);
  }

  int contentHeight() const override { return juce::jmax(Row::contentHeight(), ccFieldHeight()); }

  void paint(juce::Graphics& g) override {
    Row::paint(g);
    const auto box = contentBox();
    const float lineH = static_cast<float>(lineHeight(kSmallPx));
    const int x = cc_.getX() - kRowGap - listeningWidth();
    const float top = static_cast<float>(box.getY()) + (box.getHeight() - lineH) / 2;
    const float alpha = pulseAlpha();
    g.setColour(theme::kBrandYellow.withAlpha(alpha));
    g.fillEllipse(static_cast<float>(x), top + (lineH - kListenDot) / 2, kListenDot, kListenDot);
    paint::cssLine(g, kListening, static_cast<float>(x + kListenDot + kListenGap), top, lineH, 200,
                   Fonts::sans(kSmallPx), theme::kWhite.withAlpha(alpha));
  }

  void resized() override {
    const auto box = contentBox();
    cancel_.setBounds(box.getRight() - cancel_.getWidth(), box.getCentreY() - cancel_.getHeight() / 2,
                      cancel_.getWidth(), cancel_.getHeight());
    const int fieldH = ccFieldHeight();
    cc_.setBounds(cancel_.getX() - kRowGap - kCcFieldWidth, box.getCentreY() - fieldH / 2, kCcFieldWidth, fieldH);
  }

private:
  inline static const juce::String kListening = juce::String::fromUTF8("Listening\xe2\x80\xa6");

  static int ccFieldHeight() { return monoLineHeight(kSmallPx) + 2 * 4 + 2; }
  int listeningWidth() const {
    return kListenDot + kListenGap + juce::roundToInt(Fonts::width(Fonts::sans(kSmallPx), kListening));
  }
  int trailingWidth() const override {
    return kRowGap + listeningWidth() + kRowGap + kCcFieldWidth + kRowGap + cancel_.getWidth();
  }
  // opacity 1 → 0.45 → 1 over 1.2s, ease-in-out.
  float pulseAlpha() const {
    const double t = std::fmod(static_cast<double>(juce::Time::getMillisecondCounter()), kListenPulseMs) /
                     kListenPulseMs;
    const double wave = 0.5 - 0.5 * std::cos(t * juce::MathConstants<double>::twoPi);
    return static_cast<float>(1.0 - (1.0 - kListenDimAlpha) * wave);
  }
  void timerCallback() override { repaint(); }

  TextField cc_;
  FormButton cancel_;
};

// ListCard
// The bordered card of rows (or the italic empty-state note).
class MidiMapSection::ListCard : public FormItem {
public:
  ListCard() : empty_(RichText{}, kSmallPx, theme::kSubtle, juce::Justification::centred, 14.0f / kSmallPx) {
    empty_.setItalicText(kEmptyCopy);
    addChildComponent(empty_);
  }

  void setRows(std::vector<std::unique_ptr<Row>> rows) {
    rows_ = std::move(rows);
    for (size_t i = 0; i < rows_.size(); ++i) {
      rows_[i]->setFirst(i == 0);
      addAndMakeVisible(*rows_[i]);
    }
    empty_.setVisible(rows_.empty());
    heightChanged();
    // The host only re-lays out when the card's height changes; a learning
    // row committing to a mapping row of the same height still needs the new
    // rows placed, or they sit at 0×0 and the card paints empty.
    resized();
  }

  // Hands back the row for `targetId` (so a listening row survives a rebuild).
  std::unique_ptr<Row> takeRow(const juce::String& targetId) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it)
      if ((*it)->targetId() == targetId) {
        auto row = std::move(*it);
        rows_.erase(it);
        removeChildComponent(row.get());
        return row;
      }
    return nullptr;
  }

  float heightFor(float width) const override {
    if (rows_.empty()) return 2 + 2 * kEmptyPad + empty_.heightFor(width - 2 - 2 * kEmptyPad);
    int h = 2;
    for (const auto& row : rows_) h += row->height();
    return static_cast<float>(h);
  }

  void paint(juce::Graphics& g) override {
    paint::border(g, getLocalBounds().toFloat(), form::kCardRadius, form::kFieldBorder);
  }

  void resized() override {
    const auto inner = getLocalBounds().reduced(1);
    if (rows_.empty()) {
      empty_.setBounds(inner.reduced(kEmptyPad));
      return;
    }
    int y = inner.getY();
    for (auto& row : rows_) {
      row->setBounds(inner.getX(), y, inner.getWidth(), row->height());
      y += row->height();
    }
  }

private:
  std::vector<std::unique_ptr<Row>> rows_;
  Paragraph empty_;
};

// MidiMapSection
MidiMapSection::MidiMapSection(Services& services)
    : services_(services),
      list_(std::make_unique<ListCard>()),
      caption_(kPresetsCaption),
      channel_("MIDI Channel", "Omni listens on every channel.") {
  picker_.setPlaceholder("Choose a control");
  picker_.onChange = [this](const juce::String& targetId) { services_.midiMap.startLearn(targetId); };

  std::vector<SelectField::Option> channels{{"0", "Omni", {}}};
  for (int i = 1; i <= 16; ++i) channels.push_back({juce::String(i), "Channel " + juce::String(i), {}});
  channelSelect_.setOptions(std::move(channels));
  channelSelect_.onChange = [this](const juce::String& v) { services_.midiMap.setChannel(v.getIntValue()); };
  channel_.content().add(channelSelect_);

  add(*list_);
  add(picker_, form::kControlGap);
  add(caption_, form::kControlGap);
  add(channel_, form::kSectionGap);

  services_.midiMap.addListener(this);
  services_.chain.addListener(this);
  rebuild();
}

MidiMapSection::~MidiMapSection() {
  services_.chain.removeListener(this);
  services_.midiMap.removeListener(this);
}

juce::String MidiMapSection::targetContext(const juce::String& targetId) const {
  const auto block = midi::blockPowerTarget(targetId);
  if (!block) {
    const auto* target = midi::targetById(targetId);
    return target != nullptr ? target->group : juce::String();
  }
  const auto& chain = services_.chain.state();
  const bool stereo = chain.chainRight.has_value();
  // Lanes are only worth naming while two exist.
  const juce::String lane = block->right ? "Chain R" : stereo ? "Chain L" : "Chain";
  const juce::String sep = juce::String::fromUTF8(" \xc2\xb7 ");
  if (block->right && !stereo) return lane + sep + "Stereo off";
  const auto& items = block->right ? *chain.chainRight : chain.chain;
  int seen = 0;
  for (const auto& item : items) {
    if (!item.isTone()) continue;
    if (seen++ == block->index) return lane + sep + item.tone.title;
  }
  return lane + sep + "Empty slot";
}

void MidiMapSection::rebuild() {
  const auto& state = services_.midiMap.state();
  if (!state) return;
  const auto& learn = state->learnTargetId;

  // The listening row survives a rebuild for the same target (its typed
  // draft with it); a new target starts clean.
  std::unique_ptr<Row> learning = learn.isNotEmpty() ? list_->takeRow(learn) : nullptr;
  if (learning != nullptr && dynamic_cast<LearningRow*>(learning.get()) == nullptr) learning.reset();
  if (learning == nullptr) ccDraft_.clear();
  auto makeLearning = [&](const juce::String& targetId) -> std::unique_ptr<Row> {
    if (learning != nullptr) return std::move(learning);
    return std::make_unique<LearningRow>(
        targetId, targetContext(targetId),
        [this](const juce::String& draft) {
          ccDraft_ = draft;
          armTimeout();
        },
        [this] { defer([this] { commitCc(); }); },
        [this] { defer([this] { services_.midiMap.cancelLearn(); }); });
  };

  std::vector<std::unique_ptr<Row>> rows;
  bool pending = learn.isNotEmpty();
  for (const auto& mapping : state->mappings) {
    if (mapping.targetId == learn) {
      rows.push_back(makeLearning(mapping.targetId));
      pending = false;
    } else {
      const auto id = mapping.targetId;
      rows.push_back(std::make_unique<MappingRow>(
          mapping, targetContext(id), [this, id] { defer([this, id] { services_.midiMap.startLearn(id); }); },
          [this, id] { defer([this, id] { services_.midiMap.removeMapping(id); }); }));
    }
  }
  // A learn armed for a not-yet-mapped target renders as a pending row at
  // the end of the list.
  if (pending) rows.push_back(makeLearning(learn));
  list_->setRows(std::move(rows));

  // Picker: still-unmapped targets; block powers only for blocks that exist.
  const auto& chain = services_.chain.state();
  auto laneCount = [&](bool right) {
    const auto* items = right ? (chain.chainRight ? &*chain.chainRight : nullptr) : &chain.chain;
    int n = 0;
    if (items != nullptr)
      for (const auto& item : *items) n += item.isTone() ? 1 : 0;
    return n;
  };
  std::vector<SelectField::Option> options;
  for (const auto& target : midi::mappableTargets()) {
    if (state->mappingFor(target.id) != nullptr) continue;
    if (const auto block = midi::blockPowerTarget(target.id); block && block->index >= laneCount(block->right))
      continue;
    options.push_back({target.id, target.name, targetContext(target.id)});
  }
  const bool anyOptions = !options.empty();
  picker_.setOptions(std::move(options));
  picker_.setValue(std::nullopt);
  setShown(picker_, anyOptions);

  channelSelect_.setValue(juce::String(state->channel));
  armTimeout();
}

// Learn is armed until hardware answers; give up after a while so an
// unplugged controller doesn't leave the row listening forever. Typing a CC
// number means the user is present, so a draft pauses the timer.
void MidiMapSection::armTimeout() {
  const auto& state = services_.midiMap.state();
  const bool armed = state && state->learnTargetId.isNotEmpty();
  if (!armed || ccDraft_.isNotEmpty()) {
    learnTimeout_.cancel();
    return;
  }
  learnTimeout_.start(kLearnTimeoutMs, [this] { services_.midiMap.cancelLearn(); });
}

// Out-of-range numbers are ignored (digits-only, so 128-999 is the only
// invalid shape).
void MidiMapSection::commitCc() {
  const auto& state = services_.midiMap.state();
  if (!state || ccDraft_.isEmpty()) return;
  const int number = ccDraft_.getIntValue();
  if (number <= 127) services_.midiMap.setCcMapping(state->learnTargetId, number);
}

// The store notifies synchronously, so a change asked for by a row control
// would rebuild the list, and destroy that control, under its own callback.
// Posting the change lets the click or Return finish first; the section may
// have gone by then (Settings closed), so the action is skipped if so.
void MidiMapSection::defer(std::function<void()> action) {
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MidiMapSection>(this), action = std::move(action)] {
        if (safe != nullptr) action();
      });
}

}  // namespace t3k::ui
