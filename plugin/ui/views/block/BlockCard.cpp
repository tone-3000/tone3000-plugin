#include "BlockCard.h"

#include <algorithm>

#include "views/gallery/GalleryGeometry.h"
#include "core/CustomIcons.h"
#include "core/Icons.h"
#include "core/KnobScale.h"
#include "core/Paint.h"
#include "T3kConfig.h"
#include "core/Theme.h"
#include "core/EqMath.h"

namespace t3k::ui {

namespace {
constexpr int kHeaderGap = 24;
constexpr int kBodyGap = 24;
constexpr int kImageSize = 192;
constexpr int kImageSizeInfo = 160;
constexpr int kImageRadius = 8;
constexpr int kRailMeterLength = 160;
constexpr int kRailGap = 12;
constexpr int kNormalizeGap = 10;
// Source / Kept above Keep.
constexpr int kKeepLinkGap = 6;
// KEEP's menu arrow (a TONE3000 tone), just right of it.
constexpr int kKeepMoreWidth = 18;
constexpr int kKeepMoreGap = 3;
// The picture button on the image's corner.
constexpr int kPictureButton = 28;
constexpr int kInfoBottomPad = 24;
constexpr int kEqPillPadX = 12, kEqPillPadY = 4, kEqPillGap = 16;
constexpr int kSelectHeight = 36;
constexpr float kImageBusyOpacity = 0.35f;
const juce::Colour kNestedTrack(118, 118, 128);

Knob::Options trimKnob(const char* label, const KnobScale& scale, float defaultValue, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = defaultValue;
  o.help = help;
  o.labelOnTop = true;
  return o;
}

// Centre `child` horizontally in a column and pin its bottom to `bottom`.
void pinBottom(juce::Component& child, int columnX, int columnW, int bottom) {
  child.setTopLeftPosition(columnX + (columnW - child.getWidth()) / 2, bottom - child.getHeight());
}
}  // namespace

// Calibration gauge: an indicator, not a button. White when the loaded
// model carries calibration data, GRAY when it doesn't.
class BlockCard::Indicator : public juce::Component {
public:
  Indicator() { setSize(theme::kIconBoxSize, theme::kIconBoxSize); }
  void set(bool active) {
    active_ = active;
    setHelpText(help::text(active ? help::Key::blockCalibrated : help::Key::blockUncalibrated));
    repaint();
  }
  void paint(juce::Graphics& g) override {
    Icons::draw(g, Icon::Gauge, juce::Rectangle<float>(theme::kIconSize, theme::kIconSize).withCentre(getLocalBounds().toFloat().getCentre()),
                active_ ? theme::kWhite : theme::kGray);
  }

private:
  bool active_ = false;
};

BlockCard::BlockCard(Services& services, const ChainItem& block, bool namDownstream)
    : services_(services),
      block_(block),
      namDownstream_(namDownstream),
      calibrateInput_(services.backend, "calibrateInput"),
      inMeter_(services.meters, MeterStore::blockInId(block.blockId), kRailMeterLength, /*vertical=*/true),
      outMeter_(services.meters, MeterStore::blockOutId(block.blockId), kRailMeterLength, /*vertical=*/true),
      in_(trimKnob("In", scales::gainDb(), 0.5f, help::Key::blockIn)),
      out_(trimKnob("Out", scales::gainDb(), 0.5f, help::Key::blockOut)),
      mix_(trimKnob("Mix", scales::percent(), 1.0f, help::Key::blockMix)),
      image_(services.images),
      meta_(services.images) {
  setOpaque(false);
  buildHeader();
  buildBody();

  calibrateInput_.onChange = [this] { syncHeader(); };
  services_.session.addListener(this);
  services_.prefs.addListener(this);
  services_.library.addKeepListener(this);

  // Optimistic control values start from the block; native converges.
  enabled_ = block_.params.enabled;
  normalizeOn_ = block_.params.normalize;
  slimFull_ = isSlimSizeFull(block_.params.slimSize);
  eqOn_ = block_.params.eq.enabled;
  eqPre_ = block_.params.eq.pre;
  syncFromBlock();

  // Expand: orphan nothing yet, fetch the latest tone in the background
  // (metadata re-sync + info pre-warm).
  keptToneId_ = keptToneIdNow();
  if (siteToneId() > 0 && authenticated()) fetchInfo(/*background=*/true);
  fetchModels();

  setSize(kWidth, kHeight);
  // Which controls show (Normalize, Keep) is the body view's call; a fresh
  // card must make it too, or they only appeared after an EQ / info toggle
  // or a setting change.
  setBodyView();
}

BlockCard::~BlockCard() {
  if (keptMenu_ != nullptr) keptMenu_->close();  // it lives on the overlay layer
  if (keepMenu_ != nullptr) keepMenu_->close();
  if (pictureMenu_ != nullptr) pictureMenu_->close();
  services_.library.removeKeepListener(this);
  services_.prefs.removeListener(this);
  services_.session.removeListener(this);
}

// Construction
void BlockCard::buildHeader() {
  power_.onClick = [this] {
    enabled_ = !enabled_;
    services_.chain.setBlockParam(block_.blockId, "enabled", enabled_);
    power_.setOn(enabled_);
    body_.setOff(!enabled_);
  };
  addAndMakeVisible(power_);

  calibration_ = std::make_unique<Indicator>();
  addChildComponent(*calibration_);

  addAndMakeVisible(eqPill_);
  eqPill_.setInterceptsMouseClicks(false, true);
  eqPower_.onClick = [this] {
    eqOn_ = !eqOn_;
    services_.chain.setBlockEqEnabled(block_.blockId, eqOn_);
    syncHeader();
    if (eqEditor_) eqEditor_->setEqEnabled(eqOn_);
  };
  eqPill_.addChildComponent(eqPower_);
  pre_.onClick = [this] {
    eqPre_ = !eqPre_;
    services_.chain.setBlockEqPre(block_.blockId, eqPre_);
    pre_.setArmed(eqPre_);
  };
  preGroup_.addAndMakeVisible(pre_);
  preGroup_.setSize(pre_.getWidth(), pre_.getHeight());
  eqPill_.addChildComponent(preGroup_);

  auto viewStyle = SegmentedText::selection();
  viewStyle.track = kNestedTrack.withAlpha(0.24f);
  eqView_ = std::make_unique<SegmentedText>(
      std::vector<SegmentedText::Cell>{
          {"Sliders", help::text(help::Key::eqSlidersView), custom_icons::kEqSliders, 16},
          {"Curve", help::text(help::Key::eqCurveView), custom_icons::kEqCurve, 16}},
      viewStyle);
  eqView_->onCellClick = [this](int index) {
    eqViewMode_ = index == 0 ? BlockEqView::View::sliders : BlockEqView::View::graph;
    eqView_->select(index);
    if (eqEditor_) eqEditor_->setView(eqViewMode_);
  };
  eqView_->select(0);
  eqPill_.addChildComponent(*eqView_);

  eq_.onClick = [this] { setShowEq(!showEq_); };
  eqPill_.addAndMakeVisible(eq_);

  info_.onClick = [this] { setShowInfo(!showInfo_); };
  addChildComponent(info_);
  share_.onClick = [this] { share(); };
  addChildComponent(share_);
  reveal_.onClick = [this] { services_.library.showBlock(block_.blockId); };
  addChildComponent(reveal_);
  swap_.onClick = [this] {
    if (onSwap) onSwap();
  };
  addAndMakeVisible(swap_);
  remove_.onClick = [this] { services_.chain.removeBlock(block_.blockId); };
  addAndMakeVisible(remove_);
}

void BlockCard::buildBody() {
  addAndMakeVisible(body_);

  body_.addAndMakeVisible(inMeter_);
  body_.addAndMakeVisible(outMeter_);

  in_.onChange = [this](float v) { services_.chain.setBlockParam(block_.blockId, "inputGain", static_cast<double>(v)); };
  out_.onChange = [this](float v) { services_.chain.setBlockParam(block_.blockId, "outputGain", static_cast<double>(v)); };
  mix_.onChange = [this](float v) { services_.chain.setBlockParam(block_.blockId, "mix", static_cast<double>(v)); };
  for (auto* knob : {&in_, &out_, &mix_}) body_.addAndMakeVisible(*knob);

  normalize_.onClick = [this] {
    normalizeOn_ = !normalizeOn_;
    services_.chain.setBlockParam(block_.blockId, "normalize", normalizeOn_);
    syncFromBlock();
  };
  normalizeWrap_.addAndMakeVisible(normalize_);
  normalizeWrap_.setSize(normalize_.getWidth(), normalize_.getHeight());
  body_.addChildComponent(normalizeWrap_);

  image_.setCornerRadius(kImageRadius);
  imageFrame_.addAndMakeVisible(image_);
  imageFrame_.addChildComponent(loading_);
  retry_.onRetry = [this] { services_.modelLoads.retry(block_.blockId); };
  imageFrame_.addChildComponent(retry_);
  body_.addAndMakeVisible(imageFrame_);
  picture_.onClick = [this] { pictureClicked(); };
  imageFrame_.addChildComponent(picture_);

  meta_.onToggleFavorite = [this] { toggleFavorite(); };
  meta_.onHeightChanged = [this] { setSize(kWidth, preferredHeight()); };
  meta_.info().onLogin = [this] {
    services_.connection.requireConnection([this] { services_.session.login(); });
  };
  meta_.info().onRetry = [this] { fetchInfo(/*background=*/false); };
  meta_.info().onOpenUrl = [](const juce::String& url) { juce::URL(url).launchInDefaultBrowser(); };
  body_.addAndMakeVisible(meta_);

  select_.onChange = [this](const juce::String& id) { switchModel(id); };
  select_.onTyped = [this](int position, int count) {
    services_.toast.show(juce::String(position) + " / " + juce::String(count), Toast::Style::quiet);
  };
  setWantsKeyboardFocus(true);  // given by a click (mouseDown): numbers, A/B
  setTitle("Block");             // a Tab stop now: its screen-reader name
  addMouseListener(this, true);
  // Opening retries a failed list fetch, so a transient failure never sticks.
  select_.onOpen = [this] {
    if (!modelsLoading_ && models_.empty()) fetchModels();
  };
  selectWrap_.addAndMakeVisible(select_);
  body_.addAndMakeVisible(selectWrap_);
  // Into the keep folder; with none, the Library asks where.
  keep_.onClick = [this] { services_.library.keepAs(block_.blockId, LibraryStore::AddKind::keep); };
  body_.addChildComponent(keep_);
  keepMore_.setSize(kKeepMoreWidth, keep_.getHeight());
  keepMore_.onClick = [this] { openKeepMenu(); };
  body_.addChildComponent(keepMore_);
  original_.onClick = [this] { services_.library.openOriginal(block_.blockId); };
  kept_.onClick = [this] { openKeptMenu(); };
  refresh_.onClick = [this] { services_.library.refreshBlock(block_.blockId); };
  body_.addChildComponent(original_);
  body_.addChildComponent(kept_);
  body_.addChildComponent(refresh_);

  body_.addChildComponent(infoBusy_);
}

// Sync from native
void BlockCard::setBlock(const ChainItem& block, bool namDownstream) {
  // Opened on a block (a tile clicked): the keyboard comes here (numbers,
  // A/B), unless something is being typed into (the Library's search).
  if (block.blockId != keyboardFor_) {
    keyboardFor_ = block.blockId;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BlockCard>(this)] {
      if (safe == nullptr || !safe->isShowing()) return;
      if (dynamic_cast<juce::TextInputTarget*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) return;
      safe->grabKeyboardFocus();
    });
  }
  const int previousToneId = block_.tone.id;
  const bool previousLocal = block_.tone.local;
  const int keptBefore = keptToneId_;
  block_ = block;
  keptToneId_ = keptToneIdNow();
  // A switch within a folder of captures kept from one tone is no change.
  const bool toneChanged = block.tone.id != previousToneId || block.tone.local != previousLocal ||
                           keptToneId_ != keptBefore;
  namDownstream_ = namDownstream;

  // Params can change from outside (undo/redo, another editor window).
  enabled_ = block_.params.enabled;
  normalizeOn_ = block_.params.normalize;
  slimFull_ = isSlimSizeFull(block_.params.slimSize);
  eqOn_ = block_.params.eq.enabled;
  eqPre_ = block_.params.eq.pre;

  if (toneChanged) {
    // Swap: orphan any in-flight fetch, drop the stale payload, refetch.
    infoScope_.reset();
    modelsScope_.reset();
    favoriteScope_.reset();
    infoTone_.reset();
    infoError_.clear();
    infoLoading_ = false;
    favoriteOverride_.reset();
    favoriteBusy_ = false;
    models_.clear();
    modelsLoading_ = false;
    switchingModel_ = false;
    // With the info panel already open (swap from the detail view) the fetch
    // runs foreground so its loading / error UI behaves as before.
    if (siteToneId() > 0 && authenticated()) fetchInfo(/*background=*/!showInfo_);
    syncHeader();
    fetchModels();
  }
  syncFromBlock();
  syncKeepLinks();
}

