#include "ToneBrowser.h"

#include <algorithm>
#include <cmath>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// The preview's copy (Select Tone mockups, signed out): the CTA under the
// trending cards, and the page a card click lands on.
constexpr const char* kTrendingFooterCopy = "Discover a zillion more tones.";
constexpr const char* kSignInHeading = "Sign in to see your tones and discover a zillion new ones.";
constexpr const char* kSignInLabel = "Sign in or create free account";
constexpr const char* kFetchError = "Failed to load tones from TONE3000.";
constexpr const char* kPickError = "Failed to load that tone. Please try again.";
constexpr float kPickErrorPx = 12;
constexpr float kEmptyPx = 13;
constexpr float kSearchPx = 14;
constexpr int kSearchIcon = 18;
constexpr int kSearchPadX = 16;
constexpr int kSearchIconGap = 10;
// Cards slide under the filter row and the paginator through this much
// black: a solid band right at the edge, then a fade that starts steep
// (alpha (1-t)^1.5) so the cards read as gone before they reach it.
constexpr int kScrollFade = 32;
constexpr float kFadeSolid = 0.2f;  // 6px
constexpr float kFadePower = 1.5f;

// One fade band, solid at its top (or bottom) edge running to clear at the
// other.
void fillScrollFade(juce::Graphics& g, juce::Rectangle<float> band, bool solidAtTop) {
  const float edge = solidAtTop ? band.getY() : band.getBottom();
  const float far = solidAtTop ? band.getBottom() : band.getY();
  auto fade = juce::ColourGradient::vertical(juce::Colours::black, edge, juce::Colours::transparentBlack, far);
  fade.addColour(kFadeSolid, juce::Colours::black);
  for (const float t : {0.25f, 0.5f, 0.75f})
    fade.addColour(kFadeSolid + t * (1 - kFadeSolid), juce::Colours::black.withAlpha(std::pow(1 - t, kFadePower)));
  g.setGradientFill(fade);
  g.fillRect(band);
}

std::unique_ptr<PillButton> makeFilledButton(const juce::String& label) {
  return std::make_unique<PillButton>(label, PillButton::Style::filled);
}
}  // namespace

// Everything under the ← row, at 1x whatever the window zoom.
class ToneBrowser::Body : public juce::Component {
public:
  explicit Body(ToneBrowser& owner) : owner_(owner) {}
  void paintOverChildren(juce::Graphics& g) override { owner_.paintScrollFades(g); }

private:
  ToneBrowser& owner_;
};

// The scrolled column: hosts the cards, prompts and paginator, and paints
// the two bare text rows (pick error, empty copy) itself.
class ToneBrowser::Content : public juce::Component {
public:
  juce::String pickError, emptyCopy;
  juce::Rectangle<int> pickErrorBox, emptyBox;

  void paint(juce::Graphics& g) override {
    if (pickError.isNotEmpty()) paint::text(g, pickError, pickErrorBox, Fonts::sans(kPickErrorPx), theme::kBrandRed);
    if (emptyCopy.isNotEmpty())
      paint::text(g, emptyCopy, emptyBox, Fonts::sans(kEmptyPx), theme::kMuted, juce::Justification::centred);
  }
};

