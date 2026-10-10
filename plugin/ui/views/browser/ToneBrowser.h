// In-plugin tone browser: the Select tone takeover that covers everything
// under the header (meters, chain and faceplate). A pinned header (← SELECT
// TONE, the search box, the filter bar) and a paginator pinned at the
// bottom, with the card grid (or loading dots / empty copy / an error with
// Try again) scrolling between them, fading out under both. The query,
// filter row and last page live in the BrowserState so the screen comes
// back as it was left.
//
// The ← SELECT TONE row zooms with the window like everything else; the
// body under it does not. It is counter-scaled by the window zoom and laid
// out in screen pixels, so a bigger window shows more rather than bigger:
// the search box and filter row keep their 1x height and widen with the
// column, the cards keep their 1x height, widen to fill two columns, and go
// three-up once three fit at kMinCardWidth.
//
// Searching needs a TONE3000 session. Signed out, the screen is a preview
// instead: the gear chips alone over TONE3000's trending feed (GET
// /tones/trending, which takes no token), and under the cards a sign-in
// call to action. A card leads to a sign-in page of its own (the ← alone,
// the prompt centred); either CTA starts the browse-intent login, which
// comes straight back here signed in. Signed in, every query goes to the
// TONE3000 API through the session; native is only involved for the final
// load (selectTone), which the parent completes by closing the browser.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "BrowserPrompt.h"
#include "FilterBar.h"
#include "Paginator.h"
#include "ToneCard.h"
#include "core/AsyncScope.h"
#include "core/Design.h"
#include "services/Services.h"
#include "widgets/BackLink.h"
#include "widgets/BusyOverlay.h"
#include "widgets/DragScroller.h"
#include "widgets/LoadingDots.h"
#include "widgets/PillButton.h"
#include "widgets/TextField.h"

namespace t3k::ui {

class ToneBrowser : public juce::Component, private ToneSession::Listener, private Zoom::Listener {
public:
  // Plugin.tsx's shared 24px pad under the header (design px); half that
  // closes the screen under the paginator (1x body, screen px).
  static constexpr int kPadTop = 24, kPadBottom = 12;
  // The column's side margin: the filter row bleeds this far past the
  // column for its edge fades, so the chips fade out right at the window's
  // edge.
  static constexpr int kPadX = FilterBar::kBleed;
  static constexpr int kColumnWidth = design::kWidth - 2 * kPadX;
  static constexpr int kGridGap = 16;
  // The grid goes three-up once each card can be this wide (screen px):
  // 340 leaves the text column 188px beside the 112px image, room for a
  // wrapped title and the stats row. At the 960 column that is a window
  // zoom of about 1.1.
  static constexpr int kMinCardWidth = 340;
  static constexpr int kPageSize = 12;
  static constexpr int kSearchHeight = 40;

  explicit ToneBrowser(Services& services);
  ~ToneBrowser() override;

  // ← back to the chain.
  std::function<void()> onClose;
  // The preview's sign-in CTAs: the browse-intent login that comes back here.
  std::function<void()> onSignIn;

  // The results list: the screen's main scroller.
  DragScroller& scroller() { return *scroller_; }

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Body;
  class Content;
  static constexpr int kHeaderGap = 16;  // header row → search → filters
  // Filters → first row, and last row → paginator (or the bottom).
  static constexpr int kContentPadTop = 24, kContentPadBottom = 24;
  static constexpr int kPickErrorGap = 16;
  static constexpr int kDotsPadY = 64;
  static constexpr int kEmptyPadY = 64, kEmptyPadX = 24;
  static constexpr int kCopyMaxWidth = 420, kErrorMaxWidth = 340;

  void sessionChanged() override;
  void authFlowChanged() override;
  void zoomChanged() override { resized(); }

  bool signedOut() const { return !services_.session.authenticated(); }
  // The signed-out screen: the trending preview, or its sign-in page.
  bool preview() const { return signedOut() && !authPending(); }
  float zoom() const { return static_cast<float>(services_.zoom.factor()); }
  // Pre-mounted while an OAuth return still resolves its code exchange.
  bool authPending() const { return services_.session.authPending(); }
  // The search box's text becomes the query (Enter, ×, Escape).
  void submit();
  void queryChanged();
  void setPage(int page);
  // The search page for the query (signed in), or the trending feed for
  // its gear (signed out).
  void fetch();
  void fetchTrending();
  void pageLoaded(TonePage page);
  void pageFailed();
  void pick(const Tone& tone);
  // The preview's card click: the sign-in page, and ← back to the cards.
  void setSignInPageShown(bool shown);
  void rebuildCards();
  void rebuildBody();
  void layoutBody();
  void layoutContent();
  void paintScrollFades(juce::Graphics& g);
  const char* emptyCopy() const;

  Services& services_;
  BrowserState& state_;
  AsyncScope scope_;       // the component's lifetime (picks)
  AsyncScope fetchScope_;  // the current page request

  // This visit's state; the rest is in state_.
  bool loading_ = true;
  bool error_ = false;
  bool signInPage_ = false;  // the preview's card-click page
  std::optional<int> pickingId_;
  juce::String pickError_;

  BackLink back_;         // ← SELECT TONE: closes the browser
  BackLink signInBack_;   // the sign-in page's bare ←: back to the cards

  // The 1x body: pinned search box and filter row, the scrolled column, the
  // paginator pinned under it.
  std::unique_ptr<Body> body_;
  TextField search_;
  FilterBar filters_;
  std::unique_ptr<DragScroller> scroller_;
  std::unique_ptr<Content> content_;
  std::vector<std::unique_ptr<ToneCard>> cards_;
  std::unique_ptr<BusyOverlay> gridBusy_;
  std::unique_ptr<BrowserPrompt> bodyPrompt_;    // the sign-in page, or the fetch error
  std::unique_ptr<BrowserPrompt> footerPrompt_;  // the preview's CTA under the cards
  LoadingDots dots_;
  Paginator paginator_;
};

}  // namespace t3k::ui
