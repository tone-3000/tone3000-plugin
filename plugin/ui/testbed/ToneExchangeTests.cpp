#include "MockBackend.h"
#include "MockSession.h"
#include "Scenarios.h"
#include "Host.h"
#include "core/Fonts.h"
#include "services/Services.h"
#include "views/browser/ToneBrowser.h"
#include "widgets/SegmentedText.h"
#include "widgets/Popover.h"
#include "widgets/MenuRow.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>

namespace t3k::ui::testbed {
namespace {

juce::var exchangeObject(std::initializer_list<std::pair<const char*, juce::var>> fields) {
  auto* object = new juce::DynamicObject;
  for (const auto& [name, value] : fields) object->setProperty(name, value);
  return juce::var(object);
}
juce::var exchangeTone(int id, const char* title, std::optional<bool> favorite = false,
                        int favorites = 5, int a2Models = 2) {
  auto tone = exchangeObject({{"id",id},{"title",title},{"description","Inspectable metadata"},
    {"gear","amp"},{"format","nam"},{"models_count",4},{"a2_models_count",a2Models},
    {"favorites_count",favorites},{"downloads_count",id},{"published_at","2026-10-30T12:00:00Z"},
    {"images",juce::Array<juce::var>()},{"models",juce::Array<juce::var>()},
    {"user",exchangeObject({{"id","creator"},{"username","exchange-test"},{"avatar_url",""}})}});
  if (favorite) tone.getDynamicObject()->setProperty("is_favorite", *favorite);
  return tone;
}
juce::Array<juce::var> exchangeRows() {
  return {exchangeTone(30,"Zulu"), exchangeTone(10,"Alpha"), exchangeTone(20,"Bravo")};
}
juce::var exchangePage(const juce::Array<juce::var>& rows, int page = 1, int pages = 1) {
  return exchangeObject({{"data",rows},{"page",page},{"total_pages",pages}});
}
juce::var exchangeScenario(const juce::var& base, bool authenticated, const juce::var& api) {
  auto result = base.clone();
  result.getDynamicObject()->setProperty("auth", authenticated);
  result.getDynamicObject()->setProperty("query", "");
  result.getDynamicObject()->setProperty("offlineAfterLoad", false);
  result.getDynamicObject()->setProperty("api", api.isObject() ? api : exchangeObject({}));
  return result;
}
juce::var exchangeFixtures(const juce::var& base, const juce::Array<juce::var>& rows) {
  auto result = base.clone(); result.getDynamicObject()->setProperty("apiTones", rows);
  result.getDynamicObject()->setProperty("gatedPage", exchangePage(rows));
  return result;
}
void exchangePump(int ms = 20) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}
bool exchangeUntil(const std::function<bool()>& ready, int timeoutMs = 800) {
  for (int elapsed = 0; !ready(); elapsed += 10) {
    if (elapsed >= timeoutMs) return false;
    exchangePump(10);
  }
  return true;
}
template <typename Component>
Component* exchangeFind(juce::Component& root) {
  if (auto* component = dynamic_cast<Component*>(&root)) return component;
  for (auto* child : root.getChildren())
    if (auto* component = exchangeFind<Component>(*child)) return component;
  return nullptr;
}
struct ExchangeShell final : Shell {
  void setExtraContentHeight(int, int) override {}
};
struct ExchangeHarness {
  Fonts::Hold fonts;
  juce::var scenario, fixtures;
  MockBackend backend;
  MockSession session;
  UiPrefs prefs;
  ExchangeShell shell;
  Services services;
  std::unique_ptr<ToneBrowser> browser;

