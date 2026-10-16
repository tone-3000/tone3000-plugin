// The bordered detail card (port of ChainBlock.tsx minus the ← BLOCK row):
// a 45px chrome header (power, size/calibration, EQ menu, info, share, swap,
// trash) over a body that shows one of three views:
//   tone:  In rail | artwork + ToneMeta over the model picker | Mix | Out rail
//   eq:    BlockEqView, edge to edge
//   info:  smaller artwork + ToneMeta with BlockInfoPanel; grows past the
//          fixed height and the owner scrolls it.
// Controls hold optimistic values and native converges via chain resyncs
// (setBlock). Catalog metadata (info panel, favorite, model list) is fetched
// from TONE3000 through the session and never written into chain state;
// every reply is scoped so a swap mid-flight can't surface the old tone.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BlockEqView.h"
#include "ToneMeta.h"
#include "core/AsyncScope.h"
#include "core/BusyGrace.h"
#include "model/ChainState.h"
#include "model/Tone.h"
#include "services/ParamBinding.h"
#include "services/Services.h"
#include "widgets/BusyOverlay.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/ChromeTextButton.h"
#include "widgets/ContextMenu.h"
#include "widgets/DimGroup.h"
#include "widgets/DotMeter.h"
#include "widgets/Knob.h"
#include "widgets/LoadingDots.h"
#include "widgets/ModelSelect.h"
#include "widgets/RetryLoadBadge.h"
#include "widgets/SegmentedText.h"
#include "widgets/ToneImage.h"

