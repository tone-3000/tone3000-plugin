#include "InputChannelPicker.h"

#include <algorithm>

#include "core/Fonts.h"
#include "core/MeterScale.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/DotMeter.h"
#include "widgets/DragScroller.h"

namespace t3k::ui {

// ChannelRow
// Indicator, 1-based index, mono channel name (ellipsised) and the live peak
// strip; the whole row is the button, lit on hover.
class InputChannelPicker::ChannelRow : public juce::Component {
public:
  ChannelRow(const AudioInputChannel& channel, std::function<void()> onClick)
      : channel_(channel), meter_(kMeterLength, false), onClick_(std::move(onClick)) {
    setName(juce::String(channel.index + 1));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    meter_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(meter_);
  }

  static int height() { return 2 * kRowPadY + ChoiceIndicator::kSize; }

  void setSelected(bool selected, bool square) {
    if (selected_ == selected && square_ == square) return;
    selected_ = selected;
    square_ = square;
    repaint();
  }
  void setLevel(float db) {
    meter_.setLevel(db);
    meter_.setClipped(db >= meter::kMaxDb);
  }

  void paint(juce::Graphics& g) override {
    if (isMouseOver()) paint::fill(g, getLocalBounds().toFloat(), kRowRadius, form::kRowHover);
    const auto box = getLocalBounds().reduced(kRowPadX, kRowPadY);
    ChoiceIndicator::paint(g, juce::Rectangle<float>(box.getX(), box.getY(), ChoiceIndicator::kSize,
                                                     ChoiceIndicator::kSize),
                           selected_, square_);
    const auto indexFont = Fonts::sans(form::kBodyPx);
    const juce::String index(channel_.index + 1);
    const int indexW = juce::jmax(kIndexMinWidth, juce::roundToInt(Fonts::width(indexFont, index)));
    const float indexLine = static_cast<float>(Fonts::normalLineHeight(form::kBodyPx));
    float x = static_cast<float>(box.getX() + ChoiceIndicator::kSize + kRowGap);
    paint::cssLine(g, index, x, box.getCentreY() - indexLine / 2, indexLine, static_cast<float>(indexW), indexFont,
                   selected_ ? theme::kWhite : theme::kMuted);
    x += indexW + kRowGap;
    const auto nameFont = Fonts::mono(kNamePx);
    const float nameLine = static_cast<float>(Fonts::normalLineHeight(nameFont));
    const float nameW = meter_.getX() - kRowGap - x;
    paint::cssLine(g, channel_.name, x, box.getCentreY() - nameLine / 2, nameLine, nameW, nameFont, theme::kSubtle);
  }

  void resized() override {
    const auto box = getLocalBounds().reduced(kRowPadX, kRowPadY);
    meter_.setTopLeftPosition(box.getRight() - meter_.getWidth(), box.getCentreY() - meter_.getHeight() / 2);
  }

  void mouseEnter(const juce::MouseEvent&) override { repaint(); }
  void mouseExit(const juce::MouseEvent&) override { repaint(); }
  void mouseUp(const juce::MouseEvent& e) override {
    if (getLocalBounds().contains(e.getPosition()) && !e.mouseWasDraggedSinceMouseDown()) onClick_();
  }

private:
  AudioInputChannel channel_;
  bool selected_ = false, square_ = false;
  DotMeter meter_;
  std::function<void()> onClick_;
};

// ChannelList
// The rows, scrolling past kListMaxHeight (hidden scrollbar).
class InputChannelPicker::ChannelList : public FormItem {
public:
  ChannelList() {
    viewport_.setViewedComponent(&column_, false);
    addAndMakeVisible(viewport_);
  }

  void setRows(std::vector<std::unique_ptr<ChannelRow>> rows) {
    rows_ = std::move(rows);
    for (auto& row : rows_) column_.addAndMakeVisible(*row);
    heightChanged();
    resized();
  }
  std::vector<std::unique_ptr<ChannelRow>>& rows() { return rows_; }

  float heightFor(float) const override {
    return static_cast<float>(juce::jmin(kListMaxHeight, static_cast<int>(rows_.size()) * ChannelRow::height()));
  }