  ExchangeHarness(const juce::var& baseScenario, const juce::var& baseFixtures,
                   juce::Array<juce::var> rows = exchangeRows(), bool authenticated = true,
                   juce::var api = {})
      : scenario(exchangeScenario(baseScenario, authenticated, api)),
        fixtures(exchangeFixtures(baseFixtures, rows)), backend(scenario), session(scenario, fixtures),
        services(backend, session, shell, prefs, false) {
    services.images.offline = true;
    services.browser.exchangeView = true;
  }
  ~ExchangeHarness() { browser.reset(); exchangePump(10); }
  ToneTable* open() {
    browser = std::make_unique<ToneBrowser>(services);
    browser->setSize(1100, 740);
    exchangeUntil([this] { return services.browser.result.has_value(); });
    return exchangeFind<ToneTable>(*browser);
  }
  const Tone* cached(int id) const {
    if (services.browser.result)
      for (const auto& tone : services.browser.result->data)
        if (tone.id == id) return &tone;
    return nullptr;
  }
};


struct ExchangeOverlayFrame final : juce::Component, OverlayHost {
  juce::Component overlay;
  juce::Component& browser;
  explicit ExchangeOverlayFrame(juce::Component& child) : browser(child) {
    setSize(1200, 850);
    addAndMakeVisible(browser);
    browser.setBounds(10, 10, 1100, 740);
    addAndMakeVisible(overlay);
    overlay.setBounds(getLocalBounds());
    overlay.setInterceptsMouseClicks(false, true);
    setVisible(true); // no peer/addToDesktop: this is an in-process component tree only
  }
  ~ExchangeOverlayFrame() override {
    if (browser.getParentComponent() == this) removeChildComponent(&browser);
  }
  juce::Component& overlayLayer() override { return overlay; }
};
template <typename Button>
Button* exchangeNamedButton(juce::Component& root, const juce::String& name) {
  if (auto* button = dynamic_cast<Button*>(&root))
    if (button->getName() == name || button->getTitle() == name || button->getButtonText() == name)
      return button;
  for (auto* child : root.getChildren())
    if (auto* button = exchangeNamedButton<Button>(*child, name)) return button;
  return nullptr;
}

struct ToneExchangeTests final : juce::UnitTest {
  ToneExchangeTests() : juce::UnitTest("Tone Exchange flows", "ui") {}

  bool loaded(ExchangeHarness& harness, ToneTable* table) {
    const bool ready = table != nullptr && harness.services.browser.result.has_value();
    expect(ready, "Exchange table and server page must be present");
    return ready;
  }
  void favoriteIs(ExchangeHarness& harness, int id, bool favorite, int count) {
    const auto* tone = harness.cached(id);
    expect(tone != nullptr);
    if (tone == nullptr) return;
    expect(tone->isFavorite.has_value());
    if (tone->isFavorite) expect(*tone->isFavorite == favorite);
    expectEquals(tone->favoritesCount, count);
    expect(static_cast<bool>(tone->raw["is_favorite"]) == favorite);
    expectEquals(static_cast<int>(tone->raw["favorites_count"]), count);
  }
  void userSort(ToneTable& table, ToneColumn column, bool ascending) {
    auto* native = exchangeFind<juce::TableListBox>(table);
    expect(native != nullptr);
    if (native == nullptr) return;
    native->getHeader().setSortColumnId(static_cast<int>(column) + 1, ascending);
    exchangePump();
  }


  bool submitSearch(ExchangeHarness& harness, const juce::String& text) {
    auto* search = exchangeFind<TextField>(*harness.browser);
    expect(search != nullptr, "the browser search input must exist");
    if (search == nullptr || !search->onEnter) return false;
    search->setText(text);
    auto submit = search->onEnter;
    submit();
    return true;
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* base = fixtures.find("main-mono");
    if (base == nullptr) { expect(false, "main-mono fixture missing"); return; }

    beginTest("sorting retains selection by ID and Return loads the actual sorted row");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      expectEquals(table->selectedToneId().value_or(-1), 30);
      userSort(*table, ToneColumn::title, true);
      expectEquals(table->selectedToneId().value_or(-1), 30);
      expect(harness.services.browser.tableSort.has_value());
      expect(!harness.services.browser.query.sort.has_value(), "local sort must not alter server query");
      expect(table->handleNavigationKey(juce::KeyPress(juce::KeyPress::homeKey)));
      expectEquals(table->selectedToneId().value_or(-1), 10);
      expect(table->handleNavigationKey(juce::KeyPress(juce::KeyPress::downKey)));
      expectEquals(table->selectedToneId().value_or(-1), 20);
      int delivered = -1;
      harness.session.onToneSelected = [&](const Tone& tone) { delivered = tone.id; };
      expect(table->handleNavigationKey(juce::KeyPress(juce::KeyPress::returnKey)));
      expect(harness.session.selectedToneRequests() == std::vector<int>{20});
      expect(exchangeUntil([&] { return delivered == 20; }));
    }

