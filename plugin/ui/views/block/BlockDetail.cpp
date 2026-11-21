#include "BlockDetail.h"

#include "core/Help.h"

namespace t3k::ui {

namespace {
// The block across both lanes, and whether an enabled+loaded NAM follows it
// in its lane (the DSP's lastNamIndex scan: with calibration on, such a
// block hands off at calibrated output level instead of normalizing).
struct Found {
  const ChainItem* item = nullptr;
  bool namDownstream = false;
};

Found find(const ChainState& state, const std::string& blockId) {
  auto scan = [&](const std::vector<ChainItem>& lane) -> std::optional<Found> {
    for (size_t i = 0; i < lane.size(); ++i) {
      if (lane[i].isInsert || lane[i].blockId != blockId) continue;
      Found f{&lane[i], false};
      for (size_t j = i + 1; j < lane.size(); ++j) {
        const auto& b = lane[j];
        if (b.isTone() && b.tone.isNam() && b.loaded && b.params.enabled) f.namDownstream = true;
      }
      return f;
    }
    return std::nullopt;
  };
  if (auto f = scan(state.chain)) return *f;
  if (state.chainRight)
    if (auto f = scan(*state.chainRight)) return *f;
  return {};
}
}  // namespace

BlockDetail::BlockDetail(Services& services, const std::string& blockId)
    : services_(services),
      blockId_(blockId),
      scroller_(std::make_unique<DragScroller>(DragScroller::Axis::vertical)),
      back_(std::make_unique<BackLink>("BLOCK", help::Key::backToChain)) {
  a11y::makeGroup(*this, "Tone editor");
  back_->onClick = [this] {
    if (onBack) onBack();
  };
  column_.addAndMakeVisible(*back_);
  scroller_->setViewedComponent(&column_, false);
  scroller_->setFocusMargin(kBackGap);  // a focused control clears the row above and below it
  addAndMakeVisible(*scroller_);
  services_.chain.addListener(this);
  sync();
}

BlockDetail::~BlockDetail() { services_.chain.removeListener(this); }

void BlockDetail::chainChanged(const ChainState&) { sync(); }

void BlockDetail::componentMovedOrResized(juce::Component&, bool, bool wasResized) {
  if (wasResized) layout();
}

void BlockDetail::sync() {
  const auto found = find(services_.chain.state(), blockId_);
  if (found.item == nullptr) {
    // Drop a block that vanished: left alone it could reopen a dead view.
    if (onBack) onBack();
    return;
  }
  if (!card_) {
    card_ = std::make_unique<BlockCard>(services_, *found.item, found.namDownstream);
    card_->onSwap = [this] {
      if (onSwap) onSwap(blockId_);
    };
    card_->onInfoVisible = [this](bool open) {
      infoOpen_ = open;
      layout();
    };
    card_->addComponentListener(this);
    column_.addAndMakeVisible(*card_);
  } else {
    card_->setBlock(*found.item, found.namDownstream);
  }
  layout();
}

void BlockDetail::resized() { layout(); }

void BlockDetail::layout() {
  if (!card_) return;
  scroller_->setBounds(getLocalBounds());
  const int x = (getWidth() - BlockCard::kWidth) / 2;
  // The back link keeps its place under the header; the card fills the
  // design column exactly, so a stretched column (PluginRoot::setStretch)
  // centres it in the room below the link. Info view: the pads live in the
  // scroll content; otherwise the column is pinned under the shared pad and
  // never scrolls.
  back_->setTopLeftPosition(x, kPadY);
  const int cardTop = back_->getBottom() + kBackGap;
  const int room = getHeight() - cardTop - kPadY - card_->getHeight();
  card_->setTopLeftPosition(x, cardTop + std::max(0, room) / 2);
  column_.setSize(getWidth(), infoOpen_ ? std::max(card_->getBottom() + kPadY, getHeight()) : getHeight());
  if (!infoOpen_) scroller_->setViewPosition(0, 0);
}

}  // namespace t3k::ui