// One of Source / Kept (a kept copy that was kept again elsewhere goes
// back first), only where Keep shows.
void BlockCard::syncKeepLinks() {
  const bool tone = keep_.isVisible();
  const bool original = tone && services_.library.hasOriginal(block_.blockId);
  const bool kept = tone && !original && !services_.library.keptCopies(block_.blockId).isEmpty();
  const bool more = tone && siteToneId() > 0;  // KEEP's menu: TONE3000 tones (and captures kept from one)
  const int fresh = tone ? services_.library.newInFolder(block_.blockId) : 0;
  if (fresh > 0)
    refresh_.setHelpText("Refresh: add the " + juce::String(fresh) + (fresh == 1 ? " capture" : " captures") +
                         " new in this block's folder.");
  if (original == original_.isVisible() && kept == kept_.isVisible() && more == keepMore_.isVisible() &&
      (fresh > 0) == refresh_.isVisible())
    return;
  original_.setVisible(original);
  kept_.setVisible(kept);
  keepMore_.setVisible(more);
  refresh_.setVisible(fresh > 0);
  resized();
}

// Picture

BlockCard::PictureButton::PictureButton() {
  setHelpText(help::text(help::Key::libraryCardPicture));
  setTitle("Picture");
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void BlockCard::PictureButton::paintButton(juce::Graphics& g, bool over, bool) {
  const auto box = getLocalBounds().toFloat();
  g.setColour(theme::kBlack.withAlpha(over ? 0.8f : 0.6f));
  g.fillEllipse(box);
  Icons::draw(g, Icon::Image, box.reduced(box.getWidth() * 0.25f), over ? theme::kWhite : theme::kWhite.withAlpha(0.85f));
}

void BlockCard::pictureClicked() {
  auto& library = services_.library;
  const auto folder = library.pictureFolderFor(block_.blockId);
  if (folder.isEmpty()) return;
  if (library.pictureFor(folder) == juce::File()) return library.choosePicture(folder);
  if (auto* old = pictureMenu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  pictureMenu_ = std::make_unique<ContextMenu>(std::vector<ContextMenu::Item>{
      {"Change Picture...", Icon::Image, help::Key::librarySetPicture, [this, folder] { services_.library.choosePicture(folder); }},
      {"Remove Picture", Icon::X, help::Key::libraryRemovePicture, [this, folder] { services_.library.removePicture(folder); }},
  });
  pictureMenu_->openAtPoint(picture_, {0, picture_.getHeight()});
}

// The loading look (dimmed artwork, spinner) after a short grace, so a quick
// switch (a local capture) leaves the picture as it is.
void BlockCard::syncBusy() {
  const bool busy = busyGrace_.shown(modelBusy(), [this] { syncBusy(); }) || block_.loadFailed;
  image_.setAlpha(busy ? kImageBusyOpacity : 1.0f);
  loading_.setVisible(busy && !block_.loadFailed);
  retry_.setVisible(block_.loadFailed);
}

int BlockCard::keptToneIdNow() {
  if (!isLocal()) return 0;
  const auto site = services_.library.siteOriginalForBlock(block_.blockId);
  return site ? site->toneId : 0;
}

// KEEP's menu (a TONE3000 tone): the capture, as KEEP does, or the whole
// tone, as a reference or downloaded.
void BlockCard::openKeepMenu() {
  auto& library = services_.library;
  const auto id = block_.blockId;
  std::vector<ContextMenu::Item> items;
  using Kind = LibraryStore::AddKind;
  items.push_back({"Keep Capture", Icon::Download, help::Key::libraryKeepCapture,
                   [&library, id] { library.keepAs(id, Kind::keep); }});
  items.push_back({"Keep as Reference", Icon::Link, help::Key::libraryKeepTone,
                   [&library, id] { library.keepAs(id, Kind::keepLink); }});
  items.push_back({"Download All Captures", Icon::FolderPlus, help::Key::libraryDownloadTone,
                   [&library, id] { library.keepAs(id, Kind::download); }, library.downloading()});
  if (auto* old = keepMenu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  keepMenu_ = std::make_unique<ContextMenu>(std::move(items));
  keepMenu_->openAtPoint(keepMore_, {0, keepMore_.getHeight()});
}

void BlockCard::openKeptMenu() {
  auto& library = services_.library;
  const auto copies = library.keptCopies(block_.blockId);
  if (copies.isEmpty()) return syncKeepLinks();
  if (copies.size() == 1) return library.openKept(block_.blockId, copies.getFirst());
  std::vector<ContextMenu::Item> items;
  for (const auto& copy : copies) {
    const auto folder = copy.getParentDirectory();
    items.push_back({folder.getFileName(), Icon::FolderOpen, help::Key::libraryKept,
                     [this, copy] { services_.library.openKept(block_.blockId, copy); }, false,
                     copy.getFileName() + juce::String::fromUTF8(" \xc2\xb7 in ") + folder.getFullPathName()});
  }
  if (auto* old = keptMenu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  keptMenu_ = std::make_unique<ContextMenu>(std::move(items));
  keptMenu_->openAtPoint(kept_, {0, kept_.getHeight()});
}

void BlockCard::syncFromBlock() {
  power_.setOn(enabled_);
  body_.setOff(!enabled_);

  in_.setValue(static_cast<float>(block_.params.inputGain));
  out_.setValue(static_cast<float>(block_.params.outputGain));
  mix_.setValue(static_cast<float>(block_.params.mix));
  out_.setHelpText(help::text(isNam() || block_.irLong ? help::Key::blockOut : help::Key::blockOutIr));

  // Long (reverb-like) IRs load half wet; Alt-click reset on Mix must agree.
  // (The knob's default is fixed at construction, so the reset hook covers
  // the IR case.)
  mix_.onReset = nullptr;
  if (block_.irLong) {
    mix_.onReset = [this] {
      mix_.setValue(0.5f);
      services_.chain.setBlockParam(block_.blockId, "mix", 0.5);
    };
  }

  const bool overridden = normalizeOverridden();
  normalize_.setArmed(normalizeOn_ && !overridden);
  normalize_.setEnabled(!overridden);
  normalize_.setInterceptsMouseClicks(!overridden, false);
  normalizeWrap_.setHelpText(help::text(overridden ? help::Key::blockNormalizeOverridden : help::Key::blockNormalize));
  normalizeWrap_.setMouseCursor(juce::MouseCursor::NormalCursor);

  image_.setTone(block_.tone.image, block_.tone.gear, block_.tone.local);
  syncPicture();
  syncBusy();

  if (eqEditor_) {
    eqEditor_->setBands(block_.params.eq.bands);
    eqEditor_->setEqEnabled(eqOn_);
    eqEditor_->setSampleRate(services_.chain.state().sampleRate);
  }

  syncHeader();
  syncMeta();
  syncModelSelect();
  resized();
}

void BlockCard::syncHeader() {
  // NAM size chrome: with per-block choice on, the LITE/FULL toggle; off,
  // a locked chip that only appears when the block differs from the default.
  const bool sizeControlEnabled = services_.prefs.getBool(UiPrefs::kShowBlockSizeControl, false);
  const bool defaultFull = isSlimSizeFull(services_.chain.state().namSlimSizeDefault);
  const bool showSize = isNam() && (sizeControlEnabled || slimFull_ != defaultFull);
  const int wantCells = !showSize ? 0 : sizeControlEnabled ? 2 : 1;
  // A size change re-selects the toggle but relabels (rebuilds) the chip. The
  // toggle must never be rebuilt here: its click handler calls back into this
  // through the store's synchronous refresh, and destroying it there frees the
  // closure still running (a reliable crash on Windows, where the compiler
  // reloads the captured pointer from the closure after the call).
  const int signature = wantCells * 2 + (wantCells == 1 && slimFull_ ? 1 : 0);
  if (signature != sizeSignature_) {
    sizeSignature_ = signature;
    size_.reset();
    if (wantCells == 2) {
      size_ = std::make_unique<SegmentedText>(
          std::vector<SegmentedText::Cell>{{"LITE", help::Key::blockSize}, {"FULL", help::Key::blockSize}},
          SegmentedText::selection());
      size_->onCellClick = [this](int index) {
        slimFull_ = index == 1;
        services_.chain.setBlockSlimSize(block_.blockId, slimFull_ ? kSlimSizeFull : kSlimSizeLite);
        syncHeader();
      };
    } else if (wantCells == 1) {
      // Styled like the toggle's unselected side, locked; its hint points at
      // the setting.
      size_ = std::make_unique<SegmentedText>(
          std::vector<SegmentedText::Cell>{{slimFull_ ? "FULL" : "LITE", help::Key::blockSizeChip}},
          SegmentedText::selection());
      size_->setInteractive(false);
    }
    if (size_) addAndMakeVisible(*size_);
  }
  if (wantCells == 2) size_->select(slimFull_ ? 1 : 0);

  const bool showCalibration = isNam() && calibrateInput_.boolValue();
  calibration_->setVisible(showCalibration);
  calibration_->set(block_.inputLevelDbu.has_value());

  // EQ is shaping this block's audio: powered and not flat. Optimistic power
  // so the header glow reacts to the toggle immediately.
  eq_.setArmed(eqOn_ && !block_.params.eq.isFlat());
  eq_.setOpen(showEq_);
  eqPower_.setOn(eqOn_);
  preGroup_.setOff(!eqOn_);
  pre_.setArmed(eqPre_);
  eqView_->select(eqViewMode_ == BlockEqView::View::sliders ? 0 : 1);
  for (auto* c : std::initializer_list<juce::Component*>{&eqPower_, &preGroup_, eqView_.get()}) c->setVisible(showEq_);

  info_.setVisible(siteToneId() > 0);
  info_.setOpen(showInfo_);
  share_.setVisible(siteToneId() > 0);
  reveal_.setVisible(services_.library.canShow(block_.blockId));
  layoutHeader(getLocalBounds().reduced(1).removeFromTop(kHeaderHeight));
}

ToneSummary BlockCard::shownTone() const {
  auto tone = block_.tone;
  if (!isLocal() || keptToneId_ <= 0 || !infoTone_ || infoTone_->id != keptToneId_) return tone;
  // Kept from a TONE3000 tone: its stats, as on the tone's own card.
  tone.local = false;
  tone.publishedAt = infoTone_->publishedAt;
  tone.downloadsCount = infoTone_->downloadsCount;
  tone.favoritesCount = infoTone_->favoritesCount;
  tone.modelsCount = infoTone_->modelsCount;
  tone.a2ModelsCount = infoTone_->a2ModelsCount;
  if (tone.url.isEmpty()) tone.url = infoTone_->url;
  return tone;
}

void BlockCard::syncMeta() {
  const auto shown = shownTone();
  meta_.setTone(shown);
  ToneMeta::Counts counts;
  counts.downloads = shown.downloadsCount;
  counts.favorites = favoritesCount();
  counts.favorited = favorited();
  counts.models = shown.catalogModelCount();
  counts.favoriteToggle = authenticated() && siteToneId() > 0;
  meta_.setCounts(counts);

  BlockInfoPanel::State state;
  state.authenticated = authenticated();
  state.error = infoError_;
  state.tone = infoTone_;
  state.pageUrl = tonePageUrl();
  meta_.info().setState(state);
  // Fetch in flight: the panel steps aside and the body shows BusyOverlay.
  meta_.setInfoVisible(showInfo_ && !infoLoading_);
  infoBusy_.setVisible(showInfo_ && infoLoading_);
}

void BlockCard::syncModelSelect() {
  // Local tones own their model list; catalog tones show the full catalog
  // once loaded, just the active model until then.
  std::vector<ModelSelect::Option> options;
  if (isLocal() || models_.empty()) {
    for (const auto& m : block_.tone.models) options.push_back({juce::String(m.id), m.name});
  } else {
    for (const auto& m : models_) options.push_back({juce::String(m.id), m.name});
  }
  select_.setOptions(std::move(options));
  select_.setValue(juce::String(block_.activeModelId));
  select_.setTotalCount(isLocal() ? static_cast<int>(block_.tone.models.size()) : block_.tone.catalogModelCount());
  select_.setLoading(modelsLoading_);
  // Switching catalog models re-downloads through native with a Bearer
  // token, so the picker is inert while signed out; the wrapper carries the
  // cursor and hint.
  const bool disabled = !isLocal() && !authenticated();
  select_.setDisabledLook(disabled);
  select_.setInterceptsMouseClicks(!disabled, !disabled);
  selectWrap_.setHelpText(disabled ? help::text(help::Key::modelSelectSignedOut) : juce::String());
  selectWrap_.setMouseCursor(juce::MouseCursor::NormalCursor);
}

void BlockCard::sessionChanged() {
  // Auth arrival re-runs the models fetch with the guard now open.
  if (authenticated() && models_.empty()) fetchModels();
  syncMeta();
  syncModelSelect();
}

void BlockCard::keepChanged() { setBodyView(); }

void BlockCard::prefChanged(const juce::String& key) {
  if (key == UiPrefs::kShowBlockSizeControl || key == UiPrefs::kShowBlockNormalizeControl) {
    syncFromBlock();
    // Which controls show is the body view's call (the normalize button
    // lives there): without this an open card kept the old set until it was
    // rebuilt, so turning the setting on showed nothing.
    setBodyView();
  }
}

// Derived state
bool BlockCard::normalizeOverridden() const {
  // Mirrors the DSP's calibrated hand-off (Processor.cpp): calibration on,
  // sane output_level_dbu metadata, and another NAM downstream.
  const bool handOffSane = block_.outputLevelDbu && *block_.outputLevelDbu >= -60 && *block_.outputLevelDbu <= 60;
  return isNam() && calibrateInput_.boolValue() && namDownstream_ && handOffSane;
}

bool BlockCard::favorited() const {
  if (favoriteOverride_) return favoriteOverride_->on;
  if (infoTone_ && infoTone_->isFavorite) return *infoTone_->isFavorite;
  return block_.tone.isFavorite.value_or(false);
}

int BlockCard::favoritesCount() const {
  if (favoriteOverride_) return favoriteOverride_->count;
  if (infoTone_) return infoTone_->favoritesCount;
  return shownTone().favoritesCount;
}

juce::String BlockCard::tonePageUrl() const {
  if (infoTone_ && infoTone_->url.isNotEmpty()) return infoTone_->url;
  if (block_.tone.url.isNotEmpty()) return block_.tone.url;
  return juce::String(config::kApiOrigin) + "/tones/" + juce::String(siteToneId());
}

// Views
void BlockCard::setShowEq(bool show) {
  showEq_ = show;
  if (show) showInfo_ = false;
  if (show && !eqEditor_) {
    eqEditor_ = std::make_unique<BlockEqView>(services_, block_.blockId);
    eqEditor_->setBands(block_.params.eq.bands);
    eqEditor_->setEqEnabled(eqOn_);
    eqEditor_->setView(eqViewMode_);
    eqEditor_->setSampleRate(services_.chain.state().sampleRate);
    body_.addAndMakeVisible(*eqEditor_);
  }
  setBodyView();
}

void BlockCard::setShowInfo(bool show) {
  if (show == showInfo_) return;
  if (show) {
    showEq_ = false;
    showInfo_ = true;
    if (authenticated() && (!infoTone_ || infoTone_->id != siteToneId())) fetchInfo(/*background=*/false);
  } else {
    showInfo_ = false;
  }
  setBodyView();
  if (onInfoVisible) onInfoVisible(showInfo_);
}

// The picture button: the tone view's artwork only (the info view shows a
// smaller image it isn't laid out for), and not on a capture kept from a
// TONE3000 tone (it wears the tone's artwork).
void BlockCard::syncPicture() {
  picture_.setVisible(LibraryStore::canReveal() && keptToneId_ == 0 && body() == Body::tone &&
                      services_.library.pictureFolderFor(block_.blockId).isNotEmpty());
}

void BlockCard::setBodyView() {
  const auto view = body();
  syncPicture();
  if (eqEditor_) eqEditor_->setVisible(view == Body::eq);
  for (auto* c : std::initializer_list<juce::Component*>{&inMeter_, &outMeter_, &in_, &out_, &mix_, &selectWrap_})
    c->setVisible(view == Body::tone);
  normalizeWrap_.setVisible(view == Body::tone && isNam() &&
                            services_.prefs.getBool(UiPrefs::kShowBlockNormalizeControl, false));
  const auto keeping = services_.library.keepTargetName();
  keep_.setVisible(view == Body::tone);
  keep_.setArmed(keeping.isNotEmpty());
  keep_.setHelpText(keeping.isNotEmpty()
                        ? juce::String::fromUTF8("Keep: save the model this block plays into \xe2\x80\x9c") + keeping +
                              juce::String::fromUTF8("\xe2\x80\x9d.")
                        : help::text(help::Key::libraryKeep));
  syncKeepLinks();
  imageFrame_.setVisible(view != Body::eq);
  meta_.setVisible(view != Body::eq);
  syncHeader();
  syncMeta();
  setSize(kWidth, preferredHeight());
  resized();
}

int BlockCard::preferredHeight() {
  if (body() != Body::info) return kHeight;
  const int metaW = kWidth - 2 - 2 * kBodyPadding - kImageSizeInfo - kBodyGap;
  const int bodyH = kBodyPadding + std::max(kImageSizeInfo, meta_.heightFor(metaW)) + kInfoBottomPad;
  return std::max(kHeight, 1 + kHeaderHeight + bodyH + 1);
}

// Layout
void BlockCard::resized() {
  auto inner = getLocalBounds().reduced(1);
  layoutHeader(inner.removeFromTop(kHeaderHeight));

  // The 275px body's last 2px hide under the card's bottom border.
  const auto bodyArea = body() == Body::info ? inner : inner.withHeight(kBodyHeight);
  body_.setBounds(bodyArea);
  const auto local = body_.getLocalBounds();
  if (eqEditor_) eqEditor_->setBounds(local);
  infoBusy_.setBounds(local);
  if (body() == Body::info)
    layoutInfoBody(local);
  else
    layoutToneBody(local);
}

void BlockCard::layoutHeader(juce::Rectangle<int> header) {
  const auto row = header.reduced(kBodyPadding, 0).withTrimmedBottom(1);  // the hairline is inside the 45
  const int cy = row.getCentreY();
  auto centreAt = [cy](juce::Component& c, int x) { c.setTopLeftPosition(x, cy - c.getHeight() / 2); };

  // Left cluster.
  int x = row.getX();
  centreAt(power_, x);
  x += power_.getWidth() + kHeaderGap;
  if (size_ && size_->isVisible()) {
    centreAt(*size_, x);
    x += size_->getWidth() + kHeaderGap;
  }
  if (calibration_->isVisible()) centreAt(*calibration_, x);

  // Right cluster, laid right to left; EQ stays rightmost in its pill so
  // opening grows left only.
  int right = row.getRight();
  for (auto* b : std::initializer_list<juce::Component*>{&remove_, &swap_, &share_, &info_, &reveal_}) {
    if (!b->isVisible()) continue;
    centreAt(*b, right - b->getWidth());
    right -= b->getWidth() + kHeaderGap;
  }
  if (showEq_) {
    // Pill: [power][PRE][view] EQ, padded 4/12, pulled back by its right
    // pad so EQ doesn't shift relative to info.
    const int contentW = eqPower_.getWidth() + kEqPillGap + preGroup_.getWidth() + kEqPillGap + eqView_->getWidth() +
                         kEqPillGap + eq_.getWidth();
    const int pillW = contentW + 2 * kEqPillPadX;
    const int pillH = std::max({eqPower_.getHeight(), eqView_->getHeight(), eq_.getHeight()}) + 2 * kEqPillPadY;
    eqPill_.setBounds(right + kEqPillPadX - pillW, cy - pillH / 2, pillW, pillH);
    int px = kEqPillPadX;
    const int pcy = pillH / 2;
    for (auto* c : std::initializer_list<juce::Component*>{&eqPower_, &preGroup_, eqView_.get(), &eq_}) {
      c->setTopLeftPosition(px, pcy - c->getHeight() / 2);
      px += c->getWidth() + kEqPillGap;
    }
  } else {
    eqPill_.setBounds(right - eq_.getWidth(), cy - eq_.getHeight() / 2, eq_.getWidth(), eq_.getHeight());
    eq_.setTopLeftPosition(0, 0);
  }
  eqPill_.repaint();
}

void BlockCard::layoutToneBody(juce::Rectangle<int> body) {
  const auto content = body.reduced(kBodyPadding);
  const int knobW = theme::kKnobSizeSecondary;
  const int bottom = content.getBottom();

  // In rail: meter centred in the space above the knob (gap 12).
  int x = content.getX();
  pinBottom(in_, x, knobW, bottom);
  auto meterSlot = juce::Rectangle<int>(x, content.getY(), knobW, in_.getY() - kRailGap - content.getY());
  inMeter_.setCentrePosition(meterSlot.getCentre());

  // Out rail (right-aligned): the meter stays over the Out knob whether or
  // not the normalize button widens the bottom row to its left.
  const bool normalize = normalizeWrap_.isVisible();
  const int outRailW = knobW + (normalize ? normalizeWrap_.getWidth() + kNormalizeGap : 0);
  const int outX = content.getRight() - outRailW;
  pinBottom(out_, content.getRight() - knobW, knobW, bottom);
  meterSlot = juce::Rectangle<int>(content.getRight() - knobW, content.getY(), knobW, out_.getY() - kRailGap - content.getY());
  outMeter_.setCentrePosition(meterSlot.getCentre());
  if (normalize) {
    // Centred on the knob face.
    normalizeWrap_.setTopLeftPosition(outX, bottom - knobW / 2 - normalizeWrap_.getHeight() / 2);
    normalize_.setTopLeftPosition(0, 0);
  }

  // Mix column.
  const int mixX = outX - kBodyGap - knobW;
  pinBottom(mix_, mixX, knobW, bottom);

  // Centre column: artwork + meta on top, the picker spanning the bottom.
  const int centreX = x + knobW + kBodyGap;
  const int centreW = mixX - kBodyGap - centreX;
  // Keep takes the end of the picker row.
  const int keepW = keep_.isVisible() ? keep_.getWidth() + kNormalizeGap +
                                            (keepMore_.isVisible() ? keepMore_.getWidth() + kKeepMoreGap : 0)
                                      : 0;
  selectWrap_.setBounds(centreX, bottom - kSelectHeight, centreW - keepW, kSelectHeight);
  select_.setBounds(selectWrap_.getLocalBounds());
  if (keep_.isVisible())
    keep_.setTopLeftPosition(selectWrap_.getRight() + kNormalizeGap,
                             selectWrap_.getY() + (kSelectHeight - keep_.getHeight()) / 2);
  if (keepMore_.isVisible()) keepMore_.setTopLeftPosition(keep_.getRight() + kKeepMoreGap, keep_.getY());
  const int linkY = keep_.getY() - kKeepLinkGap;
  if (original_.isVisible()) original_.setTopLeftPosition(keep_.getX(), linkY - original_.getHeight());
  if (kept_.isVisible()) kept_.setTopLeftPosition(keep_.getRight() - kept_.getWidth(), linkY - kept_.getHeight());
  // Refresh above those (or where they'd be).
  const int linksTop = original_.isVisible() ? original_.getY() - kKeepLinkGap
                       : kept_.isVisible()   ? kept_.getY() - kKeepLinkGap
                                             : linkY;
  if (refresh_.isVisible()) refresh_.setTopLeftPosition(keep_.getRight() - refresh_.getWidth(), linksTop - refresh_.getHeight());

  const int metaX = centreX + kImageSize + kBodyGap;
  const int metaW = centreW - kImageSize - kBodyGap;
  const int metaH = meta_.heightFor(metaW);
  const int rowH = std::max(kImageSize, metaH);
  imageFrame_.setBounds(centreX, content.getY() + (rowH - kImageSize) / 2, kImageSize, kImageSize);
  meta_.setBounds(metaX, content.getY() + (rowH - metaH) / 2, metaW, metaH);
  image_.setBounds(imageFrame_.getLocalBounds());
  picture_.setBounds(imageFrame_.getWidth() - kPictureButton - 8, imageFrame_.getHeight() - kPictureButton - 8,
                     kPictureButton, kPictureButton);
  loading_.setCentrePosition(imageFrame_.getLocalBounds().getCentre());
  retry_.setCentrePosition(imageFrame_.getLocalBounds().getCentre());
}

int BlockCard::layoutInfoBody(juce::Rectangle<int> body) {
  const auto content = body.reduced(kBodyPadding, 0).withTrimmedTop(kBodyPadding);
  imageFrame_.setBounds(content.getX(), content.getY(), kImageSizeInfo, kImageSizeInfo);
  image_.setBounds(imageFrame_.getLocalBounds());
  loading_.setCentrePosition(imageFrame_.getLocalBounds().getCentre());
  retry_.setCentrePosition(imageFrame_.getLocalBounds().getCentre());
  const int metaX = content.getX() + kImageSizeInfo + kBodyGap;
  const int metaW = content.getRight() - metaX;
  const int metaH = meta_.heightFor(metaW);
  meta_.setBounds(metaX, content.getY(), metaW, metaH);
  return kBodyPadding + std::max(kImageSizeInfo, metaH) + kInfoBottomPad;
}

void BlockCard::paint(juce::Graphics& g) {
  // Header hairline (inside the 45).
  g.setColour(theme::kBorder);
  g.fillRect(1, 1 + kHeaderHeight - 1, getWidth() - 2, 1);
  if (showEq_) {
    g.setColour(theme::kSegmentedTrack);
    g.fillRoundedRectangle(eqPill_.getBounds().toFloat(), eqPill_.getHeight() / 2.0f);
  }
}

void BlockCard::paintOverChildren(juce::Graphics& g) {
  // overflow: hidden on a rounded card: the EQ backdrop bleeds edge to edge,
  // so mask the corners back to the page and draw the border on top.
  const auto r = getLocalBounds().toFloat();
  juce::Path outside;
  outside.addRectangle(r);
  outside.addRoundedRectangle(r, kRadius);
  outside.setUsingNonZeroWinding(false);
  g.setColour(theme::kBlack);
  g.fillPath(outside);
  paint::border(g, r, kRadius, theme::kBorder);
  // A drag hovering: the tiles' file-drop outline.
  if (dropArmed_) paint::dashedBorder(g, r.reduced(1), kRadius, gallery::kFileDropBorder, 2.0f);
}

// Drops (see the header)
void BlockCard::setDropArmed(bool armed) {
  if (dropArmed_ == armed) return;
  dropArmed_ = armed;
  repaint();
}

bool BlockCard::isInterestedInDragSource(const SourceDetails& details) {
  const auto path = details.description.getProperty(LibraryStore::kDragKey, {}).toString();
  const auto* node = services_.library.tree().find(path);
  // Into this block: a capture, a tone, a captures folder. Not a preset (it
  // replaces the whole chain; the preset bar and the drawer load those).
  return node != nullptr && node->kind != LibraryNode::Kind::preset && (!node->isContainer() || node->loadsAsBlock());
}

void BlockCard::itemDropped(const SourceDetails& details) {
  setDropArmed(false);
  const auto path = details.description.getProperty(LibraryStore::kDragKey, {}).toString();
  // Posted: the swap re-syncs this card.
  juce::MessageManager::callAsync([self = juce::Component::SafePointer<BlockCard>(this), path] {
    if (self == nullptr) return;
    auto& library = self->services_.library;
    if (const auto* node = library.tree().find(path)) library.use(*node, self->block_.blockId);
  });
}

bool BlockCard::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& path : files) {
    const juce::File file(path);
    if (file.isDirectory() || file.hasFileExtension(".nam;.wav")) return true;
  }
  return false;
}

void BlockCard::filesDropped(const juce::StringArray& files, int, int) {
  setDropArmed(false);
  juce::MessageManager::callAsync([self = juce::Component::SafePointer<BlockCard>(this), files] {
    if (self != nullptr) self->services_.localFiles.drop(self->block_.blockId, files);
  });
}

// TONE3000
void BlockCard::fetchInfo(bool background) {
  if (!authenticated()) return;
  if (!background) {
    infoLoading_ = true;
    infoError_.clear();
    syncMeta();
    setSize(kWidth, preferredHeight());
  }
  const int toneId = siteToneId();
  if (toneId <= 0) return;
  services_.session.getTone(
      toneId, infoScope_.wrap([this, background](Result<Tone> result) {
        if (!background) infoLoading_ = false;
        if (result) {
          // A favorite toggle in flight owns the next metadata write.
          if (!favoriteBusy_) {
            infoTone_ = *result;
            // Best-effort: native no-ops when nothing changed server-side.
            // Not onto a kept copy: it is a file of its own.
            if (!isLocal()) services_.chain.refreshToneMetadata(juce::JSON::toString(infoTone_->raw, true));
          }
        } else if (background) {
          // Silent by design (offline, API down, tone deleted): the cached
          // tone keeps working; opening the panel refetches with its own UI.
          juce::Logger::writeToLog("Tone metadata sync skipped: " + result.error);
        } else {
          juce::Logger::writeToLog("Failed to load tone info: " + result.error);
          infoTone_.reset();
          infoError_ = "Failed to load tone details.";
        }
        syncMeta();
        setSize(kWidth, preferredHeight());
        resized();
      }));
}

void BlockCard::fetchModels() {
  if (isLocal() || !authenticated()) return;
  modelsLoading_ = true;
  syncModelSelect();
  services_.session.listToneModels(
      block_.tone.id, block_.tone.format,
      modelsScope_.wrap([this](Result<std::vector<Model>> result) {
        modelsLoading_ = false;
        if (result)
          models_ = *result;
        else
          // No error UI: the picker keeps the stored model and opening it retries.
          juce::Logger::writeToLog("Failed to load models: " + result.error);
        syncModelSelect();
      }));
}

void BlockCard::toggleFavorite() {
  if (!authenticated() || favoriteBusy_) return;
  const bool next = !favorited();
  const int nextCount = std::max(0, favoritesCount() + (next ? 1 : -1));
  favoriteOverride_ = FavoriteOverride{next, nextCount};
  favoriteBusy_ = true;
  syncMeta();

  const int toneId = siteToneId();
  if (toneId <= 0) return;
  auto finish = favoriteScope_.wrap([this, next, nextCount](Result<Tone> base) {
    favoriteBusy_ = false;
    if (base) {
      infoTone_ = base->withFavorite(next, nextCount);
      if (!isLocal()) services_.chain.refreshToneMetadata(juce::JSON::toString(infoTone_->raw, true));
    } else {
      juce::Logger::writeToLog("Failed to update favorite: " + base.error);
      favoriteOverride_.reset();
    }
    syncMeta();
  });
  services_.session.setToneFavorite(
      toneId, next, favoriteScope_.wrap([this, toneId, finish](const juce::String& error) {
        if (error.isNotEmpty()) {
          finish(Result<Tone>::fail(error));
          return;
        }
        if (infoTone_ && infoTone_->id == toneId)
          finish(Result<Tone>::ok(*infoTone_));
        else
          services_.session.getTone(toneId, finish);
      }));
}

void BlockCard::mouseDown(const juce::MouseEvent&) {
  // A click anywhere on the card gives it the keyboard (numbers, A/B), unless
  // what was clicked took it for itself (a text field, the model picker:
  // they run first, and a key they don't use still comes up to the card).
  if (auto* focused = getCurrentlyFocusedComponent(); focused != nullptr && isParentOf(focused)) return;
  grabKeyboardFocus();
}

bool BlockCard::blockKey(const juce::KeyPress& key) {
  if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown() || key.getModifiers().isCtrlDown())
    return false;
  const auto c = key.getTextCharacter();
  if (c == 'a' || c == 'A') {
    abSwitch();
    return true;
  }
  if (c >= '0' && c <= '9') return select_.keyPressed(key);  // what the picker does with them
  return false;
}

bool BlockCard::keyPressed(const juce::KeyPress& key) {
  if (blockKey(key)) return true;
  // Left / Right step, once the card itself has the keyboard (elsewhere they
  // scroll the screen).
  if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::rightKey)) return select_.keyPressed(key);
  return false;
}