    beginTest("A1-only tones remain inspectable but neither Return nor browser load can select them");
    {
      ExchangeHarness harness(base->data, fixtures.root, {exchangeTone(30,"Legacy A1",false,5,0)});
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      expect(table->selectedTone() != nullptr);
      if (table->selectedTone()) {
        expectEquals(table->selectedTone()->title, juce::String("Legacy A1"));
        expectEquals(table->selectedTone()->catalogModelCount(), 0);
      }
      table->handleNavigationKey(juce::KeyPress(juce::KeyPress::returnKey));
      if (table->onLoad) table->onLoad(30);
      exchangePump();
      expect(harness.session.selectedToneRequests().empty());
    }

    beginTest("known favorite succeeds only after async completion and patches raw/count consistently");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteRequestCount(), 1);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      favoriteIs(harness, 10, true, 6);
      expect(exchangeUntil([&] { return harness.session.favoriteCompletionCount() == 1; }));
      favoriteIs(harness, 10, true, 6);
      std::optional<Tone> server;
      harness.session.getTone(10, [&](ui::Result<Tone> result) { if (result) server = *result; });
      expect(exchangeUntil([&] { return server.has_value(); }));
      if (server) { expect(server->isFavorite.value_or(false)); expectEquals(server->favoritesCount, 6); }
    }

    beginTest("favorite error rolls back optimistic status and the original count");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), true,
                               exchangeObject({{"favorite","error"}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10); favoriteIs(harness, 10, true, 6);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      expect(exchangeUntil([&] { return harness.session.favoriteCompletionCount() == 1; }));
      favoriteIs(harness, 10, false, 5);
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteRequestCount(), 2, "a failed action can be retried");
      exchangePump();
    }

    beginTest("a stalled favorite permits just one pending mutation across row changes");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), true,
                               exchangeObject({{"favorite","stall"}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10); table->onToggleFavorite(20); table->onToggleFavorite(10);
      exchangePump();
      expectEquals(harness.session.favoriteRequestCount(), 1);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      favoriteIs(harness, 10, true, 6); favoriteIs(harness, 20, false, 5);
    }

    beginTest("omitted favorite status is resolved with GET before choosing the mutation");
    {
      auto unknown = exchangeTone(10,"Unknown status",std::nullopt,5);
      auto actual = exchangeTone(10,"Unknown status",true,7);
      ExchangeHarness harness(base->data, fixtures.root, {unknown}, true,
                               exchangeObject({{"tone",actual}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteRequestCount(), 0, "GET has not replied yet");
      expect(harness.cached(10) && !harness.cached(10)->isFavorite.has_value());
      expect(exchangeUntil([&] { return harness.session.favoriteCompletionCount() == 1; }));
      expectEquals(harness.session.favoriteRequestCount(), 1);
      favoriteIs(harness, 10, false, 6); // actual GET said true/count7, so remove; never guess add
    }

    beginTest("unknown status GET errors, omitted state and wrong IDs cannot mutate any favorite");
    for (const auto& override : std::vector<juce::var>{juce::var("error"), exchangeTone(10,"Still unknown",std::nullopt),
                                                     exchangeTone(999,"Wrong tone",true,9)}) {
      ExchangeHarness harness(base->data, fixtures.root, {exchangeTone(10,"Unknown",std::nullopt)}, true,
                               exchangeObject({{"tone",override}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10); exchangePump(40);
      expectEquals(harness.session.favoriteRequestCount(), 0);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      expect(harness.cached(10) && !harness.cached(10)->isFavorite.has_value());
      expectEquals(harness.cached(10) ? harness.cached(10)->favoritesCount : -1, 5);
    }

    beginTest("signed-out loading opens sign-in while favorites and native selection remain guarded");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), false);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      int signIns = 0; harness.browser->onSignIn = [&] { ++signIns; };
      table->onToggleFavorite(30);
      expectEquals(harness.session.favoriteRequestCount(), 0);
      table->onLoad(30);
      expect(harness.session.selectedToneRequests().empty());
      expect(!table->isVisible(), "the sign-in prompt takes over the preview table");
      auto* prompt = exchangeFind<BrowserPrompt>(*harness.browser);
      expect(prompt != nullptr);
      if (prompt && prompt->button().onClick) prompt->button().onClick();
      expectEquals(signIns, 1);
    }

    beginTest("unfavoriting the only row on the final Favorites page fetches the prior distinct page");
    {
      const auto first = exchangeTone(10,"Earlier favorite",true);
      const auto last = exchangeTone(20,"Final favorite",true);
      const auto gated = exchangeObject({{"pages",juce::Array<juce::var>{
          exchangePage({first},1,2), exchangePage({last},2,2)}}});
      ExchangeHarness harness(base->data, fixtures.root, {first,last}, true, exchangeObject({{"gated",gated}}));
      harness.services.browser.query.profile = Profile::favorited; harness.services.browser.page = 2;
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      expectEquals(harness.services.browser.page, 2);
      expectEquals(table->selectedToneId().value_or(-1), 20);
      table->onToggleFavorite(20);
      expectEquals(harness.services.browser.page, 2, "do not navigate before the mutation confirms");
      expect(exchangeUntil([&] {
        return harness.session.favoriteCompletionCount() == 1 && harness.services.browser.page == 1 &&
               harness.cached(10) != nullptr;
      }));
      expect(harness.cached(20) == nullptr);
      expectEquals(table->selectedToneId().value_or(-1), 10);
    }

    beginTest("closing during a queued mutation discards unconfirmed cache and safely refetches confirmed status");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      harness.browser.reset(); expect(!harness.services.browser.result.has_value());
      expect(exchangeUntil([&] { return harness.session.favoriteCompletionCount() == 1; }));
      expect(!harness.services.browser.result.has_value(), "closed component cannot reinstall its old optimistic cache");
      table = harness.open(); if (!loaded(harness, table)) return;
      favoriteIs(harness, 10, true, 6);
    }

    beginTest("a stalled close drops optimistic data and reopening reads the unchanged server state");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), true,
                               exchangeObject({{"favorite","stall"}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10); harness.browser.reset();
      expect(!harness.services.browser.result.has_value());
      table = harness.open(); if (!loaded(harness, table)) return;
      favoriteIs(harness, 10, false, 5);
      expectEquals(harness.session.favoriteRequestCount(), 1);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
    }

    beginTest("view selection and column sort survive browser visits without changing the server query");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->handleNavigationKey(juce::KeyPress(juce::KeyPress::endKey));
      expectEquals(table->selectedToneId().value_or(-1), 20);
      userSort(*table, ToneColumn::downloads, false);
      SegmentedText* toggle = nullptr;
      for (auto* child : harness.browser->getChildren())
        if (auto* value = dynamic_cast<SegmentedText*>(child)) { toggle = value; break; }
      expect(toggle != nullptr);
      if (toggle && toggle->onCellClick) { toggle->onCellClick(0); expect(!harness.browser->exchangeView()); toggle->onCellClick(1); }
      expect(harness.services.browser.exchangeView);
      harness.browser.reset();
      expect(harness.services.browser.result.has_value());
      expectEquals(harness.services.browser.selectedToneId.value_or(-1), 20);
      expect(harness.services.browser.tableSort.has_value());
      if (harness.services.browser.tableSort) {
        expect(harness.services.browser.tableSort->column == ToneColumn::downloads);
        expect(!harness.services.browser.tableSort->ascending);
      }
      table = harness.open(); if (!loaded(harness, table)) return;
      expect(harness.browser->exchangeView()); expectEquals(table->selectedToneId().value_or(-1), 20);
      auto* native = exchangeFind<juce::TableListBox>(*table);
      expect(native != nullptr);
      if (native) {
        expectEquals(native->getHeader().getSortColumnId(), static_cast<int>(ToneColumn::downloads) + 1);
        expect(!native->getHeader().isSortedForwards());
      }
      expect(!harness.services.browser.query.sort.has_value());
    }

    beginTest("closing a stalled new query cannot reopen the previous query's cached rows");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      expectEquals(harness.session.searchRequestCount(), 1);
      expectEquals(static_cast<int>(harness.services.browser.result->data.size()), 3);
      harness.scenario["api"].getDynamicObject()->setProperty("search", "stall");
      if (!submitSearch(harness, "Bravo")) return;
      expectEquals(harness.session.searchRequestCount(), 2);
      expectEquals(harness.services.browser.query.text, juce::String("Bravo"));
      harness.browser.reset();
      expect(!harness.services.browser.result.has_value(), "A rows cannot become the cached result of stalled query B");
      harness.browser = std::make_unique<ToneBrowser>(harness.services);
      harness.browser->setSize(1100, 740);
      exchangePump();
      expectEquals(harness.session.searchRequestCount(), 3, "reopening must issue the still-unanswered B query");
      expect(!harness.services.browser.result.has_value());
      table = exchangeFind<ToneTable>(*harness.browser);
      expect(table != nullptr);
      if (table) expect(!table->selectedToneId().has_value(), "no stale A row may be selected under query B");
    }

    beginTest("closing before a queued new-query reply refetches its own page rather than reusing A");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      if (!submitSearch(harness, "Bravo")) return;
      expectEquals(harness.session.searchRequestCount(), 2);
      harness.browser.reset();
      expect(!harness.services.browser.result.has_value());
      table = harness.open(); if (!loaded(harness, table)) return;
      expectEquals(harness.session.searchRequestCount(), 3);
      expectEquals(static_cast<int>(harness.services.browser.result->data.size()), 1);
      expectEquals(table->selectedToneId().value_or(-1), 20);
      expectEquals(harness.services.browser.query.text, juce::String("Bravo"));
    }

    beginTest("a confirmed PUT after the query changes refreshes the currently visible page");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), true,
                               exchangeObject({{"favorite","manual"}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteRequestCount(), 1);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      if (!submitSearch(harness, "Alpha")) return;
      expect(exchangeUntil([&] {
        return harness.cached(10) && harness.services.browser.result->data.size() == 1;
      }));
      favoriteIs(harness, 10, false, 5); // new GET was completed before held PUT
      expectEquals(harness.session.searchRequestCount(), 2);
      harness.session.releasePendingFavorites();
      expect(exchangeUntil([&] {
        const auto* tone = harness.cached(10);
        return harness.session.favoriteCompletionCount() == 1 &&
               harness.session.searchRequestCount() >= 3 && tone &&
               tone->isFavorite.value_or(false) && tone->favoritesCount == 6;
      }));
      favoriteIs(harness, 10, true, 6);
      expectEquals(harness.services.browser.query.text, juce::String("Alpha"));
      expectEquals(static_cast<int>(harness.services.browser.result->data.size()), 1);
    }

    beginTest("pending favorite ownership survives close/reopen and refreshes the new visit on completion");
    {
      ExchangeHarness harness(base->data, fixtures.root, exchangeRows(), true,
                               exchangeObject({{"favorite","manual"}}));
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteCompletionCount(), 0);
      harness.browser.reset();
      expect(!harness.services.browser.result.has_value());
      table = harness.open(); if (!loaded(harness, table)) return;
      favoriteIs(harness, 10, false, 5); // server has not accepted the held PUT
      table->onToggleFavorite(20);
      table->onToggleFavorite(10);
      expectEquals(harness.session.favoriteRequestCount(), 1,
                   "the new component cannot start a duplicate/opposite mutation while the editor still owns one");
      const int searchesBeforeCompletion = harness.session.searchRequestCount();
      harness.session.releasePendingFavorites();
      expect(exchangeUntil([&] {
        const auto* tone = harness.cached(10);
        return harness.session.favoriteCompletionCount() == 1 &&
               harness.session.searchRequestCount() > searchesBeforeCompletion &&
               tone && tone->isFavorite.value_or(false) && tone->favoritesCount == 6;
      }));
      favoriteIs(harness, 10, true, 6);
      table->onToggleFavorite(20);
      expectEquals(harness.session.favoriteRequestCount(), 2, "confirmed completion releases the editor-wide guard");
      harness.session.releasePendingFavorites();
      expect(exchangeUntil([&] { return harness.session.favoriteCompletionCount() == 2; }));
      favoriteIs(harness, 20, true, 6);
    }

    beginTest("direct table navigation is inert while busy or disabled and resumes when enabled");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      const auto selected = table->selectedToneId();
      table->setBusy(true);
      for (const int code : {juce::KeyPress::downKey,juce::KeyPress::endKey,juce::KeyPress::homeKey,juce::KeyPress::returnKey}) {
        expect(!table->handleNavigationKey(juce::KeyPress(code)));
        expect(table->selectedToneId() == selected);
      }
      expect(harness.session.selectedToneRequests().empty());
      table->setBusy(false);
      table->setEnabled(false);
      for (const int code : {juce::KeyPress::downKey,juce::KeyPress::endKey,juce::KeyPress::returnKey}) {
        expect(!table->handleNavigationKey(juce::KeyPress(code)));
        expect(table->selectedToneId() == selected);
      }
      table->setEnabled(true);
      expect(table->handleNavigationKey(juce::KeyPress(juce::KeyPress::downKey)));
      expectEquals(table->selectedToneId().value_or(-1), 10);
      expect(harness.session.selectedToneRequests().empty());
    }


    beginTest("the Sort page popover exposes explicit ascending descending and API-order actions");
    {
      ExchangeHarness harness(base->data, fixtures.root);
      auto* table = harness.open(); if (!loaded(harness, table)) return;
      const auto selected = table->selectedToneId();
      ExchangeOverlayFrame frame(*harness.browser); // dies before the non-owned browser
      auto* sortButton = exchangeNamedButton<juce::Button>(*table, "Sort page");
      expect(sortButton != nullptr);
      if (sortButton == nullptr || !sortButton->onClick) return;
      const auto ascendingName = juce::String(toneColumnName(ToneColumn::title)) + " ascending";
      const auto descendingName = juce::String(toneColumnName(ToneColumn::title)) + " descending";
      const juce::String actions[]{ascendingName, descendingName, "API order"};
      for (int action = 0; action < 3; ++action) {
        auto openMenu = sortButton->onClick;
        openMenu();
        exchangePump();
        auto* popover = exchangeFind<Popover>(frame.overlay);
        expect(popover != nullptr, "a real native Popover must be mounted in the editor overlay");
        if (popover == nullptr) return;
        expect(popover->getParentComponent() == &frame.overlay);
        expect(popover->isOpen());
        expect(popover->getName() == "Sort page menu" || popover->getTitle() == "Sort page menu");
        expect(exchangeNamedButton<MenuRow>(*popover, ascendingName) != nullptr);
        expect(exchangeNamedButton<MenuRow>(*popover, descendingName) != nullptr);
        int rowCount = 0;
        std::function<void(juce::Component&)> countRows = [&](juce::Component& component) {
          if (dynamic_cast<MenuRow*>(&component)) ++rowCount;
          for (auto* child : component.getChildren()) countRows(*child);
        };
        countRows(*popover);
        expectEquals(rowCount, 17, "API order plus two directions for all eight columns");
        auto* row = exchangeNamedButton<MenuRow>(*popover, actions[action]);
        expect(row != nullptr);
        if (row == nullptr || !row->onClick) return;
        auto choose = row->onClick; // callback may close/delete its owning menu
        choose();
        exchangePump();
        expect(exchangeFind<Popover>(frame.overlay) == nullptr, "choosing a sort closes the overlay menu");
        expect(table->selectedToneId() == selected, "sort actions preserve the inspected Tone ID");
        expect(harness.services.browser.selectedToneId == selected);
        if (action < 2) {
          expect(harness.services.browser.tableSort.has_value());
          if (harness.services.browser.tableSort) {
            expect(harness.services.browser.tableSort->column == ToneColumn::title);
            expect(harness.services.browser.tableSort->ascending == (action == 0));
          }
        } else {
          expect(!harness.services.browser.tableSort.has_value());
        }
        auto* native = exchangeFind<juce::TableListBox>(*table);
        expect(native != nullptr);
        if (native) {
          expectEquals(native->getHeader().getSortColumnId(),
                       action == 2 ? 0 : static_cast<int>(ToneColumn::title) + 1);
          if (action < 2) expect(native->getHeader().isSortedForwards() == (action == 0));
        }
      }
    }

  }
};
static ToneExchangeTests toneExchangeTests;

} // namespace
} // namespace t3k::ui::testbed