ToneBrowser::ToneBrowser(Services& services)
    : services_(services),
      state_(services.browser),
      back_("Select Tone", help::Key::closeToneBrowser),
      signInBack_({}, help::Key::browserBackToTrending),
      body_(std::make_unique<Body>(*this)),
      filters_(services, services.browser),
      scroller_(std::make_unique<DragScroller>(DragScroller::Axis::vertical)),
      content_(std::make_unique<Content>()) {
  setOpaque(true);  // covers the meters, chain and faceplate outright
  back_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(back_);
  signInBack_.onClick = [this] { setSignInPageShown(false); };
  addChildComponent(signInBack_);
  addAndMakeVisible(*body_);

  search_.setPlaceholder(juce::String::fromUTF8("Search\xe2\x80\xa6"));
  search_.setFontSize(kSearchPx);
  search_.setCornerRadius(kSearchHeight / 2.0f);
  search_.setPadding(0, kSearchPadX + kSearchIcon + kSearchIconGap, kSearchPadX + kSearchIcon + kSearchIconGap);
  search_.setLeadingIcon(Icon::Search, kSearchIcon, kSearchPadX, theme::kGray);
  search_.setClearButton(kSearchIcon, kSearchPadX);
  search_.setText(state_.query.text);
  search_.onEnter = [this] { submit(); };
  search_.onClear = [this] { submit(); };
  search_.onEscape = [this] {
    search_.setText({});
    submit();
  };
  body_->addChildComponent(search_);

  filters_.onChange = [this] { queryChanged(); };
  body_->addChildComponent(filters_);

  scroller_->setViewedComponent(content_.get(), false);
  scroller_->setFocusMargin(kGridGap);  // a focused card clears its neighbours' gap
  body_->addAndMakeVisible(*scroller_);
  content_->addChildComponent(dots_);
  paginator_.onPageChange = [this](int page) { setPage(page); };
  body_->addChildComponent(paginator_);

  services_.session.addListener(this);
  services_.zoom.addListener(this);
  // Back to the page this screen was left on, so long as it is the same
  // kind of page (a signed-in search page, or the signed-out trending feed);
  // a fresh visit, or one across a sign-in / out, fetches.
  if (state_.result && state_.resultIsTrending == signedOut() && !authPending()) {
    loading_ = false;
    rebuildCards();
    rebuildBody();
  } else {
    fetch();
  }
}

ToneBrowser::~ToneBrowser() {
  services_.zoom.removeListener(this);
  services_.session.removeListener(this);
}

// State
void ToneBrowser::sessionChanged() {
  // Signed in from the sign-in page: it is done with; signed out: the
  // preview starts on its cards.
  signInPage_ = false;
  fetch();
}

void ToneBrowser::authFlowChanged() {
  // Held back during an OAuth return; the fetch goes out once it clears.
  if (!authPending() && loading_ && !state_.result) fetch();
}

void ToneBrowser::submit() {
  state_.query.text = search_.text();
  filters_.refresh();  // the default sort follows the text
  queryChanged();
}

void ToneBrowser::queryChanged() {
  state_.page = 1;
  fetch();
}

void ToneBrowser::setPage(int page) {
  if (page == state_.page) return;
  state_.page = page;
  fetch();
}

void ToneBrowser::fetch() {
  // Pre-mounted during an OAuth return: the token exchange hasn't finished,
  // so we don't yet know whether to render the gate or fetch. Keep the
  // loading state; authFlowChanged reruns this once it clears.
  if (authPending()) return;

  // Signed out: the trending preview (a search would only fail with
  // not_authenticated and trip the client's re-auth callback).
  if (signedOut()) {
    fetchTrending();
    return;
  }

  fetchScope_.reset();  // a newer request supersedes anything in flight
  loading_ = true;
  error_ = false;
  rebuildBody();
  services_.session.searchTones(state_.query, state_.page, kPageSize, fetchScope_.wrap([this](ui::Result<TonePage> r) {
    if (r) {
      state_.resultIsTrending = false;
      pageLoaded(std::move(*r.value));
    } else {
      pageFailed();
    }
  }));
}

// The trending feed is one unpaginated page (the API's top 10 for the
// gear), so it lands as a TonePage and the grid needs to know nothing.
void ToneBrowser::fetchTrending() {
  fetchScope_.reset();
  loading_ = true;
  error_ = false;
  rebuildBody();
  services_.session.listTrending(state_.query.gear, fetchScope_.wrap([this](ui::Result<std::vector<Tone>> r) {
    if (!r) return pageFailed();
    TonePage page;
    page.data = std::move(*r.value);
    state_.resultIsTrending = true;
    pageLoaded(std::move(page));
  }));
}