namespace t3k::ui {

// Drops land on the open card too, the way they land on its tile: a
// Library capture (with its folder's captures beside it), folder or tone, or
// .nam / .wav files from the OS, swapped into this block in place, so a
// capture can be tried without leaving the card.
class BlockCard : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::FileDragAndDropTarget,
                  private ToneSession::Listener,
                  private UiPrefs::Listener,
                  private LibraryStore::KeepListener {
public:
  // A number (its model) or "a" (A/B) typed anywhere in the plugin with
  // this card open and nothing else taking it (PluginRoot passes it on).
  // True when it was one of those.
  bool blockKey(const juce::KeyPress& key);
  // chainLayout.tsx: 16px-radius card, 45px chrome header, 275px padded
  // body (the last 2px hide under the border, so 273 show).
  static constexpr int kWidth = 800;
  static constexpr int kHeaderHeight = 45;
  static constexpr int kBodyHeight = 275;
  static constexpr int kBodyPadding = 16;
  static constexpr int kHeight = kHeaderHeight + kBodyHeight;
  static constexpr int kRadius = 16;

  BlockCard(Services& services, const ChainItem& block, bool namDownstream);
  ~BlockCard() override;

  // A chain resync: same block, fresher fields.
  void setBlock(const ChainItem& block, bool namDownstream);
  const std::string& blockId() const { return block_.blockId; }

  // Info view open/closed: the owner drops the meter-band pads so the card
  // can scroll to the faceplate, and lets the card grow.
  std::function<void(bool)> onInfoVisible;
  // The header's ⇄: launch the Select flow to swap this block's tone.
  std::function<void()> onSwap;
  // The card's height in the current view (kHeight, or taller in info).
  int preferredHeight();

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override;

  // Library rows (LibraryDrawer): items and captures folders.
  bool isInterestedInDragSource(const SourceDetails& details) override;
  void itemDragEnter(const SourceDetails&) override { setDropArmed(true); }
  void itemDragExit(const SourceDetails&) override { setDropArmed(false); }
  void itemDropped(const SourceDetails& details) override;
  // OS files: the tile's file drop.
  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void fileDragEnter(const juce::StringArray&, int, int) override { setDropArmed(true); }
  void fileDragExit(const juce::StringArray&) override { setDropArmed(false); }
  void filesDropped(const juce::StringArray& files, int, int) override;
  void resized() override;

private:
  enum class Body { tone, eq, info };
  class Indicator;

  // ToneSession::Listener / UiPrefs::Listener
  void sessionChanged() override;
  void prefChanged(const juce::String& key) override;
  void keepChanged() override;

  void buildHeader();
  void buildBody();
  void layoutHeader(juce::Rectangle<int> header);
  void layoutToneBody(juce::Rectangle<int> body);
  int layoutInfoBody(juce::Rectangle<int> body);
  Body body() const { return showEq_ ? Body::eq : showInfo_ ? Body::info : Body::tone; }
  void syncFromBlock();
  void syncBusy();
  void syncPicture();
  void syncHeader();
  void syncMeta();
  void syncModelSelect();
  void setBodyView();
  void setShowEq(bool show);
  void setShowInfo(bool show);

  bool isNam() const { return block_.tone.isNam(); }
  bool isLocal() const { return block_.tone.local; }
  // The TONE3000 tone the card stands for: the block's, or for a capture kept
  // from one, that one (its info, stats, share, KEEP's menu); 0 for none.
  int siteToneId() const { return isLocal() ? keptToneId_ : block_.tone.id; }
  int keptToneId_ = 0;
  int keptToneIdNow();
  // block_.tone as the card shows it: a kept copy with its tone's stats.
  ToneSummary shownTone() const;
  bool authenticated() const { return services_.session.authenticated(); }
  bool modelBusy() const { return block_.modelLoading || (!block_.loaded && !block_.loadFailed); }
  bool normalizeOverridden() const;
  bool favorited() const;
  int favoritesCount() const;
  juce::String tonePageUrl() const;

  // TONE3000 fetches.
  void fetchInfo(bool background);
  void fetchModels();
  void toggleFavorite();
  void switchModel(const juce::String& id);
  // Once the card is clicked, the keyboard: a number picks that model (the
  // picker's number entry), Left / Right step, "a" goes back to the model
  // played before and again forth (A/B).
  bool keyPressed(const juce::KeyPress& key) override;
  void mouseDown(const juce::MouseEvent&) override;
  void abSwitch();
  std::string keyboardFor_;  // the block the card last took the keyboard for (on opening)
  void share();

  void setDropArmed(bool armed);
  bool dropArmed_ = false;

  Services& services_;
  ChainItem block_;
  bool namDownstream_ = false;
  ParamBinding calibrateInput_;

  // Header
  ChromeIconButton power_{Icon::Power, ChromeIconButton::Tone::power, help::Key::blockPower};
  std::unique_ptr<SegmentedText> size_;
  int sizeSignature_ = -1;
  std::unique_ptr<Indicator> calibration_;
  juce::Component eqPill_;
  ChromeIconButton eqPower_{Icon::Power, ChromeIconButton::Tone::power, help::Key::eqPower};
  DimGroup preGroup_;
  ChromeTextButton pre_{"PRE", help::Key::eqPre};
  std::unique_ptr<SegmentedText> eqView_;
  ChromeTextButton eq_{"EQ", help::Key::eqToggle};
  ChromeIconButton info_{Icon::Info, ChromeIconButton::Tone::plain, help::Key::toneInfo};
  ChromeIconButton share_{Icon::Share, ChromeIconButton::Tone::plain, help::Key::shareTone};
  // Show in Library: a block playing a Library file.
  ChromeIconButton reveal_{Icon::LibraryBig, ChromeIconButton::Tone::plain, help::Key::libraryShowBlock};
  ChromeIconButton swap_{Icon::ArrowLeftRight, ChromeIconButton::Tone::plain, help::Key::swapTone};
  ChromeIconButton remove_{Icon::Trash2, ChromeIconButton::Tone::plain, help::Key::removeBlock};

  // Body (tone / info views live in `body_`, which dims while bypassed)
  DimGroup body_;
  LiveDotMeter inMeter_, outMeter_;
  Knob in_, out_, mix_;
  juce::Component normalizeWrap_;
  // Labelled, not a glyph: a bare "=" read as decoration and went unfound.
  ChromeTextButton normalize_{"NORM", help::Key::blockNormalize};
  juce::Component imageFrame_;
  ToneImage image_;
  LoadingDots loading_;
  RetryLoadBadge retry_;
  ToneMeta meta_;
  juce::Component selectWrap_;
  ModelSelect select_;
  // Keep, beside the picker where models are auditioned: copies the playing
  // model into the keep folder (lit while one is set), or without one
  // asks for a Library folder (the tile menu's Add to Library).
  ChromeTextButton keep_{"KEEP", help::Key::libraryKeep};
  // A TONE3000 tone's other ways to keep (the capture, the whole tone).
  ChromeIconButton keepMore_{Icon::ChevronDown, ChromeIconButton::Tone::plain, help::Key::libraryKeepMore};
  std::unique_ptr<ContextMenu> keepMenu_;
  void openKeepMenu();
  // On the image of a block from a local file: set (change, remove) the
  // picture of the folder it came from (LibraryStore's folder pictures).
  class PictureButton : public Clickable {
  public:
    PictureButton();
    void paintButton(juce::Graphics& g, bool over, bool down) override;
  };
  PictureButton picture_;
  std::unique_ptr<ContextMenu> pictureMenu_;
  void pictureClicked();
  // Above Keep (the picker row stays as wide): a kept capture's way back to
  // where it was kept from (SOURCE), or an original's way to the copies kept
  // of it (KEPT; one: straight there, more: a menu of their folders).
  ChromeTextButton original_{"SOURCE", help::Key::libraryOriginal};
  ChromeTextButton kept_{"KEPT", help::Key::libraryKept};
  // Above Source / Kept: the block's folder has captures it doesn't list
  // (kept or copied in since it loaded): it reads the folder again.
  ChromeTextButton refresh_{"REFRESH", help::Key::libraryRefreshBlock};
  std::unique_ptr<ContextMenu> keptMenu_;
  void syncKeepLinks();
  void openKeptMenu();
  BusyOverlay infoBusy_{BusyOverlay::Align::centre};
  std::unique_ptr<BlockEqView> eqEditor_;

  // Optimistic UI state
  bool enabled_ = true, normalizeOn_ = true, slimFull_ = false;
  bool eqOn_ = true, eqPre_ = false;
  bool showEq_ = false, showInfo_ = false;
  BlockEqView::View eqViewMode_ = BlockEqView::View::sliders;
  bool switchingModel_ = false;

  // Catalog state
  std::optional<Tone> infoTone_;
  bool infoLoading_ = false;
  juce::String infoError_;
  struct FavoriteOverride {
    bool on;
    int count;
  };
  std::optional<FavoriteOverride> favoriteOverride_;
  bool favoriteBusy_ = false;
  std::vector<Model> models_;
  bool modelsLoading_ = false;
  AsyncScope infoScope_, modelsScope_, favoriteScope_;
  BusyGrace busyGrace_;
};

}  // namespace t3k::ui
