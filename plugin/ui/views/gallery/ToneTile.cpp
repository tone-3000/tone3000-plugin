#include "ToneTile.h"

#include "GalleryGeometry.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kImageFadeMs = 200;
constexpr float kDimmedImage = 0.35f;
const juce::Colour kStrip = juce::Colours::black.withAlpha(0.35f);
}  // namespace

void ToneTile::Strip::paint(juce::Graphics& g) {
  g.setColour(kStrip);
  g.fillRect(getLocalBounds().withHeight(kChromeHeight));
}

ToneTile::ToneTile(Services& services, const ChainItem& block, int size)
    : GalleryTile(services, block.blockId, size),
      block_(block),
      image_(services.images),
      power_(Icon::Power, ChromeIconButton::Tone::power, help::Key::blockPower),
      swap_(Icon::ArrowLeftRight, ChromeIconButton::Tone::plain, help::Key::swapTone),
      remove_(Icon::Trash2, ChromeIconButton::Tone::plain, help::Key::removeBlock),
      glow_(services.meters, MeterStore::blockOutId(block.blockId)),
      led_(services.meters, MeterStore::blockOutId(block.blockId)) {
  image_.setCornerRadius(gallery::kTileCorner);
  image_.setGlow(glow_.glow());
  glow_.onChange = [this] { image_.setGlow(glow_.glow()); };
  addAndMakeVisible(image_);
  addChildComponent(dots_);
  addChildComponent(retry_);
  retry_.onRetry = [this] { this->services().modelLoads.retry(blockId()); };

  addAndMakeVisible(chrome_);
  for (auto* b : {&power_, &swap_, &remove_}) chrome_.addAndMakeVisible(*b);
  power_.onClick = [this] { togglePower(); };
  swap_.onClick = [this] { if (onSwap) onSwap(blockId()); };
  remove_.onClick = [this] { this->services().chain.removeBlock(blockId()); };

  addAndMakeVisible(ledSlot_);
  ledSlot_.addChildComponent(led_);
  ledSlot_.setSize(BlockLed::kSize, BlockLed::kSize);
  addMouseListener(&hover_, true);
  services.pointer.addListener(this);

  setBlock(block);
  setHovered(false);
  resized();  // the base set the size before these children existed
}

ToneTile::~ToneTile() {
  services().pointer.removeListener(this);
  removeMouseListener(&hover_);
}

void ToneTile::setBlock(const ChainItem& block) {
  block_ = block;
  enabled_ = block.params.enabled;
  setHelpText(help::toneTile(block.tone.title));
  setTitle(block.tone.title);
  image_.setTone(block.tone.image, block.tone.gear, block.tone.local, kGlyphSize);
  syncState();
}

// A model download/prepare is in flight: `modelLoading` covers switches
// (the previous model keeps playing, so `loaded` stays true) and `!loaded`
// covers fresh blocks that have nothing to play yet.
void ToneTile::syncState() {
  // After a short grace (BusyGrace): a quick switch leaves the tile as it is.
  const bool busy = busyGrace_.shown(block_.modelLoading || (!block_.loaded && !block_.loadFailed),
                                     [this] { syncState(); });
  // Only a drop on the middle swaps (the tile gives way to the drop glyph);
  // an edge drop adds beside it, so the tile stays as it is.
  const bool armed = dropArmed() && dropEdge() == DropEdge::none;
  image_.setVisible(!armed);
  imageFade_.animateTo(enabled_ && !busy && !block_.loadFailed ? 1.0f : kDimmedImage, kImageFadeMs);
  dots_.setVisible(!armed && busy && !block_.loadFailed);
  retry_.setVisible(!armed && block_.loadFailed);
  chrome_.setVisible(!armed);
  ledSlot_.setVisible(!armed);
  power_.setOn(enabled_);
  repaint();
}

void ToneTile::togglePower() {
  enabled_ = !enabled_;
  power_.setOn(enabled_);
  imageFade_.animateTo(enabled_ ? 1.0f : kDimmedImage, kImageFadeMs);
  this->services().chain.setBlockParam(blockId(), "enabled", enabled_);
}