void ToneBrowser::pageLoaded(TonePage page) {
  state_.result = std::move(page);
  loading_ = false;
  rebuildCards();
  rebuildBody();
  // Jump to the top whenever fresh results land (page turn / filter).
  scroller_->setViewPosition(0, 0);
  const auto& result = *state_.result;
  if (result.data.empty()) help::announce(emptyCopy());
  else if (state_.resultIsTrending) help::announce(juce::String(result.data.size()) + " trending tones");
  else
    help::announce(juce::String(result.data.size()) + " tones, page " + juce::String(result.page) + " of " +
                   juce::String(result.totalPages));
}

void ToneBrowser::pageFailed() {
  error_ = true;
  loading_ = false;
  rebuildBody();
  help::announce(kFetchError);
}

void ToneBrowser::pick(const Tone& tone) {
  if (pickingId_) return;
  pickError_.clear();
  pickingId_ = tone.id;
  rebuildBody();
  // On success the parent closes the browser; the scope guards the reply.
  services_.session.selectTone(tone.id, scope_.wrap([this](const juce::String& error) {
    if (error.isEmpty()) return;
    pickError_ = kPickError;
    pickingId_.reset();
    rebuildBody();
  }));
}

void ToneBrowser::setSignInPageShown(bool shown) {
  if (shown == signInPage_) return;
  signInPage_ = shown;
  rebuildBody();
  scroller_->setViewPosition(0, 0);
  if (shown) help::announce(kSignInHeading);
}

const char* ToneBrowser::emptyCopy() const {
  if (state_.resultIsTrending) return "No trending tones of this type right now. Try another.";
  if (state_.query.text.isNotEmpty()) return "No tones match. Try a different search or fewer filters.";
  switch (state_.query.profile) {
    case Profile::none: return "No tones match. Try fewer filters.";
    case Profile::downloaded: return "Tones you download on TONE3000 will show up here.";
    case Profile::favorited: return "Tones you favorite on TONE3000 will show up here.";
    case Profile::created: return "Tones you upload to TONE3000 will show up here.";
  }
  return "";
}

// Children
void ToneBrowser::rebuildCards() {
  cards_.clear();
  if (!state_.result) return;
  for (const auto& tone : state_.result->data) {
    auto card = std::make_unique<ToneCard>(services_.images, tone);
    card->onClick = [this, id = tone.id] {
      // Loading a tone needs the session: signed out, a card is the way to
      // the sign-in page.
      if (preview()) {
        setSignInPageShown(true);
        return;
      }
      const auto it = std::find_if(cards_.begin(), cards_.end(), [id](const auto& c) { return c->tone().id == id; });
      if (it != cards_.end()) pick((*it)->tone());
    };
    content_->addAndMakeVisible(*card);
    cards_.push_back(std::move(card));
  }
}