  void resized() override {
    viewport_.setBounds(getLocalBounds());
    column_.setSize(getWidth(), static_cast<int>(rows_.size()) * ChannelRow::height());
    int y = 0;
    for (auto& row : rows_) {
      row->setBounds(0, y, getWidth(), ChannelRow::height());
      y += ChannelRow::height();
    }
  }

private:
  DragScroller viewport_{DragScroller::Axis::vertical};
  juce::Component column_;
  std::vector<std::unique_ptr<ChannelRow>> rows_;
};

// InputChannelPicker
InputChannelPicker::InputChannelPicker(Services& services)
    : FieldRow("Input Channel"),
      services_(services),
      mode_({"Mono", "Stereo"}),
      list_(std::make_unique<ChannelList>()),
      stereoCaption_("Pick any two. Selecting a third swaps out your oldest pick.") {
  mode_.setName("Input mode");
  mode_.setSize(mode_.preferredWidth(), SegmentedControl::preferredHeight());
  mode_.onChange = [this](int index) { switchMode(index == 1); };
  emptyCaption_.setItalicText("No input channels. Select an interface above.");
  auto& stack = content();
  stack.add(*list_);
  stack.add(stereoCaption_, form::kControlGap);
  stack.add(emptyCaption_);
  stack.add(noInput_, form::kControlGap);
}

InputChannelPicker::~InputChannelPicker() = default;

void InputChannelPicker::update(const AudioDeviceState& state) {
  const bool channelsChanged = channels_.size() != state.inputChannels.size() ||
                               !std::equal(channels_.begin(), channels_.end(), state.inputChannels.begin(),
                                           [](const AudioInputChannel& a, const AudioInputChannel& b) {
                                             return a.index == b.index && a.name == b.name;
                                           });
  channels_ = state.inputChannels;
  deviceOpen_ = state.deviceOpen;
  inputDevice_ = state.inputDevice;

  active_.clear();
  for (const auto& c : channels_)
    if (c.active) active_.push_back(c.index);
  // Drop order entries that went inactive, append newly-active ones.
  std::vector<int> order;
  for (int i : order_)
    if (std::find(active_.begin(), active_.end(), i) != active_.end()) order.push_back(i);
  for (int i : active_)
    if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
  order_ = std::move(order);

  const bool stereoMode = stereo();
  setLabel(stereoMode ? "Input Channels" : "Input Channel");
  setHelp(channels_.empty() ? juce::String()
          : stereoMode      ? "Play your instrument and pick the two channels with signal."
                            : "Play your instrument and select the channel with signal.");
  mode_.setSelected(stereoMode ? 1 : 0);
  setLabelExtra(channels_.size() > 1 ? &mode_ : nullptr);

  if (channelsChanged) {
    std::vector<std::unique_ptr<ChannelRow>> rows;
    for (const auto& c : channels_)
      rows.push_back(std::make_unique<ChannelRow>(c, [this, index = c.index] { selectChannel(index); }));
    list_->setRows(std::move(rows));
    syncMetering();
  }
  for (auto& row : list_->rows())
    row->setSelected(std::find(active_.begin(), active_.end(), row->getName().getIntValue() - 1) != active_.end(),
                     stereoMode);

  // The no-input banner, mirrored inline. When it shows it replaces the
  // neutral "no channels" caption so the two never stack (they say the same
  // thing). Gated to a running device; a dead device is the top-of-form error.
  const bool showNoInput = deviceOpen_ && (inputDevice_.isEmpty() || active_.empty());
  auto& stack = content();
  stack.setShown(*list_, !channels_.empty());
  stack.setShown(stereoCaption_, !channels_.empty() && stereoMode);
  stack.setShown(emptyCaption_, channels_.empty() && !showNoInput);
  stack.setShown(noInput_, noInput_.update(state, showNoInput));
  levelsChanged();
}

void InputChannelPicker::selectChannel(int index) {
  if (std::find(active_.begin(), active_.end(), index) != active_.end()) return;  // min 1
  std::vector<int> next;
  if (stereo()) {
    next = order_;
    next.push_back(index);
    if (next.size() > 2) next.erase(next.begin(), next.end() - 2);  // max 2: swap out the oldest
  } else {
    next = {index};
  }
  services_.audioDevice.setInputChannels(next);
}

void InputChannelPicker::switchMode(bool toStereo) {
  if (toStereo == stereo()) return;
  const int current = !order_.empty() ? order_.back() : !channels_.empty() ? channels_.front().index : 0;
  if (!toStereo) {
    services_.audioDevice.setInputChannels({current});
    return;
  }
  for (const auto& c : channels_)
    if (c.index != current) {
      services_.audioDevice.setInputChannels({current, c.index});
      return;
    }
}

void InputChannelPicker::syncMetering() {
  const bool wanted = isShowing() && !channels_.empty();
  if (wanted && levels_ == nullptr)
    levels_ = std::make_unique<AudioInputLevels>(services_.backend, services_.clock,
                                                 [this] { levelsChanged(); });
  else if (!wanted)
    levels_.reset();
}

void InputChannelPicker::levelsChanged() {
  static const std::vector<float> kNone;
  const auto& levels = levels_ != nullptr ? levels_->levels() : kNone;
  for (auto& row : list_->rows()) {
    const auto index = static_cast<size_t>(row->getName().getIntValue() - 1);
    row->setLevel(index < levels.size() ? levels[index] : meter::kMinDb);
  }
}

}  // namespace t3k::ui
