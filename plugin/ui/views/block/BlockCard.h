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
#include "model/ChainState.h"
#include "model/Tone.h"
#include "services/ParamBinding.h"
#include "services/Services.h"
#include "widgets/BusyOverlay.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/ChromeTextButton.h"
#include "widgets/DimGroup.h"
#include "widgets/DotMeter.h"
#include "widgets/Knob.h"
#include "widgets/LoadingDots.h"
#include "widgets/ModelSelect.h"
#include "widgets/RetryLoadBadge.h"
#include "widgets/SegmentedText.h"
#include "widgets/ToneImage.h"

namespace t3k::ui {

class BlockCard : public juce::Component, private ToneSession::Listener, private UiPrefs::Listener {
public:
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
  void resized() override;

private:
  enum class Body { tone, eq, info };
  class Indicator;

  // ToneSession::Listener / UiPrefs::Listener
  void sessionChanged() override;
  void prefChanged(const juce::String& key) override;

  void buildHeader();
  void buildBody();
  void layoutHeader(juce::Rectangle<int> header);
  void layoutToneBody(juce::Rectangle<int> body);
  int layoutInfoBody(juce::Rectangle<int> body);
  Body body() const { return showEq_ ? Body::eq : showInfo_ ? Body::info : Body::tone; }
  void syncFromBlock();
  void syncHeader();
  void syncMeta();
  void syncModelSelect();
  void setBodyView();
  void setShowEq(bool show);
  void setShowInfo(bool show);

  bool isNam() const { return block_.tone.isNam(); }
  bool isLocal() const { return block_.tone.local; }
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
  void share();

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
  ChromeIconButton swap_{Icon::ArrowLeftRight, ChromeIconButton::Tone::plain, help::Key::swapTone};
  ChromeIconButton remove_{Icon::Trash2, ChromeIconButton::Tone::plain, help::Key::removeBlock};

  // Body (tone / info views live in `body_`, which dims while bypassed)
  DimGroup body_;
  LiveDotMeter inMeter_, outMeter_;
  Knob in_, out_, mix_;
  juce::Component normalizeWrap_;
  ChromeIconButton normalize_{Icon::Equal, ChromeIconButton::Tone::power, help::Key::blockNormalize};
  juce::Component imageFrame_;
  ToneImage image_;
  LoadingDots loading_;
  RetryLoadBadge retry_;
  ToneMeta meta_;
  juce::Component selectWrap_;
  ModelSelect select_;
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
};

}  // namespace t3k::ui