void ToneBrowser::rebuildBody() {
  const bool previewing = preview();
  if (!previewing) signInPage_ = false;  // the page belongs to the preview
  const bool signInPage = signInPage_;
  const bool showError = error_ && !loading_;
  const bool hasCards = state_.result && !state_.result->data.empty();

  // The header row: ← SELECT TONE closes the browser; the sign-in page has
  // the bare ← back to the cards instead.
  back_.setVisible(!signInPage);
  signInBack_.setVisible(signInPage);

  // The search box exists only for a session; signed out the filter row is
  // the gear chips alone (the trending feed's one filter). A profile
  // filter's stream searches titles alone.
  search_.setVisible(!previewing);
  filters_.setGearOnly(previewing);
  filters_.setVisible(!signInPage);
  search_.setHelpText(help::text(filters_.profileLocked() ? help::Key::browserSearchProfile : help::Key::browserSearch));

  // Body prompt: the sign-in page, or the fetch error with Try again.
  bodyPrompt_.reset();
  if (signInPage) {
    bodyPrompt_ = std::make_unique<BrowserPrompt>(true, kSignInHeading, kCopyMaxWidth, makeFilledButton(kSignInLabel));
    bodyPrompt_->button().onClick = [this] {
      if (onSignIn) onSignIn();
    };
  } else if (showError) {
    bodyPrompt_ = std::make_unique<BrowserPrompt>(false, kFetchError, kErrorMaxWidth, makeFilledButton("Try again"));
    bodyPrompt_->button().onClick = [this] { fetch(); };
  }
  if (bodyPrompt_) content_->addAndMakeVisible(*bodyPrompt_);

  // The preview's CTA closes the list once the feed has answered (it stays
  // put under the dimmed cards while another gear loads).
  footerPrompt_.reset();
  if (previewing && !signInPage && !showError && (hasCards || !loading_)) {
    footerPrompt_ =
        std::make_unique<BrowserPrompt>(true, kTrendingFooterCopy, kCopyMaxWidth, makeFilledButton(kSignInLabel));
    footerPrompt_->button().onClick = [this] {
      if (onSignIn) onSignIn();
    };
    content_->addAndMakeVisible(*footerPrompt_);
  }

  // First load (nothing to dim yet): dots alone. Nothing found: the copy.
  dots_.setVisible(!signInPage && !showError && !hasCards && loading_);
  content_->emptyCopy = !signInPage && !showError && !hasCards && !loading_ ? emptyCopy() : juce::String();

  // Cards stay mounted while a new page loads, dimmed and inert under the
  // busy overlay. Other cards dim while one pick resolves.
  const bool cardsVisible = !signInPage && !showError && hasCards;
  for (auto& card : cards_) {
    card->setVisible(cardsVisible);
    const bool picking = pickingId_ && *pickingId_ == card->tone().id;
    card->setLoading(picking);
    card->setDisabled(ToneCard::unavailable(card->tone()) || pickingId_.has_value() || loading_);
  }
  if (cardsVisible && loading_) {
    if (!gridBusy_) {
      gridBusy_ = std::make_unique<BusyOverlay>(BusyOverlay::Align::top);
      content_->addAndMakeVisible(*gridBusy_);
    }
  } else {
    gridBusy_.reset();
  }

  const bool paginate = !signInPage && !error_ && state_.result && state_.result->totalPages > 1;
  paginator_.setVisible(paginate);
  if (paginate) {
    paginator_.set(state_.page, state_.result->totalPages);
    paginator_.setAlpha(loading_ ? theme::kDisabledOpacity : 1.0f);
    paginator_.setInterceptsMouseClicks(!loading_, false);
  }

  content_->pickError = pickError_;
  resized();
}

// Layout
void ToneBrowser::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void ToneBrowser::resized() {
  // Design space: the ← row, and the box the body fills under it.
  const int w = getWidth();
  const int colW = std::min(kColumnWidth, w);
  back_.setTopLeftPosition((w - colW) / 2, kPadTop);
  signInBack_.setTopLeftPosition((w - colW) / 2, kPadTop);
  const int top = kPadTop + BackLink::kHeight;
  const int h = std::max(0, getHeight() - top);

  // The body holds 1x on screen: counter-scaled by the zoom and sized in
  // screen pixels over that box.
  const float z = zoom();
  body_->setTransform(juce::AffineTransform::scale(1 / z).translated(0, static_cast<float>(top)));
  body_->setBounds(0, 0, juce::roundToInt(w * z), juce::roundToInt(h * z));
  layoutBody();
}

// Screen pixels from here down. The column under the ← row is kColumnWidth
// design px wide: that times the zoom.
void ToneBrowser::layoutBody() {
  const int w = body_->getWidth(), h = body_->getHeight();
  const int colW = std::min(juce::roundToInt(kColumnWidth * zoom()), w);
  const int colX = (w - colW) / 2;
  int y = 0;
  if (search_.isVisible()) {
    y += kHeaderGap;
    search_.setBounds(colX, y, colW, kSearchHeight);
    y += kSearchHeight;
  }
  if (filters_.isVisible()) {
    y += kHeaderGap;
    filters_.setColumn({colX, y, colW, FilterBar::kHeight});
    y += FilterBar::kHeight;
  }
  // The paginator is pinned at the bottom, right-aligned to the column; the
  // results scroll between it and the filter row.
  int bottom = h;
  if (paginator_.isVisible()) {
    bottom = h - kPadBottom - Paginator::kHeight;
    paginator_.setTopLeftPosition(colX + colW - paginator_.getWidth(), bottom);
  }
  scroller_->setBounds(0, y, w, std::max(0, bottom - y));
  layoutContent();
}