void ToneTile::open() {
  if (onOpen) onOpen(blockId());
}

std::vector<ContextMenu::Item> ToneTile::menuItems() {
  std::vector<ContextMenu::Item> items{
      {"Copy", Icon::Copy, help::Key::copyBlock,
       [this] { this->services().chain.copyBlock(blockId()); }},
  };
  for (auto& item : localLoadItems()) items.push_back(std::move(item));
  items.push_back({"Add Before...", Icon::ArrowLeft, help::Key::addBefore,
                   [this] { if (onAddBeside) onAddBeside(slotBefore(blockId())); }});
  items.push_back({"Add After...", Icon::ArrowRight, help::Key::addAfter,
                   [this] { if (onAddBeside) onAddBeside(slotAfter(blockId())); }});
  items.push_back({"Add to Library", Icon::LibraryBig, help::Key::libraryAddBlock,
                   [this] { this->services().library.beginAdd(blockId()); }});
  if (services().library.canShow(blockId()))
    items.push_back({"Show in Library", Icon::FolderOpen, help::Key::libraryShowBlock,
                     [this] { this->services().library.showBlock(blockId()); }});
  return items;
}

void ToneTile::dropArmedChanged(bool) { syncState(); }

void ToneTile::travellingChanged(bool) { setHovered(hovered_); }

void ToneTile::pointerMoved(const juce::MouseEvent& e, bool leaving) {
  // Leaving onto one of our own children is still hovering the tile.
  setHovered(!leaving || getLocalBounds().contains(e.getEventRelativeTo(this).getPosition()));
}

void ToneTile::setHovered(bool hovered) {
  hovered_ = hovered;
  const bool shown = services().pointer.coarse() || hovered_ || travelling();
  chrome_.setAlpha(shown ? 1.0f : 0.0f);
  chrome_.setInterceptsMouseClicks(false, shown);
}

void ToneTile::resized() {
  const auto box = getLocalBounds();
  image_.setBounds(box);
  dots_.setBounds(box.withSizeKeepingCentre(LoadingDots::kWidth, LoadingDots::kDot + 2 * LoadingDots::kMargin));
  retry_.setBounds(box.withSizeKeepingCentre(retry_.getWidth(), retry_.getHeight()));

  chrome_.setBounds(box.withHeight(kChromeHeight));
  const int y = kChromePad;
  power_.setBounds(kChromePad, y, theme::kIconBoxSize, theme::kIconBoxSize);
  remove_.setBounds(getWidth() - kChromePad - theme::kIconBoxSize, y, theme::kIconBoxSize,
                    theme::kIconBoxSize);
  swap_.setBounds(remove_.getX() - kChromeGap - theme::kIconBoxSize, y, theme::kIconBoxSize,
                  theme::kIconBoxSize);

  ledSlot_.setTopLeftPosition(getWidth() - kLedInset - BlockLed::kSize,
                              getHeight() - kLedInset - BlockLed::kSize);
}

void ToneTile::paint(juce::Graphics& g) {
  paint::fill(g, getLocalBounds().toFloat(), gallery::kTileCorner, theme::kSurface);
  if (dropArmed() && dropEdge() == DropEdge::none) {
    const float s = gallery::kFileDropGlyphSize;
    Icons::draw(g, Icon::Upload, getLocalBounds().toFloat().withSizeKeepingCentre(s, s), theme::kGray);
  }
}

void ToneTile::paintOverChildren(juce::Graphics& g) {
  // A drop on the middle swaps: the outline. An edge drop's mark is in the
  // gap beside the tile (ChainView), the tile itself stays as it is.
  if (dropArmed() && dropEdge() == DropEdge::none)
    paint::dashedBorder(g, getLocalBounds().toFloat(), gallery::kTileCorner, gallery::kFileDropBorder,
                        gallery::kAddTileBorderWidth);
}

}  // namespace t3k::ui