void BlockCard::abSwitch() {
  const auto* before = services_.chain.previous(block_.blockId);
  if (before == nullptr) return;
  // Another model of the tone it plays: switched as the picker does.
  if (block_.tone.local && before->tone.local) {
    juce::String file;
    for (const auto& m : before->tone.models)
      if (m.id == before->activeModelId) file = m.sourcePath;
    for (const auto& m : block_.tone.models)
      if (file.isNotEmpty() && m.sourcePath == file) {
        if (m.id != block_.activeModelId) switchModel(juce::String(m.id));
        return;
      }
  } else if (!block_.tone.local && !before->tone.local && before->tone.id == block_.tone.id) {
    for (const auto& m : models_)
      if (m.id == before->activeModelId) {
        switchModel(juce::String(m.id));
        return;
      }
  }
  // Another folder's capture, another tone: loaded into this block again.
  services_.library.abSwitch(block_.blockId);
}

void BlockCard::switchModel(const juce::String& idText) {
  if (switchingModel_) return;
  const int newId = idText.getIntValue();
  if (newId == 0 || newId == block_.activeModelId) return;

  // Native only stores the active model, so the switch carries the model
  // object: from the fetched catalog, or the local tone's own list (whose
  // entries ship their stash model_url).
  std::optional<Model> model;
  if (isLocal()) {
    for (const auto& m : block_.tone.models) {
      if (m.id != newId) continue;
      Model local;
      local.id = m.id;
      local.name = m.name;
      local.modelUrl = m.modelUrl;
      auto* obj = new juce::DynamicObject();
      obj->setProperty("id", m.id);
      obj->setProperty("name", m.name);
      obj->setProperty("model_url", m.modelUrl);
      local.raw = juce::var(obj);
      model = local;
    }
  } else {
    for (const auto& m : models_)
      if (m.id == newId) model = m;
  }
  if (!model || model->modelUrl.isEmpty()) return;

  switchingModel_ = true;
  services_.modelLoads.switchModel(block_.blockId, *model,
                                   modelsScope_.wrap([this](bool) { switchingModel_ = false; }));
}

void BlockCard::share() {
  services_.backend.copyToClipboard(tonePageUrl());
  services_.toast.show("Link Copied");
}

}  // namespace t3k::ui