// Scrolled cards fade out under the filter row and the paginator instead
// of clipping at them; each band shows only while there is more that way.
void ToneBrowser::paintScrollFades(juce::Graphics& g) {
  const auto view = scroller_->getBounds().toFloat();
  const int viewY = scroller_->getViewPositionY();
  if (viewY > 0) fillScrollFade(g, view.withHeight(kScrollFade), /*solidAtTop=*/true);
  if (viewY + scroller_->getViewHeight() < content_->getHeight())
    fillScrollFade(g, view.withTop(view.getBottom() - kScrollFade), /*solidAtTop=*/false);
}

void ToneBrowser::layoutContent() {
  const int w = scroller_->getWidth();
  if (w <= 0) return;
  const int colW = std::min(juce::roundToInt(kColumnWidth * zoom()), w);
  const int colX = (w - colW) / 2;
  int y = 0;

  if (pickError_.isNotEmpty()) {
    y += kPickErrorGap;
    const int line = Fonts::normalLineHeight(kPickErrorPx);
    content_->pickErrorBox = {colX, y, colW, line};
    y += line;
  }

  // Tone grid / empty state / prompt.
  y += kContentPadTop;
  if (bodyPrompt_) {
    // The sign-in page's prompt sits in the middle of the body (the
    // mockup centres it); the error prompt tops the column as the cards do.
    const int promptH = bodyPrompt_->heightFor(colW);
    if (signInPage_) y = std::max(y, (scroller_->getHeight() - promptH) / 2);
    bodyPrompt_->setBounds(colX, y, colW, promptH);
    y += bodyPrompt_->getHeight();
  } else if (dots_.isVisible()) {
    dots_.setTopLeftPosition(colX + (colW - dots_.getWidth()) / 2, y + kDotsPadY);
    y += kDotsPadY + dots_.getHeight() + kDotsPadY;
  } else if (content_->emptyCopy.isNotEmpty()) {
    const int line = Fonts::normalLineHeight(kEmptyPx);
    content_->emptyBox = {colX + kEmptyPadX, y + kEmptyPadY, colW - 2 * kEmptyPadX, line};
    y += kEmptyPadY + line + kEmptyPadY;
  } else if (!cards_.empty() && cards_.front()->isVisible()) {
    // Two columns of cards that widen with the window, three once three fit
    // at kMinCardWidth. Grid rows are as tall as their tallest card,
    // fractionally (a wrapped 14px title is 36.4px): the rows accumulate at
    // that precision and each card's edges snap where they land, as the CSS
    // grid does.
    const int gridTop = y;
    const size_t cols = colW >= 3 * kMinCardWidth + 2 * kGridGap ? 3 : 2;
    const int cardW = (colW - static_cast<int>(cols - 1) * kGridGap) / static_cast<int>(cols);
    float rowY = static_cast<float>(y);
    for (size_t i = 0; i < cards_.size(); i += cols) {
      const size_t end = std::min(i + cols, cards_.size());
      float rowH = 0;
      for (size_t j = i; j < end; ++j) rowH = std::max(rowH, cards_[j]->contentHeightFor(cardW));
      const int top = juce::roundToInt(rowY), bottom = juce::roundToInt(rowY + rowH);
      for (size_t j = i; j < end; ++j) {
        cards_[j]->setContentHeight(rowH);
        cards_[j]->setBounds(colX + static_cast<int>(j - i) * (cardW + kGridGap), top, cardW, bottom - top);
      }
      rowY += rowH + kGridGap;
    }
    y = juce::roundToInt(rowY - kGridGap);
    if (gridBusy_) gridBusy_->setBounds(colX, gridTop, colW, y - gridTop);
  }

  // The preview's CTA under the list (its own padding parts it from the
  // last row).
  if (footerPrompt_) {
    footerPrompt_->setBounds(colX, y, colW, footerPrompt_->heightFor(colW));
    y += footerPrompt_->getHeight();
  }

  y += kContentPadBottom;
  content_->setSize(w, std::max(y, scroller_->getHeight()));
  content_->repaint();
}

}  // namespace t3k::ui
