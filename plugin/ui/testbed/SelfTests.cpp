// Unit tests for the UI's pure logic (`UiTestbed --selftest`): the pieces
// pixel comparison can't pin down, such as parsers, wrapping and state
// machines. Each juce::UnitTest here mirrors one core/ or services/ file.
#include "SelfTests.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <optional>
#include <thread>

#include "Drive.h"
#include "Host.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/KnobScale.h"
#include "core/Labels.h"
#include "core/MidiCatalog.h"
#include "core/Pitch.h"
#include "core/RichText.h"
#include "core/TextFlow.h"
#include "core/WindowFit.h"
#include "model/ChainState.h"
#include "model/Tone.h"
#include "model/ToneOverview.h"
#include "model/ToneQuery.h"
#include "services/ConnectionGate.h"
#include "services/LoopbackServer.h"
#include "services/OAuth.h"
#include "services/Pointer.h"
#include "services/Tone3000Client.h"
#include "services/Tone3000Session.h"
#include "services/UiPrefs.h"
#include "services/UpdateCheck.h"
#include "views/Faceplate.h"
#include "views/HintBar.h"
#include "views/PluginHeader.h"
#include "views/PresetBar.h"
#include "views/block/BlockCard.h"
#include "views/block/BlockDetail.h"
#include "views/browser/FilterChip.h"
#include "views/browser/Paginator.h"
#include "views/browser/ToneCard.h"
#include "views/block/BlockInfoPanel.h"
#include "views/gallery/ChainView.h"
#include "views/gallery/GalleryGeometry.h"
#include "views/gallery/GalleryLane.h"
#include "views/gallery/GalleryTile.h"
#include "views/gallery/AddTile.h"
#include "views/gallery/ToneTile.h"
#include "views/gallery/GalleryLane.h"
#include "views/modals/UpdateNotice.h"
#include "views/modals/ConnectionModal.h"
#include "widgets/AccessibleGroup.h"
#include "widgets/RichTextView.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/Avatar.h"
#include "widgets/BackLink.h"
#include "widgets/ChromeTextButton.h"
#include "widgets/Clickable.h"
#include "widgets/DbMeter.h"
#include "widgets/DimGroup.h"
#include "widgets/DragScroller.h"
#include "widgets/EnergyGlow.h"
#include "widgets/FormatBadge.h"
#include "widgets/IconButton.h"
#include "widgets/Knob.h"
#include "widgets/form/FormControls.h"
#include "widgets/form/SelectField.h"
#include "widgets/form/FormRows.h"
#include "widgets/ModelSelect.h"
#include "views/block/BlockEqView.h"
#include "widgets/Popover.h"
#include "widgets/QrCode.h"
#include "widgets/SegmentedText.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui::testbed {

namespace {

// Peer-injected pointer events still use native hit testing. Keep the test
// window on screen and above unrelated desktop windows for the gesture.
void showTestWindow(juce::DocumentWindow& window) {
  window.centreWithSize(window.getWidth(), window.getHeight());
  window.setAlwaysOnTop(true);
  window.setVisible(true);
}

juce::String flat(const RichText& runs) {
  juce::String s;
  for (const auto& r : runs) s += (r.bold ? "*" : "") + (r.href.isNotEmpty() ? "[" + r.text + "]" : r.text) + (r.bold ? "*" : "") + "|";
  return s;
}

struct HtmlTests : juce::UnitTest {
  HtmlTests() : juce::UnitTest("Html::toRichText", "ui") {}
  void runTest() override {
    beginTest("formatting subset");
    expectEquals(flat(Html::toRichText("<p>New in <b>1.5</b>: faster.</p>")), juce::String("New in |*1.5*|: faster.|"));
    expectEquals(flat(Html::toRichText("a<br>b")), juce::String("a|\n|b|"));
    expectEquals(flat(Html::toRichText("<ul><li>one</li><li>two</li></ul>")),
                 juce::String::fromUTF8("\xe2\x80\xa2 one|\n|\xe2\x80\xa2 two|"));

    beginTest("links keep http(s) only");
    const auto link = Html::toRichText("<a href=\"https://x.y/z\">site</a> <a href='javascript:evil()'>no</a>");
    expectEquals(link.size(), static_cast<size_t>(2));
    expectEquals(link[0].href, juce::String("https://x.y/z"));
    expect(link[1].href.isEmpty());

    beginTest("unknown tags drop, entities decode");
    expectEquals(flat(Html::toRichText("<script>x</script><span>a &amp; b &lt;c&gt;</span>")),
                 juce::String("xa & b <c>|"));
  }
};

// The body typeface resolves on this machine (Arial, or the embedded Arimo
// where Arial is missing) and its metrics are numbers. A Font with no
// typeface has height 0 and NaN ascent/descent; text laid out from those
// lands at NaN positions and the software renderer writes out of bounds.
struct FontTests : juce::UnitTest {
  FontTests() : juce::UnitTest("Fonts", "ui") {}
  void runTest() override {
    beginTest("sans resolves to a typeface with finite metrics");
    for (const bool bold : {false, true}) {
      const auto font = Fonts::sans(14, bold);
      expect(font.getTypefacePtr() != nullptr, "no sans typeface");
      expect(font.getHeight() > 0);
      expect(std::isfinite(font.getAscent()) && font.getAscent() > 0);
      expect(std::isfinite(font.getDescent()) && font.getDescent() > 0);
      expect(std::isfinite(Fonts::cssBaseline(font, 18)));
    }

    beginTest("a text flow lays its glyphs out at finite positions");
    const TextFlow flow(Fonts::sans(14, true), 18.2f, "'02 Vox AC30/6 Top Boost", 240);
    expect(flow.lineCount() >= 1);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(Fonts::sans(14, true), "Top Boost", 140, Fonts::cssBaseline(Fonts::sans(14, true), 18.2f));
    expect(glyphs.getNumGlyphs() > 0);
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i) {
      const auto& g = glyphs.getGlyph(i);
      expect(std::isfinite(g.getLeft()) && std::isfinite(g.getRight()) && std::isfinite(g.getBaselineY()));
    }
  }
};

struct RichFlowTests : juce::UnitTest {
  RichFlowTests() : juce::UnitTest("RichFlow", "ui") {}
  void runTest() override {
    beginTest("wraps across runs and keeps style per word");
    RichText runs{TextRun::strong("No audio input."), TextRun::plain(" No input device is selected.")};
    const RichFlow narrow(runs, 13, 18, 120);
    expect(narrow.lineCount() >= 3);
    expect(narrow.maxLineWidth() <= 120);
    const RichFlow wide(runs, 13, 18, 2000);
    expectEquals(wide.lineCount(), 1);

    beginTest("paragraph breaks force lines");
    RichText paras{TextRun::plain("a"), paragraphBreak(), TextRun::plain("b")};
    expectEquals(RichFlow(paras, 13, 18, 500).lineCount(), 2);

    beginTest("link hit-testing");
    RichText linky{TextRun::plain("see "), TextRun::link("here", "https://t.co")};
    const RichFlow flow(linky, 13, 18, 500);
    expectEquals(flow.linkAt({flow.maxLineWidth() - 2, 9}, {0, 0}), juce::String("https://t.co"));
    expect(flow.linkAt({1, 9}, {0, 0}).isEmpty());
    // The space in "see " belongs to the plain run, not the link after it
    // (so an underline never bleeds under the space before it).
    expect(flow.linkAt({Fonts::width(Fonts::sans(13), "see") + 1, 9}, {0, 0}).isEmpty());
  }
};

struct UpdateCheckTests : juce::UnitTest {
  UpdateCheckTests() : juce::UnitTest("UpdateCheck", "ui") {}
  void runTest() override {
    beginTest("compareVersions");
    expect(UpdateCheck::compareVersions("1.5.0", "1.4.9") > 0);
    expect(UpdateCheck::compareVersions("v1.5", "1.5.0") == 0);
    expect(UpdateCheck::compareVersions("1.5.0-beta", "1.5.0") == 0);
    expect(UpdateCheck::compareVersions("1.10", "1.9") > 0);
    expect(UpdateCheck::compareVersions("0.9", "1.0") < 0);

    beginTest("parsePayload rejects bad shapes and non-http urls");
    auto make = [](const char* url) {
      auto* o = new juce::DynamicObject();
      o->setProperty("version", "2.0.0");
      o->setProperty("message_html", "<p>hi</p>");
      o->setProperty("url", url);
      return juce::var(o);
    };
    expect(UpdateCheck::parsePayload(make("https://www.tone3000.com/plugin")).has_value());
    expect(!UpdateCheck::parsePayload(make("javascript:alert(1)")).has_value());
    expect(!UpdateCheck::parsePayload(juce::var("nope")).has_value());
  }
};

// A signed-out session whose reachability the test scripts.
struct ScriptedSession : ToneSession {
  bool isOnline = true;
  std::vector<Probe> probes;  // replies in order; empty = inconclusive
  int probeCalls = 0;

  bool online() const override { return isOnline; }
  void probeSecureConnection(std::function<void(Probe)> reply) override {
    ++probeCalls;
    const auto result = probes.empty() ? Probe::inconclusive : probes.front();
    if (!probes.empty()) probes.erase(probes.begin());
    reply(result);
  }

  bool authenticated() const override { return false; }
  std::optional<User> user() const override { return std::nullopt; }
  void getTone(int, Reply<Tone> reply) override { reply(Result<Tone>::fail("n/a")); }
  void listToneModels(int, const juce::String&, Reply<std::vector<Model>> reply) override {
    reply(Result<std::vector<Model>>::fail("n/a"));
  }
  void setToneFavorite(int, bool, Done done) override { done("n/a"); }
  void searchTones(const ToneQuery&, int, int, Reply<TonePage> reply) override {
    reply(Result<TonePage>::fail("n/a"));
  }
  void listTrending(const juce::String&, Reply<std::vector<Tone>> reply) override {
    reply(Result<std::vector<Tone>>::fail("n/a"));
  }
  void listTaxonomy(Taxonomy, const juce::String&, Reply<std::vector<TaxonomyEntry>> reply) override {
    reply(Result<std::vector<TaxonomyEntry>>::fail("n/a"));
  }
  void selectTone(int, Done done) override { done("n/a"); }
  void ensureNativeAuth(Done done) override { done("n/a"); }
  void login(LoginIntent) override {}
  void logout() override {}
  const AuthFlow& authFlow() const override { return flow; }
  void retryFlow() override {}
  void cancelFlow() override {}
  void clearAuthError() override {}
  void startDeviceFlow() override {}
  void fetchPluginVersion(Reply<juce::var> reply) override { reply(Result<juce::var>::fail("n/a")); }
  AuthFlow flow;
};

struct ConnectionGateTests : juce::UnitTest {
  ConnectionGateTests() : juce::UnitTest("ConnectionGate", "ui") {}
  void runTest() override {
    beginTest("offline queues the action; retry releases it once online");
    ScriptedSession session;
    session.isOnline = false;
    ConnectionGate gate(session);
    int ran = 0;
    gate.requireConnection([&] { ++ran; });
    expect(gate.problem() == ConnectionGate::Problem::offline);
    expectEquals(ran, 0);
    gate.retry();  // still offline: nothing moves
    expect(gate.problem() == ConnectionGate::Problem::offline);
    session.isOnline = true;
    gate.retry();
    expect(!gate.problem());
    expectEquals(ran, 1);

    beginTest("online runs at once and probes in the background, throttled");
    ScriptedSession live;
    ConnectionGate g2(live);
    g2.requireConnection([&] { ++ran; });
    expectEquals(ran, 2);
    expectEquals(live.probeCalls, 1);
    g2.requireConnection([&] { ++ran; });
    expectEquals(live.probeCalls, 1);  // within the TTL

    beginTest("a single insecure result never alarms; retry forces a probe");
    ScriptedSession flaky;
    flaky.probes = {ToneSession::Probe::insecure, ToneSession::Probe::ok};
    ConnectionGate g3(flaky);
    g3.requireConnection({});
    expect(!g3.problem());  // confirmation pending (2s), no alarm yet
    expectEquals(flaky.probeCalls, 1);

    beginTest("dismiss drops the queued action");
    ScriptedSession off;
    off.isOnline = false;
    ConnectionGate g4(off);
    int never = 0;
    g4.requireConnection([&] { ++never; });
    g4.dismiss();
    off.isOnline = true;
    g4.retry();
    expectEquals(never, 0);
    expect(!g4.problem());
  }
};

struct PitchTests : juce::UnitTest {
  PitchTests() : juce::UnitTest("pitch", "ui") {}
  void runTest() override {
    beginTest("frequency to note");
    const auto a4 = pitch::fromFrequency(440);
    expectEquals(a4.name, juce::String("A"));
    expectEquals(a4.octave, 4);
    expectWithinAbsoluteError(a4.cents, 0.0f, 0.01f);
    const auto sharp = pitch::fromFrequency(277.18f);
    expectEquals(sharp.name, juce::String::fromUTF8("C\xe2\x99\xaf"));
    expectEquals(sharp.octave, 4);
    beginTest("lit bars");
    expectEquals(pitch::litCount(0), 1);
    expectEquals(pitch::litCount(50), pitch::kBarsPerSide);
  }
};

struct OAuthTests : juce::UnitTest {
  OAuthTests() : juce::UnitTest("OAuth", "ui") {}
  void runTest() override {
    beginTest("PKCE: base64url, S256 challenge, fresh randomness");
    // RFC 7636 appendix B.
    expectEquals(oauth::Pkce::challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 juce::String("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    const auto a = oauth::Pkce::generate(), b = oauth::Pkce::generate();
    expect(a.verifier != b.verifier && a.state != b.state);
    expectEquals(a.challenge, oauth::Pkce::challengeFor(a.verifier));
    expect(!a.verifier.containsAnyOf("+/=") && !a.state.containsAnyOf("+/="));
    expectEquals(a.verifier.length(), 43);  // 32 bytes → 43 unpadded chars
    expectEquals(a.state.length(), 22);     // 16 bytes → 22

    beginTest("authorize URL carries PKCE and the extras");
    oauth::Pkce pkce;
    pkce.challenge = "CH";
    pkce.state = "ST";
    juce::StringPairArray extra;
    extra.set("menubar", "true");
    const juce::URL url(oauth::authorizeUrl("https://www.tone3000.com", "pk_x", "http://127.0.0.1:1234/cb", pkce, extra));
    expectEquals(url.toString(false), juce::String("https://www.tone3000.com/api/v1/oauth/authorize"));
    const auto q = url.getParameterNames(), v = url.getParameterValues();
    auto param = [&](const char* name) { return v[q.indexOf(name)]; };
    expectEquals(param("client_id"), juce::String("pk_x"));
    expectEquals(param("redirect_uri"), juce::String("http://127.0.0.1:1234/cb"));
    expectEquals(param("code_challenge"), juce::String("CH"));
    expectEquals(param("code_challenge_method"), juce::String("S256"));
    expectEquals(param("state"), juce::String("ST"));
    expectEquals(param("menubar"), juce::String("true"));

    beginTest("callback parsing");
    using K = oauth::Callback::Kind;
    auto parse = [](const char* query) { return oauth::Callback::parse(query, "ST"); };
    expect(parse("?code=abc&state=ST").kind == K::code);
    expect(parse("?code=abc&state=OTHER").kind == K::error);
    expectEquals(parse("?code=abc&state=OTHER").error, juce::String("state_mismatch"));
    expect(parse("?canceled=true&state=ST").kind == K::canceled);
    expect(parse("?canceled=true&code=abc&state=ST").kind == K::code);  // a code wins over the flag
    expectEquals(parse("?error=access_denied&state=ST").error, juce::String("access_denied"));
    expectEquals(parse("?state=ST").error, juce::String("missing_code"));
  }
};

struct LoopbackServerTests : juce::UnitTest {
  LoopbackServerTests() : juce::UnitTest("LoopbackServer", "ui") {}
  void runTest() override {
    beginTest("serves one redirect on an ephemeral port and hands over the query");
    LoopbackServer server;
    juce::String query;
    server.onCallback = [&](const juce::String& q) { query = q; };
    expect(server.start());
    expect(server.port() > 0);
    expectEquals(server.redirectUri(), "http://localhost:" + juce::String(server.port()) + "/");

    // The browser's GET, from a worker so the message thread stays free for
    // the callback.
    const juce::URL url("http://127.0.0.1:" + juce::String(server.port()) + "/?code=abc&state=xyz");
    std::atomic<int> status{-1};
    juce::String page;
    std::thread fetcher([&] {
      int code = 0;
      auto stream = url.createInputStream(
          juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(3000).withStatusCode(&code));
      if (stream) page = stream->readEntireStreamAsString();
      status = code;
    });
    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
    while ((query.isEmpty() || status < 0) && juce::Time::getMillisecondCounter() < deadline)
      juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    fetcher.join();
    expectEquals(status.load(), 200);
    expect(page.contains("signed in.") && page.contains("return to the TONE3000 Plugin"));
    expectEquals(query, juce::String("code=abc&state=xyz"));

    beginTest("one redirect per flow: the listener is gone afterwards");
    const auto deadline2 = juce::Time::getMillisecondCounter() + 2000;
    while (server.running() && juce::Time::getMillisecondCounter() < deadline2)
      juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    expect(!server.running());
    server.stop();
    expectEquals(server.port(), 0);
  }
};

struct PaginatorTests : juce::UnitTest {
  PaginatorTests() : juce::UnitTest("Paginator", "ui") {}
  void runTest() override {
    using V = std::vector<int>;
    beginTest("seven or fewer pages list them all");
    expect(Paginator::pagesFor(1, 1) == V{1});
    expect(Paginator::pagesFor(4, 7) == V{1, 2, 3, 4, 5, 6, 7});
    beginTest("past seven: head, tail and a window (0 = ellipsis)");
    expect(Paginator::pagesFor(1, 20) == V{1, 2, 3, 4, 0, 20});
    expect(Paginator::pagesFor(3, 20) == V{1, 2, 3, 4, 0, 20});
    expect(Paginator::pagesFor(4, 20) == V{1, 0, 3, 4, 5, 0, 20});
    expect(Paginator::pagesFor(17, 20) == V{1, 0, 16, 17, 18, 0, 20});
    expect(Paginator::pagesFor(18, 20) == V{1, 0, 17, 18, 19, 20});
    expect(Paginator::pagesFor(20, 20) == V{1, 0, 17, 18, 19, 20});
  }
};

// A transport that answers from a script, synchronously, recording requests.
struct ScriptedHttp : HttpTransport {
  struct Sent {
    juce::String url, method, authorization, body;
  };
  std::vector<Sent> sent;
  std::function<HttpResponse(const HttpRequest&)> answer;
  void send(HttpRequest request, std::function<void(HttpResponse)> onDone) override {
    sent.push_back({request.url.toString(true), request.method, request.headers["Authorization"], request.body});
    onDone(answer(request));
  }
};

HttpResponse jsonResponse(int status, const juce::String& body) {
  HttpResponse r;
  r.status = status;
  r.body = body;
  return r;
}

// Two PropertiesFile objects on one file stand in for two host processes.
struct UiPrefsTests : juce::UnitTest {
  UiPrefsTests() : juce::UnitTest("UiPrefs", "ui") {}

  struct Host : UiPrefs::Listener {
    Host(const juce::File& file, juce::InterProcessLock& lock)
        : props(file,
                [&] {
                  juce::PropertiesFile::Options o;
                  o.millisecondsBeforeSaving = -1;
                  o.processLock = &lock;
                  return o;
                }()),
          prefs(&props, &lock) {
      prefs.addListener(this);
    }
    ~Host() override { prefs.removeListener(this); }
    void prefChanged(const juce::String& key) override { heard.add(key); }
    juce::PropertiesFile props;
    UiPrefs prefs;
    juce::StringArray heard;
  };

  void runTest() override {
    const auto file = juce::File::createTempFile("t3k-prefs.xml");
    juce::InterProcessLock lock("TONE3000.ui-preferences.test");
    Host a(file, lock), b(file, lock);

    beginTest("a write merges in the other host's writes instead of overwriting them");
    a.prefs.set(UiPrefs::kTokens, "A");
    b.prefs.setBool(UiPrefs::kShowHints, false);  // b's copy never saw kTokens
    expectEquals(b.prefs.get(UiPrefs::kTokens), juce::String("A"));
    expect(b.heard.contains(UiPrefs::kTokens) && b.heard.contains(UiPrefs::kShowHints));
    Host c(file, lock);  // what is on disk
    expectEquals(c.prefs.get(UiPrefs::kTokens), juce::String("A"));
    expect(!c.prefs.getBool(UiPrefs::kShowHints, true));

    beginTest("sync pulls in changes and removals, telling listeners of each");
    a.heard.clear();
    a.prefs.sync();  // b's hints toggle
    expect(!a.prefs.getBool(UiPrefs::kShowHints, true) && a.heard.contains(UiPrefs::kShowHints));
    a.heard.clear();
    b.prefs.set(UiPrefs::kTokens, "B");
    b.prefs.remove(UiPrefs::kShowHints);
    expectEquals(a.prefs.get(UiPrefs::kTokens), juce::String("A"));
    a.prefs.sync();
    expectEquals(a.prefs.get(UiPrefs::kTokens), juce::String("B"));
    expect(a.prefs.getBool(UiPrefs::kShowHints, true));
    expect(a.heard.contains(UiPrefs::kTokens) && a.heard.contains(UiPrefs::kShowHints));
    a.heard.clear();
    a.prefs.sync();
    expect(a.heard.isEmpty());

    beginTest("a write of the value already held is no write");
    a.prefs.set(UiPrefs::kTokens, "B");
    expect(a.heard.isEmpty());

    file.deleteFile();
  }
};

struct Tone3000ClientTests : juce::UnitTest {
  Tone3000ClientTests() : juce::UnitTest("Tone3000Client", "ui") {}
  void runTest() override {
    beginTest("token payloads round-trip; expiry is absolute");
    const auto fromResponse = Tokens::fromTokenResponse(
        juce::JSON::parse(R"({"access_token":"A","refresh_token":"R","expires_in":3600})"), 1'000);
    expect(fromResponse.has_value());
    expectEquals(fromResponse->expiresAtMs, static_cast<juce::int64>(3'601'000));
    expect(!Tokens::fromTokenResponse(juce::JSON::parse(R"({"refresh_token":"R"})"), 0).has_value());
    const auto back = Tokens::fromVar(fromResponse->toVar());
    expect(back && back->access == "A" && back->refresh == "R" && back->expiresAtMs == fromResponse->expiresAtMs);

    beginTest("tokens persist in prefs and clear on demand");
    UiPrefs prefs;
    ScriptedHttp http;
    Tone3000Client client(http, prefs, "https://api.test", "pk_test");
    client.now = [] { return juce::int64{1'000'000}; };
    expect(!client.authenticated());
    client.setTokens({"A1", "R1", 1'000'000 + 3'600'000});
    expect(client.authenticated());
    {
      Tone3000Client again(http, prefs, "https://api.test", "pk_test");
      expect(again.authenticated() && again.tokens()->access == "A1");
    }

    beginTest("a fresh token is used as is; a near-expired one refreshes first");
    http.answer = [](const HttpRequest& r) {
      if (r.url.toString(false).endsWith("/oauth/token"))
        return jsonResponse(200, R"({"access_token":"A2","refresh_token":"R2","expires_in":3600})");
      return jsonResponse(200, R"({"id":7})");
    };
    juce::String token;
    client.getAccessToken([&](Result<juce::String> r) { token = r ? *r : juce::String(); });
    expectEquals(token, juce::String("A1"));
    expectEquals(static_cast<int>(http.sent.size()), 0);
    client.setTokens({"A1", "R1", 1'000'000 + Tone3000Client::kRefreshLeadMs / 2});
    int updates = 0;
    client.onTokensUpdated = [&](const Tokens&) { ++updates; };
    client.getAccessToken([&](Result<juce::String> r) { token = r ? *r : juce::String(); });
    expectEquals(token, juce::String("A2"));
    expectEquals(updates, 1);
    expectEquals(static_cast<int>(http.sent.size()), 1);
    expect(http.sent[0].body.contains("grant_type=refresh_token") && http.sent[0].body.contains("refresh_token=R1"));
    expectEquals(client.tokens()->access, juce::String("A2"));

    beginTest("401 refreshes once and retries with the new token");
    http.sent.clear();
    int calls = 0;
    http.answer = [&](const HttpRequest& r) {
      if (r.url.toString(false).endsWith("/oauth/token"))
        return jsonResponse(200, R"({"access_token":"A3","refresh_token":"R3","expires_in":3600})");
      return ++calls == 1 ? jsonResponse(401, "{}") : jsonResponse(200, R"({"id":7})");
    };
    juce::var user;
    client.getUser([&](Result<juce::var> r) { user = r ? *r : juce::var(); });
    expectEquals(static_cast<int>(user["id"]), 7);
    expectEquals(static_cast<int>(http.sent.size()), 3);
    expectEquals(http.sent[0].authorization, juce::String("Bearer A2"));
    expectEquals(http.sent[2].authorization, juce::String("Bearer A3"));

    beginTest("a rejected refresh signs out and reports auth_required");
    bool authRequired = false;
    client.onAuthRequired = [&] { authRequired = true; };
    client.setTokens({"A3", "R3", 0});  // expired
    http.answer = [](const HttpRequest&) { return jsonResponse(400, R"({"error":"invalid_grant"})"); };
    juce::String error;
    client.getAccessToken([&](Result<juce::String> r) { error = r.error; });
    expect(authRequired);
    expect(!client.authenticated());
    expectEquals(error, juce::String("token_refresh_failed"));
    expect(!Tokens::fromVar(prefs.getJson(UiPrefs::kTokens)).has_value());

    beginTest("a rejected refresh adopts the pair another host rotated meanwhile");
    authRequired = false;
    client.setTokens({"A4", "R4", 0});
    http.answer = [&](const HttpRequest&) {
      prefs.setJson(UiPrefs::kTokens, Tokens{"A5", "R5", 1'000'000 + 3'600'000}.toVar());  // the other host
      return jsonResponse(400, R"({"error":"invalid_grant"})");
    };
    client.getAccessToken([&](Result<juce::String> r) { token = r ? *r : juce::String(); });
    expectEquals(token, juce::String("A5"));
    expect(!authRequired && client.authenticated());

    beginTest("optional-auth calls fall back to anonymous when the session is gone");
    client.clearTokens();
    http.sent.clear();
    http.answer = [](const HttpRequest&) { return jsonResponse(200, "[]"); };
    HttpResponse anon;
    client.fetchOptionalAuth("/api/v1/plugin/version", {}, [&](Result<HttpResponse> r) { anon = r ? *r : HttpResponse(); });
    expect(anon.ok());
    expectEquals(static_cast<int>(http.sent.size()), 1);
    expect(http.sent[0].authorization.isEmpty());

    beginTest("device authorization: the code pair, with the defaults the response leaves out");
    http.sent.clear();
    http.answer = [](const HttpRequest&) {
      return jsonResponse(200, R"({"device_code":"D1","user_code":"BCDF-GHJK",
        "verification_uri":"https://www.tone3000.com/activate",
        "verification_uri_complete":"https://www.tone3000.com/activate?user_code=BCDF-GHJK",
        "expires_in":600,"interval":5})");
    };
    std::optional<DeviceAuthorization> device;
    client.requestDeviceAuthorization([&](Result<DeviceAuthorization> r) {
      if (r) device = *r;
    });
    expect(device.has_value());
    expectEquals(device->deviceCode, juce::String("D1"));
    expectEquals(device->userCode, juce::String("BCDF-GHJK"));
    expectEquals(device->verificationUriComplete, juce::String("https://www.tone3000.com/activate?user_code=BCDF-GHJK"));
    expectEquals(device->intervalS, 5);
    expect(http.sent[0].url.endsWith("/api/v1/oauth/device_authorization"));
    expect(http.sent[0].body == "client_id=pk_test");
    const auto bare = DeviceAuthorization::fromVar(
        juce::JSON::parse(R"({"device_code":"D","user_code":"U","verification_uri":"https://t/activate"})"));
    expect(bare && bare->verificationUriComplete == "https://t/activate" && bare->intervalS == 5 &&
           bare->expiresInS == 600);
    expect(!DeviceAuthorization::fromVar(juce::JSON::parse(R"({"user_code":"U"})")).has_value());

    beginTest("device poll: the grant, the server's pending codes as errors, tokens on approval");
    http.sent.clear();
    http.answer = [](const HttpRequest&) { return jsonResponse(400, R"({"error":"authorization_pending"})"); };
    juce::String pollError;
    client.pollDeviceToken("D1", [&](Result<Tokens> r) { pollError = r ? juce::String() : r.error; });
    expectEquals(pollError, juce::String("authorization_pending"));
    expect(http.sent[0].url.endsWith("/api/v1/oauth/token"));
    expect(http.sent[0].body.contains("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"));
    expect(http.sent[0].body.contains("device_code=D1") && http.sent[0].body.contains("client_id=pk_test"));
    http.answer = [](const HttpRequest&) { return jsonResponse(400, R"({"error":"slow_down"})"); };
    client.pollDeviceToken("D1", [&](Result<Tokens> r) { pollError = r ? juce::String() : r.error; });
    expectEquals(pollError, juce::String("slow_down"));
    http.answer = [](const HttpRequest&) {
      return jsonResponse(200, R"({"access_token":"A6","refresh_token":"R6","expires_in":3600})");
    };
    std::optional<Tokens> granted;
    client.pollDeviceToken("D1", [&](Result<Tokens> r) {
      if (r) granted = *r;
    });
    expect(granted && granted->access == "A6" && granted->refresh == "R6");
  }
};

// Sign-in in a process with no browser (Config::signInViaApp, the iOS
// AUv3): the device flow is the sign-in, and a sign-in made in the TONE3000
// app is adopted from the shared preferences. Two UiPrefs on one file stand
// in for the app's and the plugin's processes.
struct Tone3000SessionTests : juce::UnitTest {
  Tone3000SessionTests() : juce::UnitTest("Tone3000Session", "ui") {}

  struct Process {
    Process(const juce::File& file, juce::InterProcessLock& lock)
        : props(file,
                [&] {
                  juce::PropertiesFile::Options o;
                  o.millisecondsBeforeSaving = -1;
                  o.processLock = &lock;
                  return o;
                }()),
          prefs(&props, &lock) {}
    juce::PropertiesFile props;
    UiPrefs prefs;
  };

  struct Ears : ToneSession::Listener {
    explicit Ears(const ToneSession& s) : session(s) {}
    const ToneSession& session;
    int sessionChanges = 0;
    bool sawScreen = false;  // the flow was seen leaving (the sign-in screen came up)
    void reset() { sessionChanges = 0, sawScreen = false; }
    void sessionChanged() override { ++sessionChanges; }
    void authFlowChanged() override {
      sawScreen |= session.authFlow().phase == ToneSession::AuthFlow::Phase::leaving;
    }
  };

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  void runTest() override {
    using Phase = ToneSession::AuthFlow::Phase;
    using DeviceState = ToneSession::AuthFlow::Device::State;
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    ScriptedHttp http;
    // The device endpoints as the server answers them: a code at once, then
    // "not yet" to every poll until `approved` is set.
    bool approved = false;
    http.answer = [&](const HttpRequest& r) {
      const auto path = r.url.toString(false);
      if (path.endsWith("/oauth/device_authorization"))
        return jsonResponse(200, R"({"device_code":"D1","user_code":"BCDF-GHJK",
          "verification_uri":"https://www.tone3000.com/activate",
          "verification_uri_complete":"https://www.tone3000.com/activate?user_code=BCDF-GHJK",
          "expires_in":600,"interval":1})");
      if (path.endsWith("/oauth/token"))
        return approved ? jsonResponse(200, R"({"access_token":"A2","refresh_token":"R2","expires_in":3600})")
                        : jsonResponse(400, R"({"error":"authorization_pending"})");
      return jsonResponse(200, R"({"id":7,"username":"ada"})");
    };
    const auto file = juce::File::createTempFile("t3k-session.xml");
    juce::InterProcessLock lock("TONE3000.ui-preferences.session-test");
    Process app(file, lock), plugin(file, lock);
    Tone3000Session::Config config;
    config.apiOrigin = "https://api.test";
    config.publishableKey = "pk_test";
    config.signInViaApp = true;
    Tone3000Session session(backend, plugin.prefs, http, config);
    Ears ears(session);
    session.addListener(&ears);
    int browserOpens = 0;
    session.onAuthenticated = [&] { ++browserOpens; };
    const Tokens tokens{"A1", "R1", juce::Time::currentTimeMillis() + 3'600'000};
    const auto access = [&] {
      const auto t = Tokens::fromVar(plugin.prefs.getJson(UiPrefs::kTokens));
      return t ? t->access : juce::String();
    };
    const auto deviceRequests = [&] {
      return static_cast<int>(std::count_if(http.sent.begin(), http.sent.end(), [](const ScriptedHttp::Sent& s) {
        return s.url.endsWith("/oauth/device_authorization");
      }));
    };

    beginTest("signed out, login goes straight to the device code, no browser");
    session.login(ToneSession::LoginIntent::plain);
    const auto& flow = session.authFlow();
    expect(flow.phase == Phase::leaving && flow.deviceOnly);
    expect(flow.device && flow.device->state == DeviceState::waiting && flow.device->userCode == "BCDF-GHJK");
    expect(flow.authorizeUrl.isEmpty());  // nothing to copy: there is no browser path
    expectEquals(deviceRequests(), 1);
    expect(!session.authenticated());

    beginTest("the back arrow abandons it: the code poll and the app watch both stop");
    session.cancelFlow();
    expect(flow.phase == Phase::idle && !flow.device);
    const auto polls = static_cast<int>(http.sent.size());
    app.prefs.setJson(UiPrefs::kTokens, tokens.toVar());  // the app signs in after the dismissal
    pump(Tone3000Session::kAppSignInPollMs + 300);
    expect(flow.phase == Phase::idle);
    expectEquals(ears.sessionChanges, 0);
    expectEquals(static_cast<int>(http.sent.size()), polls);  // no device poll either
    app.prefs.remove(UiPrefs::kTokens);

    beginTest("the app signing in while the page is up finishes the flow by itself");
    ears.reset();
    session.login(ToneSession::LoginIntent::browse);
    expect(flow.phase == Phase::leaving && flow.device.has_value());
    app.prefs.setJson(UiPrefs::kTokens, tokens.toVar());
    expect(!session.authenticated());  // not until the poll re-reads the file
    pump(Tone3000Session::kAppSignInPollMs + 300);
    expect(session.authenticated() && access() == "A1");
    expect(flow.phase == Phase::idle && !flow.device);
    expect(ears.sessionChanges >= 1);
    expectEquals(browserOpens, 1);  // a browse-intent login still lands in the browser
    expect(session.user().has_value() && session.user()->username == "ada");
    session.logout();
    app.prefs.sync();  // the app re-reads the file on its own schedule
    expect(!session.authenticated() && !app.prefs.getJson(UiPrefs::kTokens).isObject());  // one sign-out for both

    beginTest("the code being approved finishes it, and the app watch stops");
    ears.reset();
    http.sent.clear();
    session.login(ToneSession::LoginIntent::plain);
    expect(flow.phase == Phase::leaving && flow.deviceOnly);
    approved = true;
    pump(1500);  // the server's interval is 1 s
    expect(session.authenticated() && access() == "A2");
    expect(flow.phase == Phase::idle);
    expectEquals(browserOpens, 1);  // plain intent: no browser
    app.prefs.sync();
    expect(Tokens::fromVar(app.prefs.getJson(UiPrefs::kTokens)).has_value());  // the app has them too
    approved = false;

    beginTest("already signed in by the app, a gated action simply goes ahead");
    ears.reset();
    http.sent.clear();
    session.login(ToneSession::LoginIntent::plain);
    expect(flow.phase == Phase::idle);
    expect(!ears.sawScreen);  // never showed the page
    expectEquals(deviceRequests(), 0);

    beginTest("a loopback that will not bind falls back to the device code");
    session.logout();
    http.sent.clear();
    auto unbound = config;
    unbound.signInViaApp = false;
    unbound.loopbackAddress = "192.0.2.1";  // TEST-NET-1: on no interface
    Tone3000Session desktop(backend, plugin.prefs, http, unbound);
    desktop.login(ToneSession::LoginIntent::plain);
    const auto& fallback = desktop.authFlow();
    expect(fallback.phase == Phase::leaving && !fallback.deviceOnly);
    expect(fallback.device && fallback.device->state == DeviceState::waiting);
    expect(fallback.authorizeUrl.isEmpty());  // no listener, so no link to copy
    expectEquals(deviceRequests(), 1);
    approved = true;
    pump(1500);
    expect(desktop.authenticated() && fallback.phase == Phase::idle);

    session.removeListener(&ears);
    file.deleteFile();
  }
};


struct DragScrollerTests : juce::UnitTest {
  DragScrollerTests() : juce::UnitTest("DragScroller", "ui") {}

  // A wheel event as JUCE delivers it to the viewport itself (bubbled up
  // from the child it landed on).
  static void wheel(DragScroller& s, float deltaX, float deltaY) {
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), {10.0f, 10.0f}, {}, 0, 0, 0, 0, 0, &s,
                             &s, now, {10.0f, 10.0f}, now, 0, false);
    juce::MouseWheelDetails w;
    w.deltaX = deltaX;
    w.deltaY = deltaY;
    s.mouseWheelMove(e, w);
  }

  struct Rig {
    DragScroller s{DragScroller::Axis::horizontal};
    juce::Component content;
    Rig() {
      content.setSize(4000, 100);
      s.setSize(400, 100);
      s.setViewedComponent(&content, false);
      s.setViewPosition(1000, 0);
    }
  };

  void runTest() override {
    beginTest("a mostly vertical gesture pans by its vertical delta, sideways jitter ignored");
    {
      Rig r;
      wheel(r.s, 0.001f, -0.05f);  // 11.2px (JUCE: up is negative; the view moves the other way)
      expectEquals(r.s.getViewPositionX(), 1011);
      wheel(r.s, -0.002f, -0.05f);  // jitter flips sign: same direction, and the .2 carried
      expectEquals(r.s.getViewPositionX(), 1022);
    }

    beginTest("a native sideways gesture pans by its own delta");
    {
      Rig r;
      wheel(r.s, 0.05f, 0.001f);
      expectEquals(r.s.getViewPositionX(), 989);
    }

    beginTest("slow gestures accumulate sub-pixel motion instead of rounding up per event");
    {
      Rig r;
      for (int i = 0; i < 10; ++i) wheel(r.s, 0.0f, -0.001f);  // 0.224px each: 2.24px in all
      expectEquals(r.s.getViewPositionX(), 1002);
    }

    // A vertical page, as Settings has: 300px of view over 2000px of content.
    struct Page {
      DragScroller s{DragScroller::Axis::vertical};
      juce::Component content;
      Page() {
        content.setSize(400, 2000);
        s.setSize(400, 300);
        s.setViewedComponent(&content, false);
      }
    };
    using KP = juce::KeyPress;
    const auto key = [](int code, juce::ModifierKeys mods = {}) { return KP(code, mods, 0); };

    beginTest("keys scroll a page: arrows by a line, Page by a page less a line, Home / End to the ends");
    {
      Page p;
      expect(p.s.scrollByKey(key(KP::downKey)));
      expectEquals(p.s.getViewPositionY(), DragScroller::kKeyLineStep);
      expect(p.s.scrollByKey(key(KP::pageDownKey)));
      expectEquals(p.s.getViewPositionY(), 300);  // a line, then a page less a line
      expect(p.s.scrollByKey(key(KP::upKey)));
      expectEquals(p.s.getViewPositionY(), 300 - DragScroller::kKeyLineStep);
      expect(p.s.scrollByKey(key(KP::endKey)));
      expectEquals(p.s.getViewPositionY(), 1700);
      expect(p.s.scrollByKey(key(KP::pageUpKey)));
      expectEquals(p.s.getViewPositionY(), 1700 - (300 - DragScroller::kKeyLineStep));
      expect(p.s.scrollByKey(key(KP::homeKey)));
      expectEquals(p.s.getViewPositionY(), 0);
    }

    beginTest("a key at an end is still spent, and never hands the page's keys on");
    {
      Page p;
      expect(p.s.scrollByKey(key(KP::upKey)));  // already at the top
      expectEquals(p.s.getViewPositionY(), 0);
      p.s.scrollByKey(key(KP::endKey));
      expect(p.s.scrollByKey(key(KP::downKey)));
      expectEquals(p.s.getViewPositionY(), 1700);
    }

    beginTest("cross-axis arrows, modified arrows and other keys are not the page's");
    {
      Page p;
      expect(!p.s.scrollByKey(key(KP::leftKey)));
      expect(!p.s.scrollByKey(key(KP::rightKey)));
      expect(!p.s.scrollByKey(key(KP::downKey, juce::ModifierKeys::shiftModifier)));
      expect(!p.s.scrollByKey(key(KP::spaceKey)));
      expect(!p.s.scrollByKey(key(KP::returnKey)));
      expectEquals(p.s.getViewPositionY(), 0);
    }

    beginTest("a page that fits takes no keys (they stay with the host)");
    {
      Page p;
      p.content.setSize(400, 200);
      expect(!p.s.scrollByKey(key(KP::downKey)));
      expect(!p.s.scrollByKey(key(KP::endKey)));
    }

    beginTest("reveal scrolls the least that shows a control with its margin, and not at all when it shows");
    {
      Page p;
      juce::Component control;
      p.content.addAndMakeVisible(control);
      control.setBounds(20, 1000, 200, 40);
      p.s.reveal(control, 48);  // below the view: its bottom edge lands 48px above the view's
      expectEquals(p.s.getViewPositionY(), 1040 + 48 - 300);
      const int settled = p.s.getViewPositionY();
      p.s.reveal(control, 48);  // in view: stays put
      expectEquals(p.s.getViewPositionY(), settled);
      p.s.scrollByKey(key(KP::endKey));
      p.s.reveal(control, 48);  // above the view: its top lands 48px below the view's
      expectEquals(p.s.getViewPositionY(), 1000 - 48);
      control.setBounds(20, 1000, 200, 400);  // taller than the view: its top wins
      p.s.scrollByKey(key(KP::endKey));
      p.s.reveal(control, 48);
      expectEquals(p.s.getViewPositionY(), 1000 - 48);
      juce::Component elsewhere;  // not on the page: ignored
      elsewhere.setBounds(0, 0, 10, 10);
      p.s.reveal(elsewhere, 48);
      expectEquals(p.s.getViewPositionY(), 1000 - 48);
    }

    beginTest("reveal of a control inside a nested scroller aims at the part of it that scroller shows");
    {
      Page p;
      // A 100px-tall lane at y=1000 on the page, showing one 100px slice of a 1000px strip.
      juce::Viewport lane;
      juce::Component strip, control;
      p.content.addAndMakeVisible(lane);
      lane.setBounds(0, 1000, 400, 100);
      strip.setSize(400, 1000);
      lane.setViewedComponent(&strip, false);
      strip.addAndMakeVisible(control);
      control.setBounds(0, 900, 400, 40);  // the lane has scrolled it out of its view
      p.s.reveal(control, 0);
      expectEquals(p.s.getViewPositionY(), 1100 - 300);  // the lane's frame, not the control's far-off spot
      lane.setViewPosition(0, 900);  // now the lane shows it, at the lane's top
      p.s.scrollByKey(key(KP::homeKey));
      p.s.reveal(control, 0);
      expectEquals(p.s.getViewPositionY(), 1040 - 300);
    }

    beginTest("a menu's list takes no keys itself: its owner walks the rows");
    {
      DragScroller list{DragScroller::Axis::vertical, DragScroller::Keys::none};
      juce::Component rows;
      rows.setSize(200, 2000);
      list.setSize(200, 300);
      list.setViewedComponent(&rows, false);
      expect(!list.keyPressed(key(KP::downKey)));
      expectEquals(list.getViewPositionY(), 0);
      expect(list.scrollByKey(key(KP::downKey)));  // the primitive is still there for an owner that wants it
      expectEquals(list.getViewPositionY(), DragScroller::kKeyLineStep);
      Page page;
      expect(page.s.keyPressed(key(KP::downKey)));  // a page's keys are its own
      expectEquals(page.s.getViewPositionY(), DragScroller::kKeyLineStep);
    }

    beginTest("panning() is false for a control at rest, inside a scroller or not");
    {
      Page p;
      juce::Component control;
      p.content.addAndMakeVisible(control);
      expect(!DragScroller::panning(control));
      juce::Component loose;
      expect(!DragScroller::panning(loose));
    }
  }
};

struct ToneModelTests : juce::UnitTest {
  ToneModelTests() : juce::UnitTest("Tone", "ui") {}
  void runTest() override {
    beginTest("overview extracts stated facts and keeps original creator copy");
    const auto described = Tone::parse(juce::JSON::parse(
        R"({"id":999,"gear":"amp","description":"Subscribe to my newsletter!\nUse: crunchy rhythm guitar\nAMP SETTINGS:\nBass: 2.5\nTreble: 7\n\nIncludes: preamp only; no cabinet"})"));
    const auto overview = toneOverview(described, "Crunch B2.5 T7");
    expect(overview.contains("Use: crunchy rhythm guitar"));
    expect(overview.contains("Bass: 2.5; Treble: 7"));
    expect(overview.contains("Includes: preamp only; no cabinet"));
    expect(overview.contains("Selected model: Crunch B2.5 T7"));
    expect(!overview.contains("newsletter"));
    expect(described.description.contains("newsletter"));
    expectEquals(juce::JSON::parse(described.toJson())["description"].toString(), described.description);

    beginTest("missing settings are never inferred from model name or training epochs");
    const auto missing = Tone::parse(juce::JSON::parse(
        R"({"id":998,"gear":"pedal","description":"Epochs: 1000\nBuy the full pack!"})"));
    expect(toneOverview(missing, "Gain 7").contains("Settings: Not supplied."));
    expect(!toneOverview(missing).contains("1000"));

    beginTest("factory summaries do not replace the original description");
    const auto factory = Tone::parse(juce::JSON::parse(
        R"({"id":45026,"description":"Original Mesa creator text"})"));
    expectEquals(factory.description, juce::String("Original Mesa creator text"));
    expect(toneOverview(factory).contains("preamp only"));

    beginTest("Info reads the original description to a screen reader; layout is upstream's");
    BlockInfoPanel panel;
    BlockInfoPanel::State info;
    info.authenticated = true;
    info.tone = described;
    info.pageUrl = "https://www.tone3000.com/tones/999";
    panel.setState(info);
    panel.setSize(500, panel.heightFor(500));
    int readouts = 0;
    for (auto* child : panel.getChildren()) {
      expect(dynamic_cast<Paragraph*>(child) == nullptr, "no overview paragraph (upstream layout)");
      if (auto* button = dynamic_cast<juce::Button*>(child))
        expect(button->getButtonText() != "More info", "no More info button (upstream layout)");
      auto handler = child->createAccessibilityHandler();
      if (child->isVisible() && handler && handler->getRole() == juce::AccessibilityRole::staticText &&
          handler->getTitle() == "Description") {
        ++readouts;
        // The whole creator text, as content (description), not a value.
        expect(handler->getDescription().contains("newsletter"));
        expect(handler->getValueInterface() == nullptr);
        expect(child->getWantsKeyboardFocus());
        expect(!child->getMouseClickGrabsKeyboardFocus());
        bool self = true, children = true;
        child->getInterceptsMouseClicks(self, children);
        expect(!self && !children, "the readout lets the mouse through");
      }
    }
    expectEquals(readouts, 1);

    beginTest("withModels patches the parsed list and the raw JSON alike");
    const auto tone = Tone::parse(juce::JSON::parse(
        R"({"id":1,"title":"T","gear":"amp","models_count":2,"a2_models_count":2,"models":[]})"));
    const auto m1 = Model::parse(juce::JSON::parse(R"({"id":10,"name":"Clean","architecture":2})"));
    const auto m2 = Model::parse(juce::JSON::parse(R"({"id":11,"name":"Lead","architecture":2})"));
    const auto patched = tone.withModels({m1, m2});
    expectEquals(static_cast<int>(patched.models.size()), 2);
    expectEquals(patched.models[1].name, juce::String("Lead"));
    const auto round = juce::JSON::parse(patched.toJson());
    expectEquals(round["models"].size(), 2);
    expectEquals(static_cast<int>(round["models"][0]["id"]), 10);
    expectEquals(static_cast<int>(tone.models.size()), 0);  // the source is untouched

    beginTest("the creator's display name is only ever a verified creator's");
    const auto verified = User::parse(juce::JSON::parse(
        R"({"id":7,"username":"amalgamaudio","display_name":"Amalgam Audio","is_verified":true})"));
    expect(verified.isVerified);
    expectEquals(verified.name(), juce::String("Amalgam Audio"));
    const auto plain = User::parse(juce::JSON::parse(R"({"id":8,"username":"staas","display_name":null})"));
    expect(!plain.isVerified);
    expectEquals(plain.name(), juce::String("staas"));
  }
};

struct ReadoutTests : juce::UnitTest {
  ReadoutTests() : juce::UnitTest("Readouts", "ui") {}
  void runTest() override {
    beginTest("toFixed is JavaScript's: fixed places, none at 0");
    expectEquals(labels::toFixed(38.4, 0), juce::String("38"));
    expectEquals(labels::toFixed(38.5, 0), juce::String("39"));
    expectEquals(labels::toFixed(-3.26, 1), juce::String("-3.3"));
    expectEquals(labels::toFixed(5.0, 1), juce::String("5.0"));
    expectEquals(labels::toFixed(48.0, 0), juce::String("48"));

    beginTest("knob readouts match the web's scales");
    expectEquals(scales::percent().format(0.384), juce::String("38 %"));
    expectEquals(scales::percent().editText(0.384), juce::String("38"));
    expectEquals(scales::gainDb().format(0.5), juce::String("0.0 dB"));
    expectEquals(scales::gainDb().format(0.0), juce::String("-24.0 dB"));
    expectEquals(scales::gateDb().format(0.333), juce::String("-67 dB"));
    expectEquals(scales::tone().format(0.5), juce::String("5.0"));

    beginTest("gate deck readouts mirror the processor's real-unit ranges");
    expectEquals(scales::gateReleaseMs().format(0.0), juce::String("5 ms"));
    expectEquals(scales::gateReleaseMs().format(1.0), juce::String("500 ms"));
    // The defaults (50 ms, 20 ms) land on round normalised values: the
    // MockBackend's seeds and the deck's reset must agree with these.
    expectEquals(scales::gateReleaseMs().format(0.5), juce::String("50 ms"));
    expectWithinAbsoluteError(scales::gateReleaseMs().fromDisplay(50), 0.5, 1e-6);
    expectEquals(scales::gateReleaseMs().format(scales::gateReleaseMs().fromDisplay(100)),
                 juce::String("100 ms"));
    expectEquals(scales::gateHoldMs().format(0.1), juce::String("20 ms"));
    expectEquals(scales::gateHoldMs().format(0.0), juce::String("0 ms"));
    expectEquals(scales::gateRangeDb().format(1.0), juce::String("80 dB"));
    expectEquals(scales::gateRangeDb().format(0.0), juce::String("20 dB"));

    beginTest("pitch readouts mirror the processor's ranges");
    // Semitones, signed, ±24; the centre is 0 (the MockBackend seed). Whole
    // values (STEP on) read whole, anything else to a tenth.
    expectEquals(scales::semitones().format(0.5), juce::String("0 st"));
    expectEquals(scales::semitones().format(0.0), juce::String("-24 st"));
    expectEquals(scales::semitones().format(1.0), juce::String("+24 st"));
    expectEquals(scales::semitones().format(scales::semitones().fromDisplay(-2)), juce::String("-2 st"));
    expectEquals(scales::semitones().format(scales::semitones().fromDisplay(2.5)), juce::String("+2.5 st"));
    expectEquals(scales::semitones().format(scales::semitones().fromDisplay(-0.3)), juce::String("-0.3 st"));
    expectEquals(scales::semitones().editText(0.5), juce::String("0"));
    expectEquals(scales::semitones().editText(scales::semitones().fromDisplay(2.5)), juce::String("2.5"));
    // Tonality: log 1-20 kHz, the top end reads Off (the default seed).
    expectEquals(scales::tonalityHz().format(1.0), juce::String("Off"));
    expectEquals(scales::tonalityHz().format(0.0), juce::String("1.0 kHz"));
    expectWithinAbsoluteError(scales::tonalityHz().toDisplay(scales::tonalityHz().fromDisplay(8000)), 8000.0,
                              1e-6);
    // Buffer: four detents read as the buffer size; typed values snap to
    // the nearest.
    expectEquals(scales::bufferMs().format(0.0), juce::String("20 ms"));
    expectEquals(scales::bufferMs().format(1.0 / 3), juce::String("30 ms"));
    expectEquals(scales::bufferMs().format(2.0 / 3), juce::String("40 ms"));
    expectEquals(scales::bufferMs().format(1.0), juce::String("60 ms"));
    expectWithinAbsoluteError(scales::bufferMs().fromDisplay(28), 1.0 / 3, 1e-6);
    expectWithinAbsoluteError(scales::bufferMs().fromDisplay(55), 1.0, 1e-6);
    expectEquals(scales::offsetMs().format(0.5), juce::String("0 ms"));
    expectEquals(scales::offsetMs().format(0.25), juce::String("12.0 ms L"));
    expectEquals(scales::crossoverHz().format(0.5), juce::String("130 Hz"));
    expectEquals(scales::pan(true).format(0.5), juce::String("C"));
    expectEquals(scales::pan(true).format(0.0), juce::String("100L"));
    expectEquals(scales::pan(false).format(0.75), juce::String("50R"));
  }
};

struct ToneQueryTests : juce::UnitTest {
  ToneQueryTests() : juce::UnitTest("ToneQuery", "ui") {}
  void runTest() override {
    beginTest("an empty query is the trending catalog page, scoped to the plugin's architecture");
    ToneQuery q;
    expectEquals(q.requestPath(1, 12, 2), juce::String("/api/v1/tones/search?page=1&page_size=12&architecture=2"));
    expect(q.effectiveSort() == ToneSort::trending);
    expect(!q.hasAdvancedFilters());

    beginTest("text defaults the sort to best match; an explicit sort wins and counts as a filter");
    q.text = " fender twin ";
    expect(q.effectiveSort() == ToneSort::bestMatch);
    expect(q.requestPath(2, 12, 2).contains("query=fender%20twin&"));
    expect(!q.requestPath(2, 12, 2).contains("sort="));
    q.sort = ToneSort::popular;
    expect(q.effectiveSort() == ToneSort::popular);
    expect(q.requestPath(2, 12, 2).contains("&sort=downloads-all-time&"));
    expect(q.hasAdvancedFilters());

    beginTest("picking the default sort is no pick; an explicit one stops being explicit when the text makes it the default");
    q = {};
    q.setSort(ToneSort::trending);
    expect(!q.sort.has_value());
    expect(!q.sortIsExplicit() && !q.hasAdvancedFilters());
    q.text = "vox";
    q.setSort(ToneSort::trending);  // now explicit: best match is the default
    expect(q.sort.has_value() && q.sortIsExplicit());
    q.text.clear();
    expect(q.effectiveSort() == ToneSort::trending);
    expect(!q.sortIsExplicit() && !q.hasAdvancedFilters());

    beginTest("list filters use the API's separators with each name escaped on its own");
    q = {};
    q.gear = "amp-cab";
    q.tags = {"metal", "high gain"};
    q.makes = {"Fender Twin Reverb", "1965 Vox AC30"};
    q.creators = {"tone3000", "amalgam_audio"};
    q.calibrated = true;
    q.verified = true;
    const auto path = q.requestPath(1, 12, 2);
    expect(path.contains("&gears=amp-cab&"));
    expect(path.contains("&tags=metal_high%20gain&"));
    expect(path.contains("&makes=Fender%20Twin%20Reverb_1965%20Vox%20AC30&"));
    expect(path.contains("&creators=tone3000,amalgam_audio&"));
    expect(path.contains("&calibrated=true&verified=true&architecture=2"));

    beginTest("calibrated is parked, not sent, under IR gear or the IR format; it comes back with amps");
    q = {};
    q.calibrated = true;
    expect(q.calibratedInForce() && q.hasAdvancedFilters());
    for (const char* ir : {"cab", "space"}) {
      q.gear = ir;
      expect(!q.calibratedApplies() && !q.calibratedInForce() && !q.hasAdvancedFilters());
      expect(!q.requestPath(1, 12, 2).contains("calibrated"));
    }
    q.gear = "amp";
    expect(q.calibratedInForce() && q.requestPath(1, 12, 2).contains("&calibrated=true"));
    q.format = "ir";
    expect(!q.calibratedApplies() && !q.requestPath(1, 12, 2).contains("calibrated"));

    beginTest("the architecture rides along with every format (the API ignores it for IR); < 0 omits it");
    q = {};
    q.format = "ir";
    expectEquals(q.requestPath(1, 12, 2), juce::String("/api/v1/tones/search?page=1&page_size=12&format=ir&architecture=2"));
    q.format = "nam";
    expect(q.requestPath(1, 12, 2).endsWith("&format=nam&architecture=2"));
    expect(q.requestPath(1, 12, -1).endsWith("&format=nam"));

    beginTest("a profile filter pages the user's own stream, by title search and gear alone");
    q = {};
    q.text = " plexi ";
    q.tags = {"metal"};
    q.verified = true;
    q.gear = "pedal";
    q.profile = Profile::favorited;
    expectEquals(q.requestPath(3, 12, 2),
                 juce::String("/api/v1/tones/favorited?page=3&page_size=12&query=plexi&gear=pedal"));
    q.profile = Profile::downloaded;
    q.text.clear();
    q.gear.clear();
    expectEquals(q.requestPath(1, 12, 2), juce::String("/api/v1/tones/downloaded?page=1&page_size=12"));

    beginTest("a paginated payload parses to a page");
    const auto page = TonePage::parse(juce::JSON::parse(
        R"({"data":[{"id":1,"title":"A"},{"id":2,"title":"B"}],"page":2,"page_size":2,"total":5,"total_pages":3})"));
    expectEquals(static_cast<int>(page.data.size()), 2);
    expectEquals(page.page, 2);
    expectEquals(page.totalPages, 3);
  }
};

// The keyboard / screen-reader contract every control signs up to.
struct AccessibilityTests : juce::UnitTest {
  AccessibilityTests() : juce::UnitTest("Accessibility", "ui") {}
  void runTest() override {
    beginTest("help::lead names a control from its hint");
    expectEquals(help::lead("Undo: step back through chain edits."), juce::String("Undo"));
    expectEquals(help::lead("Clear"), juce::String("Clear"));
    expectEquals(help::lead(juce::String()), juce::String());
    expectEquals(help::lead(": odd"), juce::String(": odd"));

    beginTest("buttons take focus from Tab, not from clicks (upstream)");
    IconButton icon(Icon::X);
    expect(icon.getWantsKeyboardFocus());
    expect(!icon.getMouseClickGrabsKeyboardFocus());

    beginTest("a button's name: title, else text, else the hint lead");
    icon.setHelpText(help::text(help::Key::undo));
    expectEquals(icon.accessibleName(), help::lead(help::text(help::Key::undo)));
    expectEquals(icon.createAccessibilityHandler()->getTitle(), icon.accessibleName());
    // The hint bar shows the whole hint; a screen reader gets it without the
    // name it opens with, having just read the name.
    expectEquals(icon.getHelpText(), help::text(help::Key::undo));
    expectEquals(icon.createAccessibilityHandler()->getHelp(),
                 help::spokenHelp(icon.accessibleName(), help::text(help::Key::undo)));
    expect(!icon.createAccessibilityHandler()->getHelp().startsWith(icon.accessibleName()));
    ChromeTextButton save("Save", help::Key::presetSave);
    expectEquals(save.accessibleName(), juce::String("Save"));
    save.setTitle("Save preset");
    expectEquals(save.accessibleName(), juce::String("Save preset"));

    beginTest("a chip's name follows its label; Backspace is its x");
    FilterChip chip("Sort");
    chip.setLabel("Newest");
    expectEquals(chip.accessibleName(), juce::String("Newest"));
    int cleared = 0;
    chip.onClear = [&] { ++cleared; };
    chip.setTrailing(FilterChip::Trailing::clear);
    expect(chip.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    expectEquals(cleared, 1);
    chip.setTrailing(FilterChip::Trailing::chevron);
    expect(!chip.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    expectEquals(cleared, 1);

    beginTest("a knob is a Tab stop and a slider; arrows step it, Space falls through");
    Knob::Options options;
    options.label = "Input";
    options.scale = &scales::gainDb();
    options.help = help::Key::inputLevel;
    Knob knob(options);
    expect(knob.getWantsKeyboardFocus());
    expect(!knob.getMouseClickGrabsKeyboardFocus());  // a click never focuses (upstream)
    expectEquals(knob.getTitle(), juce::String("Input"));
    knob.setValue(0.5f);
    std::vector<float> emitted;
    int gestures = 0;
    knob.onChange = [&](float v) { emitted.push_back(v); };
    knob.onDragStateChange = [&](bool down) { gestures += down ? 1 : -1; };
    expect(knob.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    expectEquals(static_cast<int>(emitted.size()), 1);
    expectWithinAbsoluteError(emitted.back(), 0.51f, 1e-5f);
    expectEquals(gestures, 0);  // begin and end bracketed the step
    expect(knob.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    expectWithinAbsoluteError(emitted.back(), 0.51f - 0.01f / 8, 1e-5f);
    expect(knob.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    expectWithinAbsoluteError(emitted.back(), 1.0f, 1e-6f);
    expect(!knob.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    auto handler = knob.createAccessibilityHandler();
    expect(handler->getRole() == juce::AccessibilityRole::slider);
    expectEquals(handler->getValueInterface()->getCurrentValueAsString(), juce::String("24.0 dB"));
    handler->getValueInterface()->setValue(0.0);
    expectWithinAbsoluteError(knob.value(), 0.5f, 1e-6f);

    beginTest("the paginator turns pages with the arrows");
    Paginator pages;
    pages.set(2, 5);
    int turnedTo = 0;
    pages.onPageChange = [&](int p) { turnedTo = p; };
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    expectEquals(turnedTo, 3);
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    expectEquals(turnedTo, 1);
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    expectEquals(turnedTo, 3);
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    expectEquals(turnedTo, 1);
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    expectEquals(turnedTo, 5);
    expect(pages.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    expectEquals(turnedTo, 1);
    expectEquals(pages.createAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(),
                 juce::String("Page 2 of 5"));

    beginTest("a pill toggle reads as checked when on");
    PillToggle toggle;
    expect(!toggle.createAccessibilityHandler()->getCurrentState().isChecked());
    toggle.setValue(true, /*animate=*/false);
    expect(toggle.createAccessibilityHandler()->getCurrentState().isChecked());

    beginTest("decoration stays out of the accessibility tree");
    expect(!Avatar().isAccessible());
    expect(!FormatBadge().isAccessible());

    beginTest("text and segmented toggles expose their actual states");
    ChromeTextButton pre("PRE", help::Key::eqPre);
    pre.setToggleable(true);
    pre.setArmed(false);
    auto preHandler = pre.createAccessibilityHandler();
    expect(preHandler->getRole() == juce::AccessibilityRole::toggleButton);
    expect(!preHandler->getCurrentState().isChecked());
    pre.setArmed(true);
    expect(preHandler->getCurrentState().isChecked());
    pre.setArmed(false);
    expect(!preHandler->getCurrentState().isChecked());
    SegmentedText segments({{"Solo", "Solo this chain", nullptr, 16}, {"Invert", "Invert polarity", nullptr, 16}}, SegmentedText::armed());
    auto soloHandler = segments.getChildComponent(0)->createAccessibilityHandler();
    auto invertHandler = segments.getChildComponent(1)->createAccessibilityHandler();
    segments.setOn(1, true);
    expect(!soloHandler->getCurrentState().isChecked());
    expect(invertHandler->getCurrentState().isChecked());
    segments.setOn(1, false);
    expect(!invertHandler->getCurrentState().isChecked());

    beginTest("power buttons expose their initial and changed states and a toggle action");
    ChromeIconButton power(Icon::Power, ChromeIconButton::Tone::power, help::Key::blockPower);
    auto powerHandler = power.createAccessibilityHandler();
    expect(powerHandler->getCurrentState().isCheckable());
    expect(powerHandler->getCurrentState().isChecked());
    power.setOn(false);
    expect(!powerHandler->getCurrentState().isChecked());
    power.onClick = [&] { power.setOn(!power.isOn()); };
    expect(powerHandler->getActions().invoke(juce::AccessibilityActionType::toggle));
    drive::wait(150);
    expect(power.isOn());
    expect(powerHandler->getCurrentState().isChecked());
    expect(!icon.createAccessibilityHandler()->getActions().contains(juce::AccessibilityActionType::toggle));
  }
};

// The focus policy end to end, in a real window: keys and presses enter
// through the peer, as the OS delivers them, so JUCE's own focus plumbing
// (click-to-focus, the tab walk, listeners) is what is under test.
struct FocusPolicyTests : juce::UnitTest {
  FocusPolicyTests() : juce::UnitTest("Focus policy", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
  // The focused component when it is inside the UI; the window itself
  // holding focus (a DocumentWindow does once the OS activates it) is
  // nothing focused as far as the UI is concerned.
  static juce::Component* focused() {
    auto* f = juce::Component::getCurrentlyFocusedComponent();
    return f != nullptr && dynamic_cast<juce::TopLevelWindow*>(f) == nullptr ? f : nullptr;
  }
  static bool key(juce::ComponentPeer& peer, int code, juce::ModifierKeys mods = {}) {
    return peer.handleKeyPress(juce::KeyPress(code, mods, 0));
  }
  // A primary click at the component's centre, through the peer.
  static void click(juce::ComponentPeer& peer, juce::Component& target) {
    const auto pos = peer.getComponent().getLocalPoint(&target, target.getLocalBounds().getCentre().toFloat());
    const auto now = juce::Time::currentTimeMillis();
    using Type = juce::MouseInputSource::InputSourceType;
    peer.handleMouseEvent(Type::mouse, pos, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, now);
    peer.handleMouseEvent(Type::mouse, pos, juce::ModifierKeys(), 0.0f, 0.0f, now + 1);
    pump(30);
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");  // stereo input: the Input Mode menu button shows
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("focus policy", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    if (peer == nullptr) {
      expect(false, "no window peer");
      return;
    }
    // JUCE grants keyboard focus only once the OS has focused the window;
    // a run from a terminal or CI may not be allowed to take it.
    juce::Process::makeForegroundProcess();
    window.toFront(true);
    peer->grabFocus();
    for (int i = 0; i < 20 && !peer->isFocused(); ++i) pump(50);
    if (!peer->isFocused()) {
      logMessage("the window could not take OS focus here; focus policy not exercised");
      return;
    }
    auto& root = host.pluginRoot();

    beginTest("nothing is focused until the keyboard asks");
    expect(focused() == nullptr);

    beginTest("Tab from nothing enters the order; Escape leaves it");
    expect(key(*peer, juce::KeyPress::tabKey));
    expect(focused() != nullptr && root.isParentOf(focused()));
    auto* first = focused();
    expect(key(*peer, juce::KeyPress::tabKey));
    expect(focused() != nullptr && focused() != first);
    expect(key(*peer, juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier));
    expect(focused() == first);
    expect(key(*peer, juce::KeyPress::escapeKey));
    expect(focused() == nullptr);

    beginTest("Space with a Tab-focused button is left for the host");
    expect(key(*peer, juce::KeyPress::tabKey));
    expect(dynamic_cast<Clickable*>(focused()) != nullptr);
    expect(!key(*peer, juce::KeyPress::spaceKey));
    expect(!key(*peer, juce::KeyPress::spaceKey));  // and with nothing focused
    key(*peer, juce::KeyPress::escapeKey);

    beginTest("a click never focuses a knob (upstream); keyboard focus keeps it; mouse menus do not focus their button");
    auto* knob = dynamic_cast<Knob*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.isShowing() && c.isEnabled();
    }));
    auto* inputMode = drive::byHelpPrefix(root, "Input Mode:");
    expect(knob != nullptr && inputMode != nullptr);
    if (knob == nullptr || inputMode == nullptr) return;
    expect(key(*peer, juce::KeyPress::tabKey));
    expect(focused() != nullptr);
    click(*peer, *knob);
    expect(focused() != knob, "a click does not focus the knob");
    knob->grabKeyboardFocus();  // as Tab would
    expect(focused() == knob);
    expect(key(*peer, juce::KeyPress::rightKey));
    expect(focused() == knob);
    knob->getAccessibilityHandler()->getValueInterface()->setValue(0.0);
    expect(focused() == knob, "Screen-reader value changes keep the slider focused");
    click(*peer, *inputMode);  // opens its menu from the mouse
    pump(30);
    expect(focused() != inputMode, "a click does not focus the menu button");
    key(*peer, juce::KeyPress::escapeKey);
    pump(30);
    expect(focused() != inputMode, "a mouse-opened menu does not hand focus to its button");
    if (auto* open = drive::find(root, [](juce::Component& c) {
          auto* p = dynamic_cast<Popover*>(&c);
          return p != nullptr && p->isOpen();
        }))
      dynamic_cast<Popover*>(open)->dismiss();
    pump(30);

    beginTest("a keyboard-opened menu: arrows walk the rows, Escape returns to the anchor");
    inputMode->grabKeyboardFocus();
    expect(focused() == inputMode);
    expect(key(*peer, juce::KeyPress::returnKey));  // Enter presses the button
    // The press is a posted message, and a slow paint ahead of it outlasts a fixed pump.
    for (int i = 0; i < 300 && dynamic_cast<Popover*>(focused()) == nullptr; ++i) pump(10);
    auto* menu = dynamic_cast<Popover*>(focused());
    expect(menu != nullptr);
    if (menu != nullptr) {
      expect(key(*peer, juce::KeyPress::downKey));
      expect(focused() != menu && menu->isParentOf(focused()));
      auto* row = focused();
      expect(key(*peer, juce::KeyPress::downKey));
      expect(focused() != row && menu->isParentOf(focused()));
      expect(key(*peer, juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier));
      expect(focused() == row);
      expect(key(*peer, juce::KeyPress::escapeKey));
      pump(30);
      expect(!menu->isOpen());
      expect(focused() == inputMode);
    }
    key(*peer, juce::KeyPress::escapeKey);
    expect(focused() == nullptr);
    window.setVisible(false);
  }
};

// A scenario in a real window that has OS focus, for tests that drive
// input through the peer as the OS delivers it. `ok` is false where the
// window could not take focus (a run from a terminal or CI may not be
// allowed to); the test logs that and returns.
struct LiveScenario {
  LiveScenario(juce::UnitTest& test, const juce::String& scenarioId)
      : fixtures(Fixtures::load(fixturesDir().getChildFile("scenarios.json"))), scenario(fixtures.find(scenarioId)) {
    if (scenario == nullptr) {
      test.expect(false, scenarioId + " scenario missing");
      return;
    }
    backend = std::make_unique<MockBackend>(scenario->data);
    window = std::make_unique<juce::DocumentWindow>(scenarioId, juce::Colours::black, 0);
    host = std::make_unique<ScaledHost>(*backend, *scenario, fixtures.root);
    window->setContentNonOwned(host.get(), true);
    showTestWindow(*window);
    pump(400);
    peer = host->getPeer();
    if (peer == nullptr) {
      test.expect(false, "no window peer");
      return;
    }
    // JUCE grants keyboard focus only once the OS has focused the window.
    juce::Process::makeForegroundProcess();
    window->toFront(true);
    peer->grabFocus();
    for (int i = 0; i < 20 && !peer->isFocused(); ++i) pump(50);
    ok = peer->isFocused();
    if (!ok) test.logMessage("the window could not take OS focus here; " + scenarioId + " not exercised");
  }
  ~LiveScenario() {
    if (window != nullptr) window->setVisible(false);
  }

  PluginRoot& root() { return host->pluginRoot(); }
  // The scenario's own drive (clicks that open its screen), then its settle.
  void drive() {
    if (const auto* d = driveFor(scenario->id)) (*d)(root(), *backend);
    pump(scenario->settleMs());
  }

  static void pump(int ms) { FocusPolicyTests::pump(ms); }
  // Pump until `ready` holds, up to a deadline; whether it does. For what
  // the UI does on a later message (a focus callback, a store's reply): a
  // fixed pump is too short on a loaded CI machine.
  static bool until(const std::function<bool()>& ready, int deadlineMs = 3000) {
    for (int waited = 0; !ready(); waited += 10) {
      if (waited >= deadlineMs) return false;
      pump(10);
    }
    return true;
  }
  static juce::Component* focused() { return FocusPolicyTests::focused(); }
  bool key(int code, juce::ModifierKeys mods = {}) { return FocusPolicyTests::key(*peer, code, mods); }
  // Focus a control and wait for the Desktop to tell its listeners (the
  // scrollers that follow focus). The Desktop posts that; on a CI machine a
  // paint can hold the queue far longer than a fixed pump, so wait for it.
  static void focus(juce::Component& target) {
    struct Watch : juce::FocusChangeListener {
      bool fired = false;
      void globalFocusChanged(juce::Component*) override { fired = true; }
    } watch;
    if (target.hasKeyboardFocus(false)) return;  // no change, so nothing to wait for
    juce::Desktop::getInstance().addFocusChangeListener(&watch);
    target.grabKeyboardFocus();
    for (int i = 0; i < 300 && !watch.fired; ++i) pump(10);
    juce::Desktop::getInstance().removeFocusChangeListener(&watch);
  }
  // A point of `target` in the peer's space.
  juce::Point<float> at(juce::Component& target, juce::Point<float> local) {
    return peer->getComponent().getLocalPoint(&target, local);
  }
  juce::Point<float> centre(juce::Component& target) { return at(target, target.getLocalBounds().getCentre().toFloat()); }
  // A wheel turn (a trackpad gesture is a run of these) at a point.
  void wheel(juce::Point<float> pos, float deltaX, float deltaY) {
    using Type = juce::MouseInputSource::InputSourceType;
    peer->handleMouseWheel(Type::mouse, pos, juce::Time::currentTimeMillis(), {deltaX, deltaY, false, false, false});
    pump(10);
  }

  // One pointer (mouse, or finger 1 where the platform has touch sources)
  // moving through the peer.
  struct Pointer {
    LiveScenario& live;
    juce::MouseInputSource::InputSourceType type = juce::MouseInputSource::InputSourceType::mouse;
    juce::int64 time = juce::Time::currentTimeMillis();
    void at(juce::Point<float> pos, bool down) {
      const bool touch = type == juce::MouseInputSource::InputSourceType::touch;
      live.peer->handleMouseEvent(type, pos, down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f,
                                  0.0f, ++time, {}, touch ? 1 : 0);
      pump(10);
    }
    // Press at `from`, move by `delta` in `steps`, release.
    void drag(juce::Point<float> from, juce::Point<float> delta, int steps = 8) {
      at(from, true);
      for (int i = 1; i <= steps; ++i) at(from + delta * (static_cast<float>(i) / static_cast<float>(steps)), true);
      at(from + delta, false);
    }
    void click(juce::Point<float> pos) {
      at(pos, true);
      at(pos, false);
    }
  };
  Pointer mouse() { return Pointer{*this}; }
  // A finger, or nothing where the platform has no touch input source
  // (macOS): the first touch event would add one to the desktop's list.
  std::optional<Pointer> finger() {
    Pointer p{*this, juce::MouseInputSource::InputSourceType::touch};
    p.at(centre(root()).translated(0, -1000.0f), false);  // a move well off the UI
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)  // the source stays once a platform has made it
      if (desktop.getMouseSource(i)->getType() == juce::MouseInputSource::InputSourceType::touch) return p;
    return std::nullopt;
  }

  Fixtures fixtures;
  const Scenario* scenario = nullptr;
  std::unique_ptr<MockBackend> backend;
  std::unique_ptr<juce::DocumentWindow> window;
  std::unique_ptr<ScaledHost> host;
  juce::ComponentPeer* peer = nullptr;
  bool ok = false;
};

struct MenuTunerAccessibilityTests : juce::UnitTest {
  MenuTunerAccessibilityTests() : juce::UnitTest("Menu and tuner accessibility", "ui") {}
  void runTest() override {
    beginTest("sliders in menus navigate vertically and adjust horizontally");
    LiveScenario live(*this, "chrome-spread-on");
    expect(live.ok);
    if (!live.ok) return;
    Popover menu;
    Knob first(Knob::Options{}), second(Knob::Options{});
    menu.addAndMakeVisible(first); menu.addAndMakeVisible(second);
    first.setBounds(0, 0, 60, 60); second.setBounds(70, 0, 60, 60);
    menu.setSize(140, 80); menu.open(*live.root().getChildComponent(0), Popover::Align::left, 0);
    LiveScenario::focus(first);
    const float before = first.value();
    expect(first.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    expect(LiveScenario::until([&] { return second.hasKeyboardFocus(false); }), "Down focuses second slider");
    expectEquals(first.value(), before);
    expect(second.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    expect(LiveScenario::until([&] { return first.hasKeyboardFocus(false); }), "Up focuses first slider");
    expect(first.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    expect(first.value() > before);
    menu.close();
    beginTest("tuner exposes live readings, focus and close shortcut");
    live.backend->setTuner(juce::JSON::parse("{\"frequency\":440,\"confidence\":1}"));
    live.root().setTunerShown(true);
    LiveScenario::pump(200);
    auto* tuner = dynamic_cast<TunerView*>(drive::find(live.root(), [](juce::Component& c) { return dynamic_cast<TunerView*>(&c) != nullptr; }));
    expect(tuner != nullptr);
    if (tuner == nullptr) return;
    expect(tuner->hasKeyboardFocus(false));
    auto handler = tuner->createAccessibilityHandler();
    expect(handler->getValueInterface()->isReadOnly());
    expect(tuner->accessibleReading().contains("A 4"));
    expect(tuner->accessibleReading().contains("440.0 hertz, in tune"));
    expect(tuner->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    expect(tuner->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    expect(!live.root().tunerShown());
  }
};
MenuTunerAccessibilityTests menuTunerAccessibilityTests;

struct BranchAccessibilityTests : juce::UnitTest {
  BranchAccessibilityTests() : juce::UnitTest("Branch accessibility", "ui") {}
  void runTest() override {
    beginTest("keyboard activation removes and sets routing while retaining focus");
    LiveScenario live(*this, "main-stereo-branched");
    expect(live.ok);
    if (!live.ok) return;
    auto* junction = dynamic_cast<Clickable*>(drive::byHelpPrefix(live.root(), "Branch Point:"));
    expect(junction != nullptr);
    if (junction == nullptr) return;
    const auto title = junction->getTitle();
    LiveScenario::focus(*junction);
    auto* handler = junction->getAccessibilityHandler();
    expect(handler != nullptr);
    if (handler == nullptr) return;
    expect(handler->getCurrentState().isChecked());
    handler->getActions().invoke(juce::AccessibilityActionType::press);
    expect(LiveScenario::until([&] { return !live.root().services().chain.state().branch.has_value(); }));
    auto* focused = dynamic_cast<Clickable*>(LiveScenario::focused());
    expect(focused != nullptr && focused->getTitle().startsWith("Branch after "));
    if (focused == nullptr) return;
    auto* nextHandler = focused->getAccessibilityHandler();
    expect(nextHandler != nullptr);
    if (nextHandler == nullptr) return;
    expect(!nextHandler->getCurrentState().isChecked());
    nextHandler->getActions().invoke(juce::AccessibilityActionType::press);
    expect(LiveScenario::until([&] { return live.root().services().chain.state().branch.has_value(); }));
    focused = dynamic_cast<Clickable*>(LiveScenario::focused());
    expect(focused != nullptr && focused->getTitle() == title);
  }
};
BranchAccessibilityTests branchAccessibilityTests;

struct GroupNavigationTests : juce::UnitTest {
  GroupNavigationTests() : juce::UnitTest("Group navigation", "ui") {}

  static bool press(LiveScenario& live, juce::juce_wchar c, juce::ModifierKeys mods = {}) {
    return live.peer->handleKeyPress(juce::KeyPress(c, mods, c));
  }
  static juce::String groupOfFocus(PluginRoot& root) {
    auto* group = a11y::enclosingGroup(LiveScenario::focused(), &root);
    return group != nullptr ? group->getTitle() : juce::String("(none)");
  }

  void runTest() override {
    beginTest("screen readers see controls nested in named groups");
    LiveScenario live(*this, "main-stereo");
    expect(live.ok);
    if (!live.ok) return;
    auto& root = live.root();
    auto* input = dynamic_cast<Knob*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.getTitle() == "Input" && c.isShowing();
    }));
    auto* output = dynamic_cast<Knob*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.getTitle() == "Output" && c.isShowing();
    }));
    expect(input != nullptr && output != nullptr);
    if (input == nullptr || output == nullptr) return;

    auto* handler = input->getAccessibilityHandler();
    expect(handler != nullptr);
    if (handler != nullptr) {
      auto* section = handler->getParent();
      expect(section != nullptr && section->getRole() == juce::AccessibilityRole::group);
      expect(section != nullptr && section->getTitle() == "Input");
      auto* plate = section != nullptr ? section->getParent() : nullptr;
      expect(plate != nullptr && plate->getTitle() == "Global controls");
    }

    beginTest("the faceplate's Tab order reads left to right");
    const auto order = juce::KeyboardFocusTraverser().getAllComponents(&root);
    const auto indexOf = [&](juce::Component* c) { return std::find(order.begin(), order.end(), c) - order.begin(); };
    expect(indexOf(input) < indexOf(output));
    auto* bass = drive::find(root, [](juce::Component& c) { return c.getTitle() == "Bass" && c.isShowing(); });
    expect(bass != nullptr && indexOf(input) < indexOf(bass) && indexOf(bass) < indexOf(output));

    beginTest(". and , walk the groups and wrap");
    expect(LiveScenario::focused() == nullptr);
    expect(press(live, '.'));
    expectEquals(groupOfFocus(root), juce::String("Presets"));
    juce::StringArray visited{groupOfFocus(root)};
    for (int i = 0; i < 20; ++i) {
      expect(press(live, '.'));
      if (groupOfFocus(root) == "Presets") break;
      visited.add(groupOfFocus(root));
    }
    for (auto name : {"Toolbar", "Input", "Gate", "Tone stack", "Output"})
      expect(visited.contains(name), juce::String("visits ") + name + ": " + visited.joinIntoString(", "));
    expect(visited.indexOf("Toolbar") < visited.indexOf("Input"));
    expect(visited.indexOf("Input") < visited.indexOf("Output"));
    expectEquals(groupOfFocus(root), juce::String("Presets"), "wraps to the first group");
    expect(press(live, ','));
    expectEquals(groupOfFocus(root), juce::String("Output"), ", from the first group wraps to the last");
    expect(press(live, ','));
    expectEquals(groupOfFocus(root), visited[visited.size() - 2]);

    beginTest("Shift and Ctrl leave the keys to the host");
    {
      auto* at = LiveScenario::focused();
      expect(!press(live, '>', juce::ModifierKeys::shiftModifier));
      expect(!press(live, '<', juce::ModifierKeys::shiftModifier));
      expect(!press(live, '.', juce::ModifierKeys::ctrlModifier));
      expect(LiveScenario::focused() == at);
    }
    live.key(juce::KeyPress::escapeKey);
  }
};
GroupNavigationTests groupNavigationTests;

struct ToggleContextAccessibilityTests : juce::UnitTest {
  ToggleContextAccessibilityTests() : juce::UnitTest("Toggle and context accessibility", "ui") {}
  void runTest() override {
    beginTest("effect controls mount in a focused editor");
    LiveScenario live(*this, "chrome-spread-on");
    expect(live.ok);
    if (!live.ok) return;
    // This fixture needs no drive; its effect state is already mounted.
    live.root().services().prefs.setBool(UiPrefs::kShowPitchControl, true);
    LiveScenario::pump(100);
    beginTest("effect power toggles retain focus and expose state changes");
    for (const auto* prefix : {"Gate Power:", "Pitch Power:"}) {
      auto* power = dynamic_cast<Clickable*>(drive::byHelpPrefix(live.root(), prefix));
      expect(power != nullptr);
      if (power == nullptr) continue;
      auto* handler = power->getAccessibilityHandler();
      const bool before = handler->getCurrentState().isChecked();
      expect(handler->getActions().invoke(juce::AccessibilityActionType::toggle));
      expect(LiveScenario::until([&] { return handler->getCurrentState().isChecked() != before; }));
      expect(power->hasKeyboardFocus(false));
    }
    beginTest("Spread collapsing and expanding transfers focus to the matching switch");
    auto* power = dynamic_cast<Clickable*>(drive::byHelpPrefix(live.root(), "Spread Power:"));
    expect(power != nullptr && power->isShowing());
    if (power == nullptr || !power->isShowing()) return;
    expect(power->getAccessibilityHandler()->getCurrentState().isChecked());
    expect(power->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::toggle));
    Clickable* advert = nullptr;
    expect(LiveScenario::until([&] {
      advert = dynamic_cast<Clickable*>(drive::buttonNamed(live.root(), "SPREAD"));
      return advert != nullptr && advert->hasKeyboardFocus(false);
    }));
    if (advert == nullptr) return;
    expect(!advert->getAccessibilityHandler()->getCurrentState().isChecked());
    expect(advert->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::toggle));
    expect(LiveScenario::until([&] { return power->isShowing() && power->hasKeyboardFocus(false); }));
    expect(power->getAccessibilityHandler()->getCurrentState().isChecked());

    beginTest("Shift F10 opens advanced from each power button; Escape returns to the exact opener");
    for (const auto* prefix : {"Gate Power:", "Pitch Power:", "Spread Power:"}) {
      auto* opener = drive::byHelpPrefix(live.root(), prefix);
      expect(opener != nullptr);
      if (opener == nullptr) continue;
      LiveScenario::focus(*opener);
      expect(live.key(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier));
      expect(dynamic_cast<Popover*>(LiveScenario::focused()) != nullptr);
      expect(live.key(juce::KeyPress::escapeKey));
      expect(opener->hasKeyboardFocus(false));
#if JUCE_WINDOWS
      expect(live.key(kWindowsApplicationsKey));
      expect(dynamic_cast<Popover*>(LiveScenario::focused()) != nullptr);
      expect(live.key(juce::KeyPress::escapeKey));
      expect(opener->hasKeyboardFocus(false));
#endif
    }
    beginTest("advanced also opens from knobs and returns focus to the same knob");
    for (const auto* prefix : {"Gate:", "Pitch Shift:", "Offset:"}) {
      auto* opener = drive::byHelpPrefix(live.root(), prefix);
      expect(opener != nullptr);
      if (opener == nullptr) continue;
      LiveScenario::focus(*opener);
      expect(live.key(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier));
      expect(dynamic_cast<Popover*>(LiveScenario::focused()) != nullptr);
      expect(live.key(juce::KeyPress::escapeKey));
      expect(opener->hasKeyboardFocus(false));
    }
  }
};

struct BrowserAccessibilityTests : juce::UnitTest {
  BrowserAccessibilityTests() : juce::UnitTest("Browser accessibility", "ui") {}
  void runTest() override {
    beginTest("gear filters activate from screen-reader actions and retain focus");
    LiveScenario live(*this, "browser-signed-out");
    expect(live.ok);
    if (!live.ok) return;
    live.drive();
    auto* chip = dynamic_cast<FilterChip*>(drive::find(live.root(), [](juce::Component& c) {
      auto* filter = dynamic_cast<FilterChip*>(&c);
      return filter != nullptr && filter->getButtonText() == "Amp Head" && filter->isShowing();
    }));
    expect(chip != nullptr);
    if (chip == nullptr) return;
    // The hint bar shows upstream's copy; the gear's definition is the
    // chip's spoken description.
    expectEquals(chip->getHelpText(), help::text(help::Key::browserGear));
    auto* handler = chip->getAccessibilityHandler();
    expect(handler->getDescription().contains("without a speaker cabinet"));
    expect(handler->getActions().invoke(juce::AccessibilityActionType::press));
    LiveScenario::pump(100);
    expectEquals(live.root().services().browser.query.gear, juce::String("amp"));
    expect(chip->hasKeyboardFocus(false));
    expect(handler->getCurrentState().isChecked());
    expect(LiveScenario::until([&] {
      const auto& result = live.root().services().browser.result;
      return result && std::all_of(result->data.begin(), result->data.end(), [](const Tone& tone) {
        return tone.gear == "amp";
      });
    }), "Results are filtered by gear");
    expect(handler->getActions().invoke(juce::AccessibilityActionType::press));
    LiveScenario::pump(100);
    expect(live.root().services().browser.query.gear.isEmpty());
    expect(!handler->getCurrentState().isChecked());
    expect(chip->hasKeyboardFocus(false));
  }
};

struct ToneInfoKeyboardTests : juce::UnitTest {
  ToneInfoKeyboardTests() : juce::UnitTest("Tone Info keyboard", "ui") {}
  void runTest() override {
    beginTest("Favorites toggles with Enter and keeps focus");
    {
      LiveScenario live(*this, "main-detail");
      if (live.ok) {
        auto* favorite = drive::buttonNamed(live.root(), "Bookmark");
        expect(favorite != nullptr);
        if (favorite) {
          const bool saved = favorite->getToggleState();
          LiveScenario::focus(*favorite);
          expect(live.key(juce::KeyPress::returnKey));
          expect(LiveScenario::until([&] { return favorite->getToggleState() != saved; }));
          expect(favorite->hasKeyboardFocus(false));
          expectEquals(favorite->getTitle(), juce::String(saved ? "Add to favorites" : "Remove from favorites"));
          expect(favorite->getAccessibilityHandler()->getCurrentState().isChecked() != saved);
          LiveScenario::pump(100);
          live.key(juce::KeyPress::returnKey);
          expect(LiveScenario::until([&] { return favorite->getToggleState() == saved; }));
        }
      }
    }
    beginTest("Enter opens EQ and focuses a fader that responds to arrows");
    {
      LiveScenario live(*this, "main-detail");
      if (live.ok) {
        auto* eq = drive::find(live.root(), [](juce::Component& c) {
          return dynamic_cast<juce::Button*>(&c) != nullptr && c.isShowing() && c.getHelpText().startsWith("EQ:");
        });
        expect(eq != nullptr);
        if (eq) {
          LiveScenario::focus(*eq);
          expect(live.key(juce::KeyPress::returnKey));
          expect(LiveScenario::until([] {
            auto* focused = LiveScenario::focused();
            return focused && focused->getHelpText().startsWith("EQ Power:");
          }), "Bypassed EQ focuses its power button");
          live.key(juce::KeyPress::returnKey);
          LiveScenario::pump(100);
          LiveScenario::focus(*eq);
          live.key(juce::KeyPress::returnKey); // close
          LiveScenario::pump(50);
          live.key(juce::KeyPress::returnKey); // reopen enabled EQ
          expect(LiveScenario::until([] { return dynamic_cast<juce::Slider*>(LiveScenario::focused()) != nullptr; }));
          if (auto* fader = dynamic_cast<juce::Slider*>(LiveScenario::focused())) {
            const double before = fader->getValue();
            expect(live.key(juce::KeyPress::upKey));
            expectWithinAbsoluteError(fader->getValue(), before + 0.5, 0.001);
            auto* editor = fader->findParentComponentOfClass<BlockEqView>();
            expect(editor != nullptr);
            if (editor) expectWithinAbsoluteError(editor->bands()[1].gainDb, before + 0.5, 0.001);
            expect(live.key(juce::KeyPress::downKey));
            expectWithinAbsoluteError(fader->getValue(), before, 0.001);
          }
        }
      }
    }
    for (const auto* scenario : {"main-detail", "main-detail-info-signed-out"}) {
      beginTest(juce::String("Enter opens Info and focuses its content: ") + scenario);
      LiveScenario live(*this, scenario);
      if (!live.ok) continue;
      auto* info = drive::find(live.root(), [](juce::Component& c) {
        return dynamic_cast<juce::Button*>(&c) != nullptr && c.isShowing() && c.getHelpText().startsWith("Info:");
      });
      expect(info != nullptr);
      if (!info) continue;
      LiveScenario::focus(*info);
      expect(live.key(juce::KeyPress::returnKey));
      expect(LiveScenario::until([&] {
        auto* focused = LiveScenario::focused();
        for (auto* c = focused; c != nullptr; c = c->getParentComponent())
          if (dynamic_cast<BlockInfoPanel*>(c) != nullptr) return focused->isShowing();
        return false;
      }), "Info should focus its overview or sign-in prompt after opening");
    }
  }
};
static ToneInfoKeyboardTests toneInfoKeyboardTests;

struct BrowseInfoTests : juce::UnitTest {
  BrowseInfoTests() : juce::UnitTest("Browse tone information", "ui") {}
  void runTest() override {
    beginTest("browsing results initialize and accept keyboard focus");
    LiveScenario live(*this, "browser-search");
    if (!live.ok) return;
    auto* card = dynamic_cast<ToneCard*>(drive::find(live.root(), [](juce::Component& component) {
      return dynamic_cast<ToneCard*>(&component) != nullptr && component.isShowing();
    }));
    expect(card != nullptr);
    if (!card) return;
    // The hint bar shows nothing for a card, as upstream; a screen reader
    // gets the creator's description as the card's description.
    expect(card->getHelpText().isEmpty(), "a card has no hint-bar text");
    if (card->tone().description.trim().isNotEmpty())
      expect(card->createAccessibilityHandler()->getDescription().contains(card->tone().description.trim()),
             "Results retain the original creator description");
    int picks = 0;
    juce::String openedUrl;
    card->onClick = [&] { ++picks; };
    card->onOpenUrl = [&](const juce::String& url) { openedUrl = url; };
    LiveScenario::focus(*card);
    beginTest("Applications key opens Open on web without selecting a tone; no More info");
    expect(live.key(kWindowsApplicationsKey));
    auto* web = drive::buttonNamed(live.root(), "Open on web");
    expect(web != nullptr);
    expect(drive::buttonNamed(live.root(), "More info") == nullptr, "the card menu has no More info row");
    expectEquals(picks, 0);
    if (!web) return;
    beginTest("Escape closes the card menu and returns focus to the card");
    auto* menu = web->findParentComponentOfClass<Popover>();
    expect(menu != nullptr);
    if (menu) expect(menu->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    expect(LiveScenario::until([&] { return card->hasKeyboardFocus(false); }));
    expectEquals(picks, 0);
    beginTest("Shift F10 is routed through the window to the browsing card");
    expect(live.key(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier));
    auto* keyboardMenuRow = drive::buttonNamed(live.root(), "Open on web");
    expect(keyboardMenuRow != nullptr);
    if (keyboardMenuRow) {
      keyboardMenuRow->onClick();
      expectEquals(openedUrl, card->tone().url.isNotEmpty() ? card->tone().url :
          "https://www.tone3000.com/tones/" + juce::String(card->tone().id));
      if (auto* keyboardMenu = keyboardMenuRow->findParentComponentOfClass<Popover>()) keyboardMenu->dismiss();
    }
    expect(LiveScenario::until([&] { return card->hasKeyboardFocus(false); }));
    expectEquals(picks, 0);
    beginTest("a right-click on a card opens no menu (upstream)");
    live.root().unfocusAllComponents();
    LiveScenario::pump(30);
    {
      // Through the peer, as the OS delivers it: a right press and release.
      const auto pos = live.centre(*card);
      auto t = juce::Time::currentTimeMillis();
      using Type = juce::MouseInputSource::InputSourceType;
      live.peer->handleMouseEvent(Type::mouse, pos, juce::ModifierKeys(), 0.0f, 0.0f, ++t, {}, 0);
      live.peer->handleMouseEvent(Type::mouse, pos, juce::ModifierKeys::rightButtonModifier, 0.0f, 0.0f, ++t, {}, 0);
      LiveScenario::pump(20);
      live.peer->handleMouseEvent(Type::mouse, pos, juce::ModifierKeys(), 0.0f, 0.0f, ++t, {}, 0);
      LiveScenario::pump(50);
    }
    expect(drive::buttonNamed(live.root(), "Open on web") == nullptr);
    expect(!card->hasKeyboardFocus(false), "a click does not focus the card");
    picks = 0;  // a right press clicks a JUCE button, as upstream
    beginTest("unloadable cards are disabled and say why to a screen reader");
    card->setDisabled(true);
    expect(!card->isEnabled());
    auto handler = card->createAccessibilityHandler();
    expect(handler->getDescription().startsWith("Unavailable in the plugin."));
    handler->getActions().invoke(juce::AccessibilityActionType::press);
    handler->getActions().invoke(juce::AccessibilityActionType::showMenu);
    LiveScenario::pump(30);
    expectEquals(picks, 0);
    expect(drive::buttonNamed(live.root(), "Open on web") == nullptr);
  }
};
BrowseInfoTests browseInfoTests;

struct PresetAccessibilityTests : juce::UnitTest {
  PresetAccessibilityTests() : juce::UnitTest("Preset accessibility", "ui") {}
  void runTest() override {
    LiveScenario live(*this, "chrome-preset-browse");
    if (!live.ok) return;
    auto& root = live.root();
    beginTest("search narrows named preset buttons and Enter loads the selected chain");
    drive::clickByHelp(root, "Presets:");
    drive::fill(root, "Search presets", "Church");
    drive::wait(400);
    auto* result = drive::buttonNamed(root, "Church Sunday");
    expect(result != nullptr);
    expect(drive::buttonNamed(root, "My Lead Tone") == nullptr);
    if (result != nullptr) {
      LiveScenario::focus(*result);
      live.key(juce::KeyPress::returnKey);
      drive::wait(200);
      const auto& active = root.services().chain.state().preset;
      expect(active.has_value());
      if (active) expectEquals(active->name, juce::String("Church Sunday"));
      expect(drive::find(root, [](juce::Component& c) {
        auto* button = dynamic_cast<Clickable*>(&c);
        return button != nullptr && button->isShowing() && button->accessibleName() == "Presets: Church Sunday";
      }) != nullptr);
    }

    beginTest("Enter on Add Tone opens browsing instead of starting a sort");
    auto* add = dynamic_cast<AddTile*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<AddTile*>(&c) != nullptr && c.isShowing();
    }));
    expect(add != nullptr);
    if (add != nullptr) {
      int opens = 0;
      add->onAdd = [&](const std::string&) { ++opens; };
      expect(add->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
      expectEquals(opens, 1);
      expect(!add->travelling());
      beginTest("Shift F10 opens the local-file actions by keyboard");
      expect(add->keyPressed(juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)));
      expect(drive::buttonNamed(root, "Load File") != nullptr);
      expect(drive::buttonNamed(root, "Load Folder") != nullptr);
    }
  }
};

struct DialogAccessibilityTests : juce::UnitTest {
  DialogAccessibilityTests() : juce::UnitTest("Dialog accessibility", "ui") {}
  void runTest() override {
    beginTest("update text, version and reminder labels are accessible; Tab stays inside; Escape closes");
    {
      LiveScenario live(*this, "load-update-notice");
      expect(live.ok);
      if (!live.ok) return;
      live.drive();
      auto* modal = dynamic_cast<UpdateNotice*>(drive::find(live.root(), [](juce::Component& c) {
        return dynamic_cast<UpdateNotice*>(&c) != nullptr && c.isShowing();
      }));
      expect(modal != nullptr);
      if (modal == nullptr) return;
      expect(modal->getTitle().contains("1.5.0"));
      expect(modal->getDescription().contains("per-block EQ"));
      expectEquals(static_cast<int>(modal->getAccessibilityHandler()->getRole()),
                   static_cast<int>(juce::AccessibilityRole::dialogWindow));
      expect(LiveScenario::until([&] { return modal->hasKeyboardFocus(true); }), "Dialog takes initial focus");
      modal->giveAwayKeyboardFocus();
      expect(LiveScenario::until([&] { return modal->hasKeyboardFocus(true); }),
             "Active host reacquires dialog focus after native activation clears it");
      live.window->grabKeyboardFocus();
      expect(LiveScenario::until([&] { return modal->hasKeyboardFocus(true); }),
             "Dialog takes focus when its parent host window holds it");
      live.window->setVisible(false);
      modal->giveAwayKeyboardFocus();
      modal->focusFirstControl();
      LiveScenario::pump(100);
      expect(!modal->hasKeyboardFocus(true), "Hidden editor waits for host focus");
      showTestWindow(*live.window);
      live.window->toFront(true);
      live.peer->grabFocus();
      expect(LiveScenario::until([&] { return modal->hasKeyboardFocus(true); }),
             "Existing dialog receives focus when the host activates the editor");
      auto* remind = drive::buttonNamed(*modal, "7 days");
      expect(remind != nullptr);
      if (remind) expectEquals(remind->getAccessibilityHandler()->getTitle(), juce::String("Remind me in 7 days"));
      for (int i = 0; i < 12; ++i) {
        live.key(juce::KeyPress::tabKey, i < 6 ? juce::ModifierKeys() : juce::ModifierKeys::shiftModifier);
        expect(modal->hasKeyboardFocus(true), "Tab cannot enter controls behind the dialog");
      }
      modal->giveAwayKeyboardFocus(); // models entering the editor with no JUCE control focused
      live.key(juce::KeyPress::tabKey);
      expect(modal->hasKeyboardFocus(true), "Tab from host enters the dialog");
      juce::Component::SafePointer<UpdateNotice> safe(modal);
      live.key(juce::KeyPress::escapeKey);
      expect(LiveScenario::until([&] { return safe == nullptr; }));
      expect(!live.root().services().updates.notice().has_value());
    }
    beginTest("connection dialog describes the problem and restores its opener on Escape");
    {
      LiveScenario live(*this, "load-offline-modal");
      expect(live.ok);
      if (!live.ok) return;
      auto* add = drive::find(live.root(), [](juce::Component& c) { return dynamic_cast<AddTile*>(&c) != nullptr && c.isShowing(); });
      expect(add != nullptr);
      if (!add) return;
      LiveScenario::focus(*add);
      live.key(juce::KeyPress::returnKey);
      ConnectionModal* modal = nullptr;
      expect(LiveScenario::until([&] {
        modal = dynamic_cast<ConnectionModal*>(drive::find(live.root(), [](juce::Component& c) {
          return dynamic_cast<ConnectionModal*>(&c) != nullptr && c.isShowing();
        }));
        return modal != nullptr;
      }));
      if (!modal) return;
      expect(modal->getDescription().contains("No internet connection"));
      expect(modal->hasKeyboardFocus(true));
      juce::Component::SafePointer<ConnectionModal> safe(modal);
      live.key(juce::KeyPress::escapeKey);
      expect(LiveScenario::until([&] { return safe == nullptr; }));
      expect(add->hasKeyboardFocus(false), "Returns to Add tone");
    }
    beginTest("rich dialog text is readable and links activate without a mouse");
    RichTextView text(13, 20, juce::Colours::white);
    text.setText(Html::toRichText("Read <a href='https://example.com/notes'>release notes</a>."));
    auto handler = text.createAccessibilityHandler();
    expectEquals(handler->getTitle(), juce::String("Read release notes."));
    expectEquals(static_cast<int>(handler->getRole()), static_cast<int>(juce::AccessibilityRole::staticText));
    juce::String opened;
    text.onLink = [&](const juce::String& url) { opened = url; };
    expectEquals(text.getNumChildComponents(), 1);
    if (auto* link = text.getChildComponent(0)) {
      expect(link->getWantsKeyboardFocus());
      expect(link->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
      expectEquals(opened, juce::String("https://example.com/notes"));
    }
    beginTest("secure-connection message and dismissal are accessible");
    ConnectionModal secure({}, ConnectionGate::Problem::insecure, true);
    expect(secure.getDescription().contains("wrong date & time"));
    bool dismissed = false;
    secure.onDismiss = [&] { dismissed = true; };
    expect(secure.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    expect(dismissed);
    expect(drive::find(secure, [](juce::Component& c) {
      return dynamic_cast<juce::Button*>(&c) != nullptr && c.getName() == "Date & time settings";
    }) != nullptr);
    beginTest("Escape from preset popup text fields closes and restores the opening button");
    {
      LiveScenario live(*this, "chrome-preset-browse");
      expect(live.ok);
      if (!live.ok) return;
      for (const auto& prefix : {"Save Preset:", "Presets:"}) {
        auto* opener = drive::byHelpPrefix(live.root(), prefix);
        expect(opener != nullptr);
        if (!opener) continue;
        LiveScenario::focus(*opener);
        live.key(juce::KeyPress::returnKey);
        drive::wait(50);
        auto* panel = dynamic_cast<Popover*>(drive::find(live.root(), [](juce::Component& c) {
          auto* p = dynamic_cast<Popover*>(&c);
          return p != nullptr && p->isShowing() && p->isOpen();
        }));
        expect(panel != nullptr);
        if (!panel) continue;
        expect(dynamic_cast<juce::TextEditor*>(LiveScenario::focused()) != nullptr);
        live.key(juce::KeyPress::escapeKey);
        drive::wait(50);
        expect(!panel->isOpen());
        expect(opener->hasKeyboardFocus(false), "Popup returns to the opening button");
      }
      if (juce::SystemStats::getEnvironmentVariable("T3K_DIALOG_PREVIEW", "") == "1") {
        beginTest("opt-in previews open real dialogs and close without changing update or connection state");
        for (const auto code : {'U', 'O', 'S'}) {
          live.key(code, juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier);
          auto* modal = dynamic_cast<ModalLayer*>(drive::find(live.root(), [](juce::Component& c) {
            return dynamic_cast<ModalLayer*>(&c) != nullptr && c.isShowing();
          }));
          expect(modal != nullptr);
          if (!modal) continue;
          expect(modal->hasKeyboardFocus(true));
          juce::Component::SafePointer<ModalLayer> safe(modal);
          live.key(juce::KeyPress::escapeKey);
          expect(LiveScenario::until([&] { return safe == nullptr; }));
          expect(!live.root().services().connection.problem().has_value());
        }
      }
    }
  }
};

struct ChainAccessibilityTests : juce::UnitTest {
  ChainAccessibilityTests() : juce::UnitTest("Chain accessibility", "ui") {}
  void runTest() override {
    beginTest("loading transitions report completion, failure and retry without polling chatter");
    ChainState before, after;
    ChainItem item;
    item.isInsert = false;
    item.blockId = "test-model";
    item.tone.title = "Test amp";
    after.chain.push_back(item);
    expect(ChainStore::loadingFeedback(before, after).startsWith("Loading Test amp"));
    before = after;
    expect(ChainStore::loadingFeedback(before, after).isEmpty());
    after.chain[0].loaded = true;
    expect(ChainStore::loadingFeedback(before, after).startsWith("Loaded Test amp"));
    before = after;
    after.chain[0].params.enabled = false;
    expect(ChainStore::loadingFeedback(before, after).isEmpty());
    after.chain[0].modelLoading = true; // old model can still be playing
    expect(ChainStore::loadingFeedback(before, after).startsWith("Loading Test amp"));
    before = after;
    after.chain[0].modelLoading = false;
    after.chain[0].loadFailed = true;
    expect(ChainStore::loadingFeedback(before, after).startsWith("Could not load Test amp"));
    before = after;
    expect(ChainStore::loadingFeedback(before, after).isEmpty());
    after.chain[0].loadFailed = false;
    after.chain[0].modelLoading = true;
    expect(ChainStore::loadingFeedback(before, after).startsWith("Loading Test amp"));
    before = after;
    after.chain.clear();
    expect(ChainStore::loadingFeedback(before, after).isEmpty());

    beginTest("keyboard moving crosses lanes, keeps focus, commits and cancels");
    LiveScenario live(*this, "main-stereo");
    expect(live.ok);
    if (!live.ok) return;
    auto* tile = dynamic_cast<ToneTile*>(drive::find(live.root(), [](juce::Component& c) {
      auto* t = dynamic_cast<ToneTile*>(&c);
      auto* lane = c.findParentComponentOfClass<GalleryLane>();
      return t != nullptr && t->isShowing() && lane != nullptr && lane->side() == ChainSide::left;
    }));
    expect(tile != nullptr);
    if (tile == nullptr) return;
    const auto id = tile->blockId();
    const auto locate = [&]() {
      return dynamic_cast<ToneTile*>(drive::find(live.root(), [&](juce::Component& c) {
        auto* t = dynamic_cast<ToneTile*>(&c);
        return t != nullptr && t->blockId() == id;
      }));
    };
    expect(tile->getDescription().contains("Left chain"));
    LiveScenario::focus(*tile);
    expect(live.key(juce::KeyPress::spaceKey));
    expect(tile->travelling());
    const auto startDescription = tile->getDescription();
    expect(live.key(juce::KeyPress::rightKey));
    expect(tile->getDescription() != startDescription, "Right arrow changes the position");
    expect(live.key(juce::KeyPress::leftKey));
    expectEquals(tile->getDescription(), startDescription);
    expect(live.key(juce::KeyPress::downKey));
    tile = locate();
    expect(tile != nullptr && tile->hasKeyboardFocus(false));
    if (tile == nullptr) return;
    expect(tile->getDescription().contains("Right chain"));
    expect(live.key(juce::KeyPress::returnKey));
    tile = locate();
    expect(tile != nullptr && !tile->travelling() && tile->hasKeyboardFocus(false), "Drop retains block focus");
    if (tile == nullptr) return;
    expect(tile->getDescription().contains("Right chain"));
    const auto committed = tile->getDescription();
    expect(live.key(juce::KeyPress::spaceKey));
    expect(live.key(juce::KeyPress::upKey));
    expect(live.key(juce::KeyPress::escapeKey));
    tile = locate();
    expect(tile != nullptr && !tile->travelling() && tile->hasKeyboardFocus(false), "Cancel retains block focus");
    if (tile != nullptr) expectEquals(tile->getDescription(), committed);
  }
};

struct AudioSettingsAccessibilityTests : juce::UnitTest {
  AudioSettingsAccessibilityTests() : juce::UnitTest("Audio settings accessibility", "ui") {}
  void runTest() override {
    using KP = juce::KeyPress;
    beginTest("selectors expose a combo box, its label and read-back value");
    SelectField device("Audio device");
    device.setOptions({{"audient", "Audient USB Audio ASIO Driver", {}}, {"other", "Other interface", {}}});
    device.setValue("audient");
    auto handler = device.createAccessibilityHandler();
    expect(handler->getRole() == juce::AccessibilityRole::comboBox);
    expectEquals(handler->getTitle(), juce::String("Audio device"));
    expectEquals(handler->getValueInterface()->getCurrentValueAsString(), juce::String("Audient USB Audio ASIO Driver"));
    expect(handler->getActions().contains(juce::AccessibilityActionType::showMenu));
    expect(device.getWantsKeyboardFocus());
    device.setDisabled(true);
    // Dimmed and inert, as upstream (the component stays enabled so the
    // mouse behaves as before); it leaves the Tab order.
    expect(!device.getWantsKeyboardFocus());
    expect(!device.keyPressed(KP(KP::returnKey)));
    handler->getActions().invoke(juce::AccessibilityActionType::showMenu);
    expect(!device.isOpen());

    LiveScenario live(*this, "settings-system");
    expect(live.ok, "a real window is required for the audio-settings keyboard test");
    if (!live.ok) return;
    auto& root = live.root();
    root.openSettings(SettingsScreen::Tab::system);
    live.pump(100);
    auto* rate = dynamic_cast<SelectField*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<SelectField*>(&c) != nullptr && c.getName() == "Sample rate";
    }));
    expect(rate != nullptr);
    if (rate == nullptr) return;
    expect(rate->options().size() > 1);
    if (rate->options().size() < 2) return;
    // Isolate the UI interaction from real hardware and emulate the owner's
    // read-back, as the live AudioDeviceStore does after an accepted change.
    juce::String picked;
    rate->onChange = [&](const juce::String& value) { picked = value; rate->setValue(value); };
    beginTest("Enter opens the current option; polling preserves keyboard focus");
    LiveScenario::focus(*rate);
    expect(live.key(KP::returnKey));
    live.pump(30);
    expect(rate->isOpen());
    auto* focused = live.focused();
    expect(focused != nullptr && focused != rate);
    if (focused != nullptr) expectEquals(focused->getName(), rate->selectedLabel());
    const auto options = rate->options();
    rate->setOptions(options);
    rate->setValue(rate->value());
    expect(live.focused() == focused, "unchanged device read-back must not destroy the focused row");

    beginTest("arrows and Enter select an option and return to the named combo box");
    expect(live.key(KP::downKey));
    live.pump(20);
    const auto* next = live.focused();
    const auto expectedLabel = next != nullptr ? next->getName() : juce::String();
    expect(live.key(KP::returnKey));
    live.pump(30);
    expect(picked.isNotEmpty());
    expect(!rate->isOpen());
    expect(live.focused() == rate);
    expectEquals(rate->getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(), expectedLabel);

    beginTest("screen-reader expand opens options; Escape cancels without changing the value");
    const auto prior = rate->value();
    expect(rate->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::showMenu));
    expect(rate->isOpen());
    expect(live.key(KP::escapeKey));
    live.pump(20);
    expect(!rate->isOpen());
    expect(live.focused() == rate);
    expect(rate->value() == prior);
    rate->onChange = nullptr;
  }
};

// The Settings page from the keyboard, in a real window. Issue #203: a
// pointer with no wheel (a wheel-less mouse; a touchscreen the OS presents
// as a mouse) had no way to reach the device picker below the fold. With
// nothing focused the scroll keys move the page and Space / Enter still go
// to the host; the Tab walk stays on the page and lands every control in
// view; a focused control passes the keys it does not take on to the page.
struct SettingsKeyboardTests : juce::UnitTest {
  SettingsKeyboardTests() : juce::UnitTest("Settings keyboard", "ui") {}

  void runTest() override {
    using KP = juce::KeyPress;
    LiveScenario live(*this, "settings-system");  // standalone: the long System page
    if (!live.ok) return;
    auto& root = live.root();
    root.openSettings(SettingsScreen::Tab::system);
    live.pump(500);
    auto* settings = root.settings();
    expect(settings != nullptr);
    if (settings == nullptr) return;
    auto& view = settings->scroller();
    auto* page = view.getViewedComponent();
    const int maxY = page->getHeight() - view.getMaximumVisibleHeight();
    expect(maxY > DragScroller::kKeyLineStep * 4, "the System page overflows the window");
    const auto inView = [&](const juce::Component& c) {
      const auto box = page->getLocalArea(&c, c.getLocalBounds());
      const juce::Rectangle<int> visible(0, view.getViewPositionY(), page->getWidth(), view.getMaximumVisibleHeight());
      return visible.contains(box) || box.getHeight() > visible.getHeight();
    };

    beginTest("with nothing focused, arrows, Page and Home / End scroll the page");
    expect(live.focused() == nullptr);
    expect(live.key(KP::downKey));
    expectEquals(view.getViewPositionY(), DragScroller::kKeyLineStep);
    expect(live.key(KP::pageDownKey));
    expect(view.getViewPositionY() > DragScroller::kKeyLineStep);
    expect(live.key(KP::endKey));
    expectEquals(view.getViewPositionY(), maxY);
    expect(live.key(KP::pageUpKey));
    expect(view.getViewPositionY() < maxY);
    expect(live.key(KP::homeKey));
    expectEquals(view.getViewPositionY(), 0);

    beginTest("Space and Enter are still the host's; a modified arrow is not the page's");
    expect(!live.key(KP::spaceKey));
    expect(!live.key(KP::returnKey));
    expect(!live.key(KP::downKey, juce::ModifierKeys::shiftModifier));
    expectEquals(view.getViewPositionY(), 0);

    beginTest("a Tab walk stays on the page and wraps; the page follows it below the fold");
    bool followed = false, wrapped = false;
    int stops = 0;
    juce::Component* first = nullptr;
    for (int i = 0; i < 200; ++i) {
      expect(live.key(KP::tabKey));
      live.pump(30);  // the focus callback that reveals is posted
      auto* f = live.focused();
      if (f == nullptr || !page->isParentOf(f)) break;  // walked off the page: the chrome underneath
      if (first == nullptr) first = f;
      else if (f == first) {
        wrapped = true;
        break;
      }
      ++stops;
      expect(inView(*f), "Tab stop " + juce::String(stops) + " (" + f->getName() + ") is out of view");
      if (view.getViewPositionY() > 0) followed = true;
    }
    expect(wrapped, "the cycle came back to its first stop without leaving the page");
    expect(stops > 3 && followed, "the walk reached controls below the fold and the page followed");
    // Clear focus without Escape (Escape now closes Settings).
    juce::Component::unfocusAllComponents();
    live.pump(30);
    expect(live.focused() == nullptr);

    beginTest("a focused control passes the page's keys up to the page");
    view.setViewPosition(0, 0);
    expect(live.key(KP::tabKey));  // the first stop: the Close button
    live.pump(30);
    expect(dynamic_cast<Clickable*>(live.focused()) != nullptr);
    expect(live.key(KP::downKey));
    expectEquals(view.getViewPositionY(), DragScroller::kKeyLineStep);
    expect(!live.key(KP::spaceKey));  // and Space still falls through to the host
    juce::Component::unfocusAllComponents();
    live.pump(30);
    expect(live.focused() == nullptr);
  }
};

// Every scroll surface in the UI, in a real window, moved every way a
// device can move it: a wheel turn (a trackpad gesture is a run of them;
// a plain wheel pans a sideways lane too), a mouse drag, a finger where the
// platform has touch sources, the keys with nothing focused (the screen's
// main scroller only), the keys bubbling up from a focused control inside
// (a menu's list leaves them to the menu, which walks its rows instead),
// and keyboard focus landing on a control out of view, which the surface
// scrolls to. One table, so a surface added later gets the whole matrix.
struct ScrollSurfacesTests : juce::UnitTest {
  ScrollSurfacesTests() : juce::UnitTest("Scroll surfaces", "ui") {}

  struct Surface {
    const char* name;
    const char* scenario;
    // Before the scenario's own drive: lengthen the data so the surface
    // overflows. After it: open a panel the scenario does not.
    std::function<void(LiveScenario&)> before, after;
    std::function<DragScroller*(PluginRoot&)> scroller;
    bool mainScroller;  // the root's scroll keys with nothing focused go here
    bool ownKeys;       // Keys::scroll; else a menu's list (its owner walks the rows)
    // Where a drag starts, in the scroller's space; default its centre. The
    // chain lane's centre is a tile, which sorts on a drag: the gap pans.
    std::function<juce::Point<float>(DragScroller&)> grab;
  };

  static DragScroller* scrollerWhere(juce::Component& root, const std::function<bool(DragScroller&)>& pred) {
    return dynamic_cast<DragScroller*>(drive::find(root, [&](juce::Component& c) {
      auto* s = dynamic_cast<DragScroller*>(&c);
      return s != nullptr && s->isShowing() && pred(*s);
    }));
  }
  static bool overflows(const DragScroller& s) {
    const auto* content = s.getViewedComponent();
    if (content == nullptr) return false;
    return s.axis() == DragScroller::Axis::vertical ? content->getHeight() > s.getMaximumVisibleHeight()
                                                     : content->getWidth() > s.getMaximumVisibleWidth();
  }
  static bool inPopover(const juce::Component& c) { return c.findParentComponentOfClass<Popover>() != nullptr; }
  // The screen's own scroller along `axis`: showing, overflowing, not a panel's.
  static std::function<DragScroller*(PluginRoot&)> screenScroller(DragScroller::Axis axis) {
    return [axis](PluginRoot& root) {
      return scrollerWhere(root, [&](DragScroller& s) { return s.axis() == axis && overflows(s) && !inPopover(s); });
    };
  }
  // The list inside the open panel.
  static DragScroller* panelScroller(PluginRoot& root) {
    return scrollerWhere(root, [](DragScroller& s) { return inPopover(s); });
  }

  // Eight blocks: the lane overflows the window with room to pan.
  static void lengthenChain(LiveScenario& live) {
    auto chain = live.scenario->data["chain"].clone();
    auto* blocks = chain["chain"].getArray();
    const auto seed = *blocks;
    for (int i = 0; blocks->size() < 8; ++i) {
      auto block = seed[i % seed.size()].clone();
      block.getDynamicObject()->setProperty("blockId", "blk-long-" + juce::String(i));
      blocks->add(block);
    }
    live.backend->setChain(chain);
    live.pump(600);  // the store polls the revision
  }
  // A dozen more presets: the browser's list overflows its cap.
  static void lengthenPresets(LiveScenario& live) {
    for (int i = 0; i < 12; ++i) live.root().services().presets.save("Long list " + juce::String(i + 1));
    live.pump(100);
  }
  // A dozen input devices: the Input device select's list overflows its cap.
  static void lengthenDevices(LiveScenario& live) {
    auto device = live.scenario->data["device"].clone();
    auto* inputs = device["inputDevices"].getArray();
    for (int i = 0; inputs->size() < 12; ++i) inputs->add("Interface " + juce::String(i + 1));
    live.backend->setDevice(device);
    live.pump(200);
  }
  static void openInputDeviceSelect(LiveScenario& live) {
    if (auto* select = drive::find(live.root(), [](juce::Component& c) { return c.getName() == "Input device"; }))
      drive::click(live.root(), *select);
    live.pump(300);
  }
  // The gap after the first tile.
  static juce::Point<float> laneGap(DragScroller& lane) {
    auto* tile = drive::find(lane, [](juce::Component& c) { return dynamic_cast<GalleryTile*>(&c) != nullptr; });
    if (tile == nullptr) return lane.getLocalBounds().getCentre().toFloat();
    const auto box = lane.getLocalArea(tile, tile->getLocalBounds()).toFloat();
    return {box.getRight() + gallery::kTileGap / 2.0f, box.getCentreY()};
  }

  void runTest() override {
    using KP = juce::KeyPress;
    const auto settingsPage = [](PluginRoot& root) {
      return root.settings() != nullptr ? &root.settings()->scroller() : nullptr;
    };
    const Surface surfaces[] = {
        {"the chain lane", "main-mono", lengthenChain, nullptr, screenScroller(DragScroller::Axis::horizontal), true,
         true, laneGap},
        {"a block's detail column with its info open", "main-detail-info", nullptr, nullptr,
         screenScroller(DragScroller::Axis::vertical), true, true},
        {"the Settings page", "settings-system", nullptr, nullptr, settingsPage, true, true},
        {"the tone browser's results", "browser-search", nullptr, nullptr, screenScroller(DragScroller::Axis::vertical),
         true, true},
        {"the browser's filter chips", "browser-filters-expanded", nullptr, nullptr,
         screenScroller(DragScroller::Axis::horizontal), false, true},
        {"the preset browser's list (a panel's rows)", "chrome-preset-browse", lengthenPresets, nullptr, panelScroller,
         false, false},
        {"a select field's options (a panel's rows)", "settings-system", lengthenDevices, openInputDeviceSelect,
         panelScroller, false, false},
    };
    for (const auto& surface : surfaces) run(surface);

    beginTest("a mouse drag on a tile sorts it; the lane around it stays put");
    {
      LiveScenario live(*this, "main-mono");
      if (!live.ok) return;
      live.drive();
      lengthenChain(live);
      auto* lane = screenScroller(DragScroller::Axis::horizontal)(live.root());
      auto* tile = dynamic_cast<GalleryTile*>(drive::find(live.root(), [](juce::Component& c) {
        return dynamic_cast<GalleryTile*>(&c) != nullptr && c.isShowing();
      }));
      expect(lane != nullptr && tile != nullptr);
      if (lane == nullptr || tile == nullptr) return;
      auto mouse = live.mouse();
      const auto from = live.centre(*tile);
      mouse.at(from, true);
      for (int i = 1; i <= 6; ++i) mouse.at(from.translated(-20.0f * i, 0), true);
      expectEquals(lane->getViewPositionX(), 0);
      mouse.at(from.translated(-120.0f, 0), false);
      live.pump(300);
      expectEquals(lane->getViewPositionX(), 0);
    }

    beginTest("a mouse drag across a text field selects its text; the page around it stays put");
    {
      LiveScenario live(*this, "settings-advanced");
      if (!live.ok) return;
      live.drive();
      auto* settings = live.root().settings();
      auto* editor = settings != nullptr ? dynamic_cast<juce::TextEditor*>(drive::find(*settings, [](juce::Component& c) {
        return dynamic_cast<juce::TextEditor*>(&c) != nullptr && c.isShowing();
      })) : nullptr;
      expect(editor != nullptr, "a text field on the page");
      if (editor == nullptr) return;
      auto& page = settings->scroller();
      page.reveal(*editor, 0);
      live.pump(50);
      const int before = page.getViewPositionY();
      auto mouse = live.mouse();
      mouse.drag(live.at(*editor, {2.0f, editor->getHeight() / 2.0f}), {static_cast<float>(editor->getWidth()), 0.0f});
      live.pump(100);
      expect(!editor->getHighlightedRegion().isEmpty(), "the drag selected text");
      expectEquals(page.getViewPositionY(), before);
    }
  }

  void run(const Surface& surface) {
    using KP = juce::KeyPress;
    beginTest(surface.name);
    LiveScenario live(*this, surface.scenario);
    if (!live.ok) return;
    if (surface.before) surface.before(live);
    live.drive();
    if (surface.after) surface.after(live);
    auto* scroller = surface.scroller(live.root());
    expect(scroller != nullptr, "the scroller is showing");
    if (scroller == nullptr) return;
    const bool vertical = scroller->axis() == DragScroller::Axis::vertical;
    auto* content = scroller->getViewedComponent();
    const auto range = [&] {
      return vertical ? content->getHeight() - scroller->getMaximumVisibleHeight()
                      : content->getWidth() - scroller->getMaximumVisibleWidth();
    };
    // The content has settled (a store's reply, images, a relayout) when
    // its size has held for a while.
    for (int held = 0, last = range(); held < 300; held += 10) {
      live.pump(10);
      if (range() != last) held = 0, last = range();
    }
    expect(range() > DragScroller::kKeyLineStep, "the content overflows the view");
    if (range() <= DragScroller::kKeyLineStep) return;
    const auto pos = [&] { return vertical ? scroller->getViewPositionY() : scroller->getViewPositionX(); };
    const auto rewind = [&] {
      for (int still = 0; still < 500; still += 10) {  // a drag's momentum outlasts any fixed wait on a slow machine
        live.pump(10);
        if (pos() != 0) scroller->setViewPosition(0, 0), still = 0;
      }
    };
    const auto along = [&](float d) { return vertical ? juce::Point<float>(0.0f, d) : juce::Point<float>(d, 0.0f); };
    const auto inView = [&](const juce::Component& c) {
      const auto box = content->getLocalArea(&c, c.getLocalBounds());
      const int start = vertical ? box.getY() : box.getX(), end = vertical ? box.getBottom() : box.getRight();
      const int visible = vertical ? scroller->getMaximumVisibleHeight() : scroller->getMaximumVisibleWidth();
      return (start >= pos() && end <= pos() + visible) || end - start > visible;
    };
    const int forward = vertical ? KP::downKey : KP::rightKey;
    // Recompute after rewind: scrolling moves the tile that defines a lane gap.
    const auto middle = [&] { return surface.grab ? live.at(*scroller, surface.grab(*scroller)) : live.centre(*scroller); };

    // A plain wheel turn pans it, whichever way it runs.
    rewind();
    live.wheel(middle(), 0.0f, -0.5f);
    expect(pos() > 0, "the wheel pans");

    // A mouse drag pans it.
    rewind();
    live.mouse().drag(middle(), along(-150.0f));
    expect(pos() > 0, "a mouse drag pans");

    // A finger pans it, where the platform has touch sources.
    rewind();
    if (auto finger = live.finger()) {
      finger->drag(middle(), along(-150.0f));
      expect(pos() > 0, "a touch drag pans");
    } else {
      logMessage("no touch input source on this platform; the touch pan is not exercised");
    }

    // The keys with nothing focused: the screen's main scroller's. (A
    // panel holds the focus while it is open, so there is no such state.)
    rewind();
    if (live.focused() == nullptr) {
      const bool took = live.key(forward);
      if (surface.mainScroller) {
        expect(took, "the root sends the key here");
        expectEquals(pos(), DragScroller::kKeyLineStep);
      } else {
        expectEquals(pos(), 0, "not the screen's main scroller: the root's keys go elsewhere");
      }
    }

    // The keys from a focused control inside.
    rewind();
    const auto stops = juce::KeyboardFocusTraverser().getAllComponents(content);
    expect(stops.size() >= 2, "controls inside to focus");
    if (stops.size() < 2) return;
    live.focus(*stops.front());
    live.until([&] { return inView(*stops.front()); });  // the view has followed the focus, if it had to
    const int before = pos();
    expect(live.key(forward), "the key is taken");
    // The key's work is synchronous: read it before anything else moves the view.
    if (surface.ownKeys) {
      expectEquals(pos(), juce::jmin(before + DragScroller::kKeyLineStep, range()), "the scroller took the key");
      expect(live.focused() == stops.front(), "and the focus stayed");
    } else {
      expect(live.focused() == stops[1], "the menu walked to the next row");
    }

    // Focus landing out of view scrolls into it, both ways. (The focus must
    // move for the view to follow: start it at the front.)
    live.focus(*stops.front());
    rewind();
    live.focus(*stops.back());
    expect(live.until([&] { return inView(*stops.back()); }), "the last control is in view once focused");
    expect(pos() > 0, "the view moved to it");
    live.focus(*stops.front());
    expect(live.until([&] { return inView(*stops.front()); }), "and back to the first");
    live.key(KP::escapeKey);
  }
};

// A press on the tone browser's results that pans the list, through the
// peer and JUCE's own drag-to-scroll: a tap picks, a scroll gesture pans and
// does not pick the card it started on. Any pointer pans (DragScroller), so
// the mouse source stands in for a finger here; the Button state the fix
// corrects is the same either way (the card stays under the pointer while
// the content pans).
struct TouchScrollTests : juce::UnitTest {
  TouchScrollTests() : juce::UnitTest("Touch scroll", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  struct Pointer {
    juce::ComponentPeer& peer;
    juce::int64 time = juce::Time::currentTimeMillis();
    void at(juce::Point<float> pos, bool down) {
      peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pos,
                            down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f, 0.0f, ++time);
      pump(10);
    }
  };

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("browser-search");
    if (scenario == nullptr) {
      expect(false, "browser-search scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("touch scroll", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    auto* card = dynamic_cast<ToneCard*>(drive::find(host.pluginRoot(), [](juce::Component& c) {
      return dynamic_cast<ToneCard*>(&c) != nullptr && c.isShowing();
    }));
    auto* scroller = card != nullptr ? card->findParentComponentOfClass<DragScroller>() : nullptr;
    expect(peer != nullptr && card != nullptr && scroller != nullptr);
    if (peer == nullptr || card == nullptr || scroller == nullptr) return;
    int picks = 0;
    card->onClick = [&] { ++picks; };
    Pointer pointer{*peer};
    const auto centre = peer->getComponent().getLocalPoint(card, card->getLocalBounds().getCentre().toFloat());

    beginTest("a tap picks the card");
    pointer.at(centre, true);
    pointer.at(centre, false);
    expectEquals(picks, 1);
    expectEquals(scroller->getViewPositionY(), 0);

    beginTest("a scroll gesture pans the list and picks nothing");
    pointer.at(centre, true);
    for (int i = 1; i <= 6; ++i) pointer.at(centre.translated(0, -15.0f * i), true);
    expect(!card->isDown());  // let go as soon as the pan began
    pointer.at(centre.translated(0, -90.0f), false);
    expectEquals(picks, 1);
    expect(scroller->getViewPositionY() > 0);
    window.setVisible(false);
  }
};

// A popover hangs off something in a viewport (a tile in the chain lane, a
// dropdown on the settings page, a chip in the filter row): it must move
// with its anchor as the viewport scrolls, and go once the anchor has
// scrolled out of view.
struct PopoverFollowTests : juce::UnitTest {
  PopoverFollowTests() : juce::UnitTest("Popover follows its anchor", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  struct Root : juce::Component, OverlayHost {
    Root() {
      setSize(800, 600);
      overlay.setBounds(getLocalBounds());
      addAndMakeVisible(overlay);
    }
    juce::Component& overlayLayer() override { return overlay; }
    juce::Component overlay;
  };
  struct Panel : Popover {
    Panel() { setSize(100, 60); }
  };

  void runTest() override {
    Root root;
    juce::Viewport viewport;
    viewport.setBounds(100, 100, 400, 300);
    root.addAndMakeVisible(viewport);
    juce::Component content;
    content.setSize(400, 2000);
    viewport.setViewedComponent(&content, false);
    juce::Component anchor;
    anchor.setBounds(50, 200, 120, 40);
    content.addAndMakeVisible(anchor);
    root.overlay.toFront(false);

    beginTest("an anchored panel scrolls with its anchor");
    Panel panel;
    panel.open(anchor, Popover::Align::left, 4);
    expect(panel.isOpen());
    const auto before = panel.getPosition();
    expectEquals(before.y, root.overlay.getLocalPoint(&anchor, juce::Point<int>()).y + anchor.getHeight() + 4);
    viewport.setViewPosition(0, 120);
    expectEquals(panel.getX(), before.x);
    expectEquals(panel.getY(), before.y - 120);

    beginTest("a panel placed at a point (a context menu) scrolls with its context");
    Panel menu;
    menu.openAt(anchor, {10, 10});
    const auto menuBefore = menu.getPosition();
    viewport.setViewPosition(0, 150);
    expectEquals(menu.getY(), menuBefore.y - 30);
    expectEquals(panel.getY(), before.y - 150);

    beginTest("a partly visible anchor keeps its panel");
    viewport.setViewPosition(0, 220);  // the anchor's 200..240 is half under the top edge
    pump(50);
    expect(panel.isOpen() && menu.isOpen());

    beginTest("an anchor scrolled out of view dismisses");
    int dismissed = 0;
    panel.onDismiss = [&] { ++dismissed; };
    viewport.setViewPosition(0, 600);
    pump(50);
    expect(!panel.isOpen() && !menu.isOpen());
    expectEquals(dismissed, 1);
  }
};

// Services::pointer: a desktop build follows the input, and the gallery's
// hover-revealed chrome pins while that input is a finger.
struct PointerTests : juce::UnitTest {
  PointerTests() : juce::UnitTest("Pointer", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  struct Counter : Pointer::Listener {
    int changes = 0;
    void pointerChanged() override { ++changes; }
  };

  // Each tone tile's action strip: the parent of its power button.
  static void collectStrips(juce::Component& c, std::vector<juce::Component*>& out) {
    if (c.getHelpText() == help::text(help::Key::blockPower) && c.findParentComponentOfClass<GalleryTile>() != nullptr)
      out.push_back(c.getParentComponent());
    for (auto* child : c.getChildren()) collectStrips(*child, out);
  }

  void runTest() override {
    Pointer pointer;
    Counter counter;
    pointer.addListener(&counter);
    if (design::kCoarsePointer) {
      beginTest("a touch platform is touch throughout");
      expect(pointer.coarse());
      pointer.sawInput(false);
      expect(pointer.coarse());
      expectEquals(counter.changes, 0);
      pointer.removeListener(&counter);
      return;
    }
    beginTest("a desktop build follows the last press or wheel turn");
    pointer.sawInput(true);
    expect(pointer.coarse());
    expectEquals(counter.changes, 1);
    pointer.sawInput(true);
    expectEquals(counter.changes, 1);  // no change, no call
    pointer.sawInput(false);
    expect(!pointer.coarse());
    expectEquals(counter.changes, 2);
    pointer.removeListener(&counter);

    beginTest("the tile chrome shows on every tile while the pointer is a finger");
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    ScaledHost host(backend, *scenario, fixtures.root);  // offscreen: no real pointer can hover it
    pump(300);
    auto& root = host.pluginRoot();
    std::vector<juce::Component*> strips;
    collectStrips(root, strips);
    expect(strips.size() >= 2, "expected the stereo lanes' tone tiles");
    auto allAt = [&](float alpha) {
      for (auto* s : strips)
        if (!juce::approximatelyEqual(s->getAlpha(), alpha)) return false;
      return true;
    };
    expect(allAt(0.0f), "hidden until hovered on a mouse");
    root.services().pointer.sawInput(true);
    expect(allAt(1.0f), "shown on touch");
    root.services().pointer.sawInput(false);
    expect(allAt(0.0f), "hidden again once a mouse clicks");

    // Through the peer: JUCE synthesises mouse-source moves (a relayout
    // under the pointer, the peers' pointer-leave handling), so a move must
    // not take the affordances away from a finger; a press does.
    beginTest("the tracker follows presses, not moves");
    juce::DocumentWindow window("pointer", juce::Colours::black, 0);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(300);
    auto* peer = host.getPeer();
    expect(peer != nullptr);
    if (peer != nullptr) {
      using Type = juce::MouseInputSource::InputSourceType;
      auto& tracked = root.services().pointer;
      tracked.sawInput(true);  // macOS has no touch source to drive, so seed the finger directly
      const auto at = peer->getComponent().getLocalBounds().getCentre().toFloat();
      juce::int64 now = juce::Time::currentTimeMillis();
      peer->handleMouseEvent(Type::mouse, at, juce::ModifierKeys(), 0.0f, 0.0f, ++now);
      peer->handleMouseEvent(Type::mouse, at.translated(8, 8), juce::ModifierKeys(), 0.0f, 0.0f, ++now);
      pump(50);
      expect(tracked.coarse(), "a mouse move leaves the finger in charge");
      peer->handleMouseEvent(Type::mouse, at.translated(8, 8), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, ++now);
      peer->handleMouseEvent(Type::mouse, at.translated(8, 8), juce::ModifierKeys(), 0.0f, 0.0f, ++now);
      pump(50);
      expect(!tracked.coarse(), "a mouse press hands over");
    }
    window.setVisible(false);
  }
};

// Glow::compositeInto follows the rounded clip: a pixel in a corner square
// measures its distance to the arc, so it lights like a pixel that close to
// a straight edge; with no corner the straight-edge distance stands.
struct GlowCornerTests : juce::UnitTest {
  GlowCornerTests() : juce::UnitTest("Glow corners", "ui") {}

  static int red(const juce::Image& image, int x, int y) { return image.getPixelAt(x, y).getRed(); }

  void runTest() override {
    const Glow glow{juce::Colours::red, 8.0f, 0.75f};
    auto composite = [&](float corner) {
      juce::Image image(juce::Image::ARGB, 64, 64, true);
      image.clear(image.getBounds(), juce::Colours::black);
      glow.compositeInto(image, 1.0f, corner);
      return image;
    };

    beginTest("a square glow measures to the straight edges");
    const auto square = composite(0.0f);
    expect(red(square, 0, 32) > red(square, 2, 32) && red(square, 2, 32) > red(square, 6, 32), "fades inward");
    expectEquals(red(square, 6, 6), red(square, 6, 32));  // the corner is no closer than the edges
    expectEquals(red(square, 32, 32), 0);

    beginTest("a rounded glow hugs the arc");
    // Radius 16: (6, 6) is 6 px from either straight edge but only ~2.6 px
    // inside the arc, so it lights like the 2 px band.
    const auto rounded = composite(16.0f);
    expectEquals(red(rounded, 6, 6), red(rounded, 2, 32));
    expect(red(rounded, 6, 6) > red(square, 6, 6));
    // The straight runs and the interior are as before.
    expectEquals(red(rounded, 6, 32), red(square, 6, 32));
    expectEquals(red(rounded, 32, 6), red(square, 32, 6));
    expectEquals(red(rounded, 32, 32), 0);
    // The other three corners mirror.
    expectEquals(red(rounded, 57, 6), red(rounded, 6, 6));
    expectEquals(red(rounded, 6, 57), red(rounded, 6, 6));
    expectEquals(red(rounded, 57, 57), red(rounded, 6, 6));
    // Past the arc's reach the corner square is untouched: (14, 14) is
    // ~13.9 px inside the arc, beyond the 8 px blur.
    expectEquals(red(rounded, 14, 14), 0);
  }
};

// Drag-reordering a preset in the browser, through the peer. The drop
// rebuilds the row list (destroying the dragged Row) and then asks the store
// to move the preset: the move must carry the real id, not whatever is left
// in the freed row, and the browser must end up showing the store's order.
struct PresetReorderTests : juce::UnitTest {
  PresetReorderTests() : juce::UnitTest("Preset reorder", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  struct Pointer {
    juce::ComponentPeer& peer;
    juce::int64 time = juce::Time::currentTimeMillis();
    void at(juce::Point<float> pos, bool down) {
      peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pos,
                            down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f, 0.0f, ++time);
      pump(10);
    }
  };

  // The browser row showing `name`, or null.
  static juce::Component* rowNamed(juce::Component& root, const juce::String& name) {
    auto* label = drive::find(root, [&](juce::Component& c) {
      auto* b = dynamic_cast<Clickable*>(&c);
      return b != nullptr && c.isShowing() && b->accessibleName() == name && c.getHelpText().isEmpty();
    });
    return label != nullptr ? label->getParentComponent() : nullptr;
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("chrome-preset-browse");  // two user presets, three factory
    if (scenario == nullptr) {
      expect(false, "chrome-preset-browse scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("preset reorder", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    expect(peer != nullptr);
    if (peer == nullptr) return;
    auto& root = host.pluginRoot();

    beginTest("open the browser in reorder mode");
    drive::clickByHelp(root, "Presets:");
    pump(300);
    drive::clickByHelp(root, "Reorder:");
    pump(100);
    auto* first = rowNamed(root, "My Lead Tone");
    auto* second = rowNamed(root, "Church Sunday");
    auto* grip = first != nullptr ? drive::find(*first, [](juce::Component& c) {
      return c.getHelpText().startsWith("Drag:");
    }) : nullptr;
    expect(first != nullptr && second != nullptr && grip != nullptr);
    if (first == nullptr || second == nullptr || grip == nullptr) return;
    expect(first->getY() < second->getY());
    auto idOf = [&](const juce::String& name) {
      for (const auto& p : *backend.getPresetList()["presets"].getArray())
        if (p["name"].toString() == name) return p["id"].toString();
      return juce::String();
    };
    const auto firstId = idOf("My Lead Tone");
    expect(firstId.isNotEmpty());

    beginTest("dragging the first user preset below the second moves it by +1");
    Pointer pointer{*peer};
    const auto start = peer->getComponent().getLocalPoint(grip, grip->getLocalBounds().getCentre().toFloat());
    const float rowHeight = static_cast<float>(first->getHeight());
    pointer.at(start, true);
    for (int i = 1; i <= 6; ++i) pointer.at(start.translated(0, rowHeight * 1.5f * static_cast<float>(i) / 6), true);
    pointer.at(start.translated(0, rowHeight * 1.5f), false);
    pump(100);
    const auto& moves = backend.presetMoves();
    expectEquals(static_cast<int>(moves.size()), 1);
    if (!moves.empty()) {
      expectEquals(moves.back().id, firstId);
      expectEquals(moves.back().delta, 1);
    }

    beginTest("the browser shows the store's new order");
    first = rowNamed(root, "My Lead Tone");
    second = rowNamed(root, "Church Sunday");
    expect(first != nullptr && second != nullptr);
    if (first != nullptr && second != nullptr) expect(second->getY() < first->getY());
    // User section swapped; the factory section follows it untouched.
    const auto list = backend.getPresetList()["presets"];
    expectEquals(list[0]["name"].toString(), juce::String("Church Sunday"));
    expectEquals(list[1]["name"].toString(), juce::String("My Lead Tone"));
    expectEquals(list[2]["name"].toString(), juce::String("Crunch Rhythm"));
    window.setVisible(false);
  }
};

// Dragging a gallery tile across the stereo seam, through the peer. The tile
// under the pointer is the component JUCE delivers the drag to, and the live
// cross-lane reflow rebuilds both lanes: it must carry that tile over rather
// than destroy it, or the events stop and the ghost freezes where it
// crossed. The drop is one moveBlockToChain and the gallery ends up showing
// the store's new lanes.
struct ChainCrossLaneDragTests : juce::UnitTest {
  ChainCrossLaneDragTests() : juce::UnitTest("Chain cross-lane drag", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  struct Pointer {
    juce::ComponentPeer& peer;
    juce::int64 time = juce::Time::currentTimeMillis();
    void at(juce::Point<float> pos, bool down) {
      peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pos,
                            down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f, 0.0f, ++time);
      pump(10);
    }
  };

  static GalleryTile* tile(juce::Component& root, const std::string& blockId) {
    return dynamic_cast<GalleryTile*>(drive::find(root, [&](juce::Component& c) {
      auto* t = dynamic_cast<GalleryTile*>(&c);
      return t != nullptr && t->blockId() == blockId;
    }));
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");  // L: l1 l2 ins / R: r1 r2 ins
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("cross-lane drag", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    expect(peer != nullptr);
    if (peer == nullptr) return;
    auto& root = host.pluginRoot();

    auto* l1 = tile(root, "blk-l1");
    auto* r1 = tile(root, "blk-r1");
    expect(l1 != nullptr && r1 != nullptr);
    if (l1 == nullptr || r1 == nullptr) return;
    auto* leftLane = l1->getParentComponent();
    auto* rightLane = r1->getParentComponent();
    expect(leftLane != nullptr && rightLane != nullptr && leftLane != rightLane);
    const int overlayIdle = root.overlayLayer().getNumChildComponents();
    juce::Component::SafePointer<GalleryTile> dragged(l1);

    beginTest("the dragged tile survives the crossing and keeps the gesture");
    Pointer pointer{*peer};
    const auto& top = peer->getComponent();
    const auto start = top.getLocalPoint(l1, l1->getLocalBounds().getCentre().toFloat());
    // Land one slot past r1 (the lane has parted to make room there).
    const auto end = top.getLocalPoint(r1, r1->getLocalBounds().getCentre().toFloat()
                                               .translated(r1->getWidth() + gallery::kTileGap, 0.0f));
    pointer.at(start, true);
    constexpr int kSteps = 12;
    for (int i = 1; i <= kSteps; ++i) {
      pointer.at(start + (end - start) * (static_cast<float>(i) / kSteps), true);
      expect(dragged != nullptr, "the tile was destroyed mid-drag at step " + juce::String(i));
      if (dragged == nullptr) break;
    }
    if (dragged != nullptr) {
      expect(dragged->getParentComponent() == rightLane, "the tile changed lanes with its slot");
      expectEquals(root.overlayLayer().getNumChildComponents(), overlayIdle + 1);  // the ghost
    }

    beginTest("the drop is one move to the other lane");
    pointer.at(end, false);
    pump(100);
    const auto& moves = backend.chainMoves();
    expectEquals(static_cast<int>(moves.size()), 1);
    if (!moves.empty()) {
      expectEquals(juce::String(moves.back().id), juce::String("blk-l1"));
      expectEquals(moves.back().side, juce::String("right"));
      expectEquals(moves.back().index, 1);
    }
    expectEquals(root.overlayLayer().getNumChildComponents(), overlayIdle);  // ghost gone

    beginTest("the gallery shows the store's new lanes");
    const auto& state = root.services().chain.state();
    expectEquals(static_cast<int>(state.chain.size()), 2);
    expect(state.chainRight.has_value() && state.chainRight->size() == 4
           && (*state.chainRight)[1].blockId == "blk-l1");
    auto* moved = tile(root, "blk-l1");
    expect(moved != nullptr);
    if (moved != nullptr) {
      expect(moved->getParentComponent() == rightLane && moved->isShowing());
      expect(juce::exactlyEqual(moved->getAlpha(), 1.0f), "not left dimmed as travelling");
      expect(moved->getX() > r1->getX(), "sits after r1");
      expect(tile(root, "blk-l2") != nullptr && tile(root, "blk-l2")->getX() == 0, "left lane closed the gap");
    }
    window.setVisible(false);
  }
};

// The block card's LITE / FULL toggle, clicked through the peer with the
// per-block size setting on. The store refreshes synchronously inside the
// click, so the card re-syncs while the toggle's own click is still on the
// stack: the toggle must survive its own press.
struct BlockSizeToggleTests : juce::UnitTest {
  BlockSizeToggleTests() : juce::UnitTest("Block size toggle", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static Clickable* cell(juce::Component& root, const juce::String& label) {
    return dynamic_cast<Clickable*>(drive::find(root, [&](juce::Component& c) {
      auto* b = dynamic_cast<Clickable*>(&c);
      return b != nullptr && b->accessibleName() == label && c.isShowing()
             && c.findParentComponentOfClass<SegmentedText>() != nullptr;
    }));
  }

  static void click(juce::ComponentPeer& peer, juce::Component& target) {
    const auto pos = peer.getComponent().getLocalPoint(&target, target.getLocalBounds().getCentre().toFloat());
    const auto now = juce::Time::currentTimeMillis();
    using Type = juce::MouseInputSource::InputSourceType;
    peer.handleMouseEvent(Type::mouse, pos, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, now);
    peer.handleMouseEvent(Type::mouse, pos, juce::ModifierKeys(), 0.0f, 0.0f, now + 1);
    pump(30);
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-detail");
    if (scenario == nullptr) {
      expect(false, "main-detail scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("block size", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    auto& root = host.pluginRoot();
    expect(peer != nullptr);
    if (peer == nullptr) return;
    auto slimSize = [&] {  // the block the detail card shows
      for (const auto& item : root.services().chain.state().chain)
        if (item.blockId == "blk-2") return item.params.slimSize;
      return -1.0;
    };

    beginTest("the toggle appears with the per-block setting");
    expect(cell(root, "FULL") == nullptr);
    root.services().prefs.setBool(UiPrefs::kShowBlockSizeControl, true);
    pump(30);
    auto* full = cell(root, "FULL");
    auto* lite = cell(root, "LITE");
    expect(full != nullptr && lite != nullptr);
    if (full == nullptr || lite == nullptr) return;
    juce::Component::SafePointer<SegmentedText> toggle(full->findParentComponentOfClass<SegmentedText>());
    expect(toggle->isOn(0) && !toggle->isOn(1));

    beginTest("a click on FULL selects it, reaches the backend and keeps the toggle alive");
    click(*peer, *full);
    expect(isSlimSizeFull(slimSize()));
    expect(toggle != nullptr);  // the click handler's owner was not rebuilt under it
    if (toggle == nullptr) return;
    expect(!toggle->isOn(0) && toggle->isOn(1));

    beginTest("and back to LITE");
    click(*peer, *lite);
    expect(!isSlimSizeFull(slimSize()));
    expect(toggle != nullptr);
    if (toggle == nullptr) return;
    expect(toggle->isOn(0) && !toggle->isOn(1));
    window.setVisible(false);
  }
};

// The block card's "=" button shows when the card opens with Per-Block
// Normalization on, and follows the setting while open (issue #269).
struct BlockNormalizeControlTests : juce::UnitTest {
  BlockNormalizeControlTests() : juce::UnitTest("Block normalize control", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static Clickable* named(juce::Component& root, const juce::String& name) {
    return dynamic_cast<Clickable*>(drive::find(root, [&](juce::Component& c) {
      auto* b = dynamic_cast<Clickable*>(&c);
      return b != nullptr && b->accessibleName() == name && c.isShowing()
             && c.findParentComponentOfClass<BlockCard>() != nullptr;
    }));
  }
  static Clickable* button(juce::Component& root) { return named(root, "Normalize"); }

  // The scenario with the setting already saved.
  static Scenario withSetting(const Scenario& base, bool on) {
    Scenario copy{base.id, base.data.clone(), base.hasDrive};
    auto* storage = new juce::DynamicObject();
    storage->setProperty(UiPrefs::kShowBlockNormalizeControl, on ? "true" : "false");
    copy.data.getDynamicObject()->setProperty("localStorage", juce::var(storage));
    return copy;
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* detail = fixtures.find("main-detail");
    if (detail == nullptr) {
      expect(false, "main-detail scenario missing");
      return;
    }

    beginTest("a card opened with the setting on shows the button, beside the Out knob");
    {
      MockBackend backend(detail->data);
      juce::DocumentWindow window("block normalize", juce::Colours::black, 0);
      ScaledHost host(backend, withSetting(*detail, true), fixtures.root);
      window.setContentNonOwned(&host, true);
      window.setVisible(true);
      pump(400);
      auto& root = host.pluginRoot();
      auto* card = dynamic_cast<BlockCard*>(
          drive::find(root, [](juce::Component& c) { return dynamic_cast<BlockCard*>(&c) != nullptr; }));
      expect(card != nullptr);
      if (card == nullptr) return;
      auto* normalize = button(root);
      expect(normalize != nullptr);
      if (normalize != nullptr) {
        // Laid out in the card's bottom-right corner.
        const auto inCard = card->getLocalArea(normalize, normalize->getLocalBounds());
        expect(!inCard.isEmpty() && card->getLocalBounds().contains(inCard));
        expect(inCard.getCentreX() > card->getWidth() / 2 && inCard.getCentreY() > card->getHeight() / 2);
      }

      beginTest("the button follows the setting while the card is open");
      root.services().prefs.setBool(UiPrefs::kShowBlockNormalizeControl, false);
      pump(30);
      expect(button(root) == nullptr);
      root.services().prefs.setBool(UiPrefs::kShowBlockNormalizeControl, true);
      pump(30);
      expect(button(root) != nullptr);

      beginTest("the EQ view hides it and the tone view brings it back");
      auto* eq = named(root, "EQ");
      expect(eq != nullptr);
      if (eq == nullptr) return;
      drive::click(root, *eq);
      pump(30);
      expect(button(root) == nullptr);
      // A setting change must not show it over the EQ.
      root.services().prefs.setBool(UiPrefs::kShowBlockNormalizeControl, false);
      root.services().prefs.setBool(UiPrefs::kShowBlockNormalizeControl, true);
      pump(30);
      expect(button(root) == nullptr);
      drive::click(root, *eq);
      pump(30);
      expect(button(root) != nullptr);
      window.setVisible(false);
    }

    beginTest("a card opened with the setting off has no button");
    {
      MockBackend backend(detail->data);
      juce::DocumentWindow window("block normalize", juce::Colours::black, 0);
      ScaledHost host(backend, withSetting(*detail, false), fixtures.root);
      window.setContentNonOwned(&host, true);
      window.setVisible(true);
      pump(400);
      auto& root = host.pluginRoot();
      expect(named(root, "EQ") != nullptr);  // the card is up
      expect(button(root) == nullptr);
      window.setVisible(false);
    }
  }
};

// An IR block's card shows whether the IR runs stereo or mono, and why. The
// glyph is a button only where the choice is the user's, and the output
// meter splits while an IR runs stereo (issue #122).
struct IrImageTests : juce::UnitTest {
  IrImageTests() : juce::UnitTest("IR stereo indicator", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static Clickable* button(juce::Component& root) {
    return dynamic_cast<Clickable*>(drive::find(root, [](juce::Component& c) {
      const auto help = c.getHelpText();
      return dynamic_cast<Clickable*>(&c) != nullptr && c.isShowing() &&
             (help.startsWith("Stereo IR") || help.startsWith("Mono IR")) &&
             c.findParentComponentOfClass<BlockCard>() != nullptr;
    }));
  }

  static bool meterSplit(juce::Component& root) {
    auto* out = dynamic_cast<DbMeter*>(
        drive::find(root, [](juce::Component& c) { return c.getName() == "output meter"; }));
    return out != nullptr && out->stereo();
  }

  // The ids of the Out meter columns the card shows, left to right.
  static juce::String outColumns(juce::Component& root) {
    juce::StringArray ids;
    drive::find(root, [&](juce::Component& c) {
      auto* m = dynamic_cast<LiveDotMeter*>(&c);
      if (m != nullptr && c.isShowing() && m->id().contains(":out")) ids.add(m->id());
      return false;
    });
    ids.sort(false);
    return ids.joinIntoString("|");
  }

  // What the processor would report for the scenario's IR block (blk-3).
  static void report(juce::var chain, const char* image) {
    for (auto& block : *chain["chain"].getArray())
      if (block["blockId"].toString() == "blk-3") block.getDynamicObject()->setProperty("irImage", image);
    chain.getDynamicObject()->setProperty("stereoIrActive", juce::String(image) == "stereo");
  }

  static juce::String reported(MockBackend& backend) {
    for (const auto& block : *backend.getChainState(-1)["chain"].getArray())
      if (block["blockId"].toString() == "blk-3") return block["irImage"].toString();
    return {};
  }

  // main-detail with the card open on `blockId` and the IR reporting `image`.
  static Scenario opened(const Scenario& base, const char* blockId, const char* image) {
    Scenario copy{base.id, base.data.clone(), base.hasDrive};
    copy.data["sessionStorage"].getDynamicObject()->setProperty("t3k.detailBlockId", blockId);
    report(copy.data["chain"], image);
    return copy;
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* detail = fixtures.find("main-detail");
    if (detail == nullptr) {
      expect(false, "main-detail scenario missing");
      return;
    }

    {
      const auto scenario = opened(*detail, "blk-3", "stereo");
      MockBackend backend(scenario.data);
      juce::DocumentWindow window("ir stereo", juce::Colours::black, 0);
      ScaledHost host(backend, scenario, fixtures.root);
      window.setContentNonOwned(&host, true);
      window.setVisible(true);
      pump(400);
      auto& root = host.pluginRoot();

      beginTest("a stereo IR shows a lit button and splits the output meter");
      auto* stereo = button(root);
      expect(stereo != nullptr);
      if (stereo == nullptr) return;
      expectEquals(stereo->getHelpText(), help::text(help::Key::irStereo));
      expect(stereo->isEnabled());
      expect(meterSplit(root));
      expectEquals(outColumns(root), juce::String("block:blk-3:out:l|block:blk-3:out:r"));

      beginTest("a click picks mono, and the meters go back to one column");
      drive::click(root, *stereo);
      pump(30);
      expectEquals(reported(backend), juce::String("mono-user"));
      expectEquals(stereo->getHelpText(), help::text(help::Key::irMonoUser));
      expect(stereo->isEnabled());
      expect(!meterSplit(root));
      expectEquals(outColumns(root), juce::String("block:blk-3:out"));

      beginTest("a second click brings stereo back");
      drive::click(root, *stereo);
      pump(30);
      expectEquals(reported(backend), juce::String("stereo"));
      expectEquals(stereo->getHelpText(), help::text(help::Key::irStereo));
      expect(meterSplit(root));
      expectEquals(outColumns(root), juce::String("block:blk-3:out:l|block:blk-3:out:r"));

      beginTest("Spread on: the IR reads mono, names Spread, and has no choice to make");
      auto* spread = root.services().backend.parameter("spreadEnabled");
      spread->setValueNotifyingHost(1.0f);
      pump(30);
      expectEquals(stereo->getHelpText(), help::text(help::Key::irMonoSpread));
      expect(!stereo->isEnabled());
      expectEquals(outColumns(root), juce::String("block:blk-3:out"));
      drive::click(root, *stereo);
      pump(30);
      expectEquals(reported(backend), juce::String("stereo"));
      spread->setValueNotifyingHost(0.0f);
      pump(30);
      expectEquals(stereo->getHelpText(), help::text(help::Key::irStereo));
      expect(stereo->isEnabled());

      beginTest("a mono reason from the chain or the rig is display only");
      const std::pair<const char*, help::Key> reasons[] = {
          {"mono-file", help::Key::irMonoFile},
          {"mono-nam-after", help::Key::irMonoNamAfter},
          {"mono-stereo-chains", help::Key::irMonoStereoChains},
          {"mono-dual-mono", help::Key::irMonoDualMono},
          {"mono-output", help::Key::irMonoOutput},
      };
      for (const auto& [image, key] : reasons) {
        auto chain = backend.getChainState(-1);
        report(chain, image);
        backend.setChain(chain);
        // The store sees the chain on a clock tick, which a slow runner can hold past a fixed pump.
        LiveScenario::until([&] { return stereo->getHelpText() == help::text(key); });
        expectEquals(stereo->getHelpText(), help::text(key), image);
        expect(!stereo->isEnabled(), image);
        drive::click(root, *stereo);
        pump(30);
        expectEquals(reported(backend), juce::String(image));
        expect(!meterSplit(root), image);
      }
      window.setVisible(false);
    }

    beginTest("a block's output level parses per channel, or as one value for both");
    {
      const auto levels = MeterLevels::parse(juce::JSON::parse(
          R"({"blocks": {"a": {"in": -20, "out": -6, "outLR": [-6, -12]}, "b": {"in": -20, "out": -9}}})"));
      expectEquals(static_cast<int>(levels.blocks.size()), 2);
      if (levels.blocks.size() != 2) return;
      expectEquals(levels.blocks[0].outLR[0], -6.0f);
      expectEquals(levels.blocks[0].outLR[1], -12.0f);
      expectEquals(levels.blocks[1].outLR[0], -9.0f);
      expectEquals(levels.blocks[1].outLR[1], -9.0f);
    }

    beginTest("a NAM block has no stereo/mono button");
    {
      const auto scenario = opened(*detail, "blk-2", "stereo");
      MockBackend backend(scenario.data);
      juce::DocumentWindow window("ir stereo", juce::Colours::black, 0);
      ScaledHost host(backend, scenario, fixtures.root);
      window.setContentNonOwned(&host, true);
      window.setVisible(true);
      pump(400);
      auto& root = host.pluginRoot();
      expect(drive::find(root, [](juce::Component& c) { return dynamic_cast<BlockCard*>(&c) != nullptr; }) != nullptr);
      expect(button(root) == nullptr);
      window.setVisible(false);
    }
  }
};

// Committing a typed CC number swaps the listening row for a mapping row of
// the same height. The form host only re-flows on a height change, so the
// card has to place its new rows itself or they sit at 0×0 and the card
// paints as an empty box until Settings is reopened (issue #220).
struct MidiMapCommitTests : juce::UnitTest {
  MidiMapCommitTests() : juce::UnitTest("MIDI mapping commit", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static juce::Component* named(juce::Component& root, const juce::String& name) {
    return drive::find(root, [&](juce::Component& c) { return c.getName() == name && c.isShowing(); });
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("settings-midi-empty");
    if (scenario == nullptr) {
      expect(false, "settings-midi-empty scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("midi commit", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    window.setVisible(true);
    pump(400);
    auto& root = host.pluginRoot();
    drive::openSettings(root);
    drive::scrollSettingsTo(root, "MIDI Mapping");
    pump(100);

    beginTest("choosing a control arms a listening row with a CC field");
    expect(named(root, "CC number") == nullptr);
    root.services().midiMap.startLearn("outputLevel");
    pump(50);
    juce::Component::SafePointer<juce::Component> cc(named(root, "CC number"));
    expect(cc != nullptr);
    if (cc == nullptr) return;
    expect(!cc->getParentComponent()->getBounds().isEmpty());

    beginTest("Return commits once the key event has unwound, not under the field's own callback");
    drive::submit(root, "CC #", "15");
    const auto& state = root.services().midiMap.state();
    expect(cc != nullptr, "the CC field was destroyed inside its Return handler");
    expect(state && state->learnTargetId == "outputLevel");  // still armed until the deferred commit
    pump(50);
    expect(cc == nullptr);
    expect(state && state->learnTargetId.isEmpty());
    const auto* mapping = state ? state->mappingFor("outputLevel") : nullptr;
    expect(mapping != nullptr && mapping->number == 15);

    beginTest("the mapping row shows in place");
    expect(named(root, "CC number") == nullptr);
    juce::Component::SafePointer<juce::Component> remove(named(root, "Remove"));
    expect(remove != nullptr, "no mapping row after commit");
    if (remove == nullptr) return;
    // The row was placed inside the card, not left at 0×0.
    auto* row = remove->getParentComponent();
    expect(row != nullptr && !row->getBounds().isEmpty(), "mapping row has no bounds");
    expect(row != nullptr && row->getBounds().contains(remove->getBounds()));
    expect(row != nullptr && row->getWidth() == row->getParentComponent()->getWidth() - 2);

    beginTest("Remove outlives its own click, then the list empties");
    auto* removeButton = dynamic_cast<juce::Button*>(remove.getComponent());
    expect(removeButton != nullptr && removeButton->onClick != nullptr);
    if (removeButton == nullptr || removeButton->onClick == nullptr) return;
    removeButton->onClick();  // the handler itself; triggerClick() and drive::click() both pump
    expect(remove != nullptr, "the Remove button was destroyed inside its click handler");
    expect(state && !state->mappings.empty());  // the change waits for the click to unwind
    pump(50);
    expect(remove == nullptr);
    expect(state && state->mappings.empty());
    expect(named(root, "Remove") == nullptr);
    window.setVisible(false);
  }
};

// The tuner's speaker toggle: each click flips the backend mute and swaps the
// button's name (its accessible title), and the choice is saved, so the
// tuner reopens the way it was left.
struct TunerMuteTests : juce::UnitTest {
  TunerMuteTests() : juce::UnitTest("Tuner mute", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static juce::Button* named(juce::Component& root, const juce::String& name) {
    return dynamic_cast<juce::Button*>(
        drive::find(root, [&](juce::Component& c) { return c.getName() == name && c.isShowing(); }));
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("tuner mute", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    window.setVisible(true);
    pump(400);
    auto& root = host.pluginRoot();
    root.setTunerShown(true);
    pump(100);

    beginTest("the tuner opens unmuted");
    auto* mute = named(root, "Mute output");
    expect(mute != nullptr, "no mute toggle on the tuner");
    if (mute == nullptr) return;
    expect(!backend.tunerMuted());

    beginTest("a click mutes, and the toggle offers to unmute");
    mute->onClick();
    expect(backend.tunerMuted());
    expect(named(root, "Mute output") == nullptr);
    expect(named(root, "Unmute output") == mute);

    beginTest("a second click unmutes");
    mute->onClick();
    expect(!backend.tunerMuted());
    expect(named(root, "Mute output") == mute);

    beginTest("closing a muted tuner brings the output back");
    mute->onClick();
    root.setTunerShown(false);
    pump(50);
    expect(!backend.tunerMuted());

    beginTest("a reopened tuner is still muted");
    root.setTunerShown(true);
    pump(100);
    expect(backend.tunerMuted());
    auto* unmute = named(root, "Unmute output");
    expect(unmute != nullptr);
    if (unmute == nullptr) return;

    beginTest("unmuting is kept the same way");
    unmute->onClick();
    root.setTunerShown(false);
    pump(50);
    root.setTunerShown(true);
    pump(100);
    expect(!backend.tunerMuted());
    expect(named(root, "Mute output") != nullptr);
    window.setVisible(false);

    beginTest("a new editor opens the tuner muted too");
    Scenario saved{scenario->id, scenario->data.clone(), scenario->hasDrive};
    auto* storage = new juce::DynamicObject();
    storage->setProperty(UiPrefs::kTunerMuted, "true");
    saved.data.getDynamicObject()->setProperty("localStorage", juce::var(storage));
    MockBackend nextBackend(saved.data);
    ScaledHost nextHost(nextBackend, saved, fixtures.root);
    nextHost.pluginRoot().setTunerShown(true);
    pump(100);
    expect(nextBackend.tunerMuted());
  }
};

// The preset browser's "PC n" label gets a box at least as wide as its text;
// a narrower one draws as "PC…" (issue #257).
struct PresetPcLabelTests : juce::UnitTest {
  PresetPcLabelTests() : juce::UnitTest("Preset PC label", "ui") {}

  void runTest() override {
    beginTest("every program number fits its label");
    for (int pc = 0; pc < 128; ++pc) {
      const auto text = Fonts::width(Fonts::mono(PresetBar::kPcLabelPx), "PC " + juce::String(pc));
      expect(static_cast<float>(PresetBar::pcLabelWidth(pc)) >= text, "PC " + juce::String(pc));
    }
  }
};

// A MIDI control mapped to the tuner opens and closes the tuner screen, and
// leaves Settings to show it.
struct TunerMidiToggleTests : juce::UnitTest {
  TunerMidiToggleTests() : juce::UnitTest("Tuner MIDI toggle", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-mono");
    if (scenario == nullptr) {
      expect(false, "main-mono scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    ScaledHost host(backend, *scenario, fixtures.root);
    pump(100);
    auto& root = host.pluginRoot();

    beginTest("the tuner is a mappable toggle");
    const auto* target = midi::targetById("tuner");
    expect(target != nullptr && target->kind == midi::TargetKind::toggle);

    beginTest("each press opens or closes the tuner");
    expect(!root.tunerShown());
    backend.fireTunerToggle();
    expect(root.tunerShown());
    backend.fireTunerToggle();
    expect(!root.tunerShown());

    beginTest("opening it from Settings closes Settings");
    root.openSettings();
    pump(50);
    expect(root.settings() != nullptr);
    backend.fireTunerToggle();
    pump(50);
    expect(root.tunerShown());
    expect(root.settings() == nullptr);
  }
};

// A readout wider than its knob ("-100 dB" under the 36px gate, whose dim
// group is exactly the knob's width) shows whole: it floats in the overlay
// layer, past the column and the parents that clip the knob.
struct KnobReadoutTests : juce::UnitTest {
  KnobReadoutTests() : juce::UnitTest("Knob readout", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static int whitePixels(const juce::Image& image, juce::Rectangle<int> area) {
    int n = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
      for (int x = area.getX(); x < area.getRight(); ++x)
        if (image.getBounds().contains(x, y) && image.getPixelAt(x, y).getBrightness() > 0.85f)
          ++n;
    return n;
  }

  // The overlay child over the knob's column, if the readout is floating.
  static juce::Component* floatingReadout(PluginRoot& root, Knob& knob) {
    const auto column = root.getLocalArea(&knob, knob.getLocalBounds());
    for (auto* c : root.overlayLayer().getChildren())
      if (c->isVisible() && root.getLocalArea(c, c->getLocalBounds()).intersects(column))
        return c;
    return nullptr;
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-stereo");
    if (scenario == nullptr) {
      expect(false, "main-stereo scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("knob readout", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto* peer = host.getPeer();
    auto& root = host.pluginRoot();
    expect(peer != nullptr);
    if (peer == nullptr) return;
    auto* knob = dynamic_cast<Knob*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.getTitle() == "Gate" && c.isShowing();
    }));
    expect(knob != nullptr, "gate knob showing");
    if (knob == nullptr) return;

    // The strips either side of the column on the label row (host px), where
    // a readout that spills past the knob lands.
    const auto labelRow = host.getLocalArea(
        knob, knob->getLocalBounds().removeFromBottom(Knob::kLabelSlot + Knob::kEditorOverflow));
    const int reach = juce::roundToInt(labelRow.getHeight() * 1.5f);
    const auto sides = [&](const juce::Image& shot) {
      return whitePixels(shot, labelRow.withX(labelRow.getX() - reach).withWidth(reach))
             + whitePixels(shot, labelRow.withX(labelRow.getRight()).withWidth(reach));
    };
    const int idle = sides(host.createComponentSnapshot(host.getLocalBounds()));
    expect(floatingReadout(root, *knob) == nullptr);

    beginTest("a drag to the end of travel floats the readout past the column");
    auto time = juce::Time::currentTimeMillis();
    const auto at = [&](juce::Point<float> knobPos, bool down) {
      const auto pos = peer->getComponent().getLocalPoint(knob, knobPos);
      const auto mods = down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys();
      peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pos, mods, 0.0f, 0.0f,
                             ++time);
      pump(10);
    };
    const auto face = knob->faceBounds().getCentre().toFloat();
    at(face, true);
    for (int step = 1; step <= 20; ++step) at(face.translated(0.0f, 20.0f * step), true);
    pump(250);  // the readout swap is debounced on press
    expect(juce::exactlyEqual(knob->value(), 0.0f), "dragged to the minimum");
    auto* readout = floatingReadout(root, *knob);
    expect(readout != nullptr, "readout floats in the overlay");
    if (readout != nullptr) {
      const auto box = root.getLocalArea(readout, readout->getLocalBounds());
      const auto column = root.getLocalArea(knob, knob->getLocalBounds());
      expect(box.getWidth() > column.getWidth() && box.getCentreX() == column.getCentreX());
    }
    expect(sides(host.createComponentSnapshot(host.getLocalBounds())) > idle,
           "text reaches past the knob");

    beginTest("the release lets it go");
    at(face.translated(0.0f, 400.0f), false);
    pump(400);  // past the hold
    expect(floatingReadout(root, *knob) == nullptr);
    expectEquals(sides(host.createComponentSnapshot(host.getLocalBounds())), idle);
    window.setVisible(false);
  }
};

// The faceplate's effects cluster: gate shows and pitch hides by
// default, the Effects view settings flip either, a powered effect shows
// regardless, and the plate re-spreads around whatever is showing.
struct FaceplateEffectsTests : juce::UnitTest {
  FaceplateEffectsTests() : juce::UnitTest("Faceplate effects", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static Knob* knob(PluginRoot& root, const juce::String& title) {
    return dynamic_cast<Knob*>(drive::find(root, [&](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.getTitle() == title && c.isShowing();
    }));
  }

  // The Bass knob's left edge in root coordinates: where the tone stack landed.
  static int toneX(PluginRoot& root) {
    auto* bass = knob(root, "Bass");
    return bass == nullptr ? -1 : root.getLocalArea(bass, bass->getLocalBounds()).getX();
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* scenario = fixtures.find("main-mono");
    if (scenario == nullptr) {
      expect(false, "main-mono scenario missing");
      return;
    }
    MockBackend backend(scenario->data);
    juce::DocumentWindow window("faceplate effects", juce::Colours::black, 0);
    ScaledHost host(backend, *scenario, fixtures.root);
    window.setContentNonOwned(&host, true);
    showTestWindow(window);
    pump(400);
    auto& root = host.pluginRoot();
    auto& prefs = root.services().prefs;
    auto power = [&](const char* id, bool on) {
      if (auto* p = root.services().backend.parameter(id)) p->setValueNotifyingHost(on ? 1.0f : 0.0f);
      pump(30);
    };

    beginTest("gate shows and pitch hides by default");
    expect(knob(root, "Gate") != nullptr);
    expect(knob(root, "Pitch") == nullptr);
    const int gateOnly = toneX(root);

    beginTest("the Pitch Shift view setting brings its group out and the plate re-spreads");
    prefs.setBool(UiPrefs::kShowPitchControl, true);
    pump(30);
    auto* pitch = knob(root, "Pitch");
    auto* gate = knob(root, "Gate");
    expect(pitch != nullptr && gate != nullptr);
    const int both = toneX(root);
    expect(both > gateOnly, "the tone stack moves over for the wider cluster");
    if (pitch != nullptr && gate != nullptr) {
      // Grouped: the pair sits closer together than the cluster does to the
      // tone stack.
      const auto g = root.getLocalArea(gate, gate->getLocalBounds());
      const auto t = root.getLocalArea(pitch, pitch->getLocalBounds());
      const int between = t.getX() - g.getRight();
      expect(between > 0 && between < both - t.getRight(), "gate and pitch read as one cluster");
    }

    beginTest("off again hides it, unless the effect is powered");
    prefs.setBool(UiPrefs::kShowPitchControl, false);
    pump(30);
    expect(knob(root, "Pitch") == nullptr);
    power("pitchEnabled", true);
    expect(knob(root, "Pitch") != nullptr, "a powered effect shows regardless of the view setting");
    expectEquals(toneX(root), both);
    power("pitchEnabled", false);
    expect(knob(root, "Pitch") == nullptr, "switching it off lets the setting hide it again");
    expectEquals(toneX(root), gateOnly);

    beginTest("with no effects showing, four peers spread");
    prefs.setBool(UiPrefs::kShowGateControl, false);
    pump(30);
    expect(knob(root, "Gate") != nullptr, "the mock's gate is powered, so it stays");
    power("gateEnabled", false);
    expect(knob(root, "Gate") == nullptr);
    expect(toneX(root) < gateOnly, "the tone stack moves back toward the input");
    expect(knob(root, "Input") != nullptr && knob(root, "Output") != nullptr);

    beginTest("the settings restore the defaults");
    prefs.remove(UiPrefs::kShowGateControl);
    pump(30);
    expect(knob(root, "Gate") != nullptr);
    expectEquals(toneX(root), gateOnly);
    window.setVisible(false);
  }
};

// The input-mode menu and the chrome that follows Dual Mono: on a mono
// chain with a stereo source the menu offers Stereo SUM / Stereo Dual Mono / Left / Right,
// picking Dual Mono brings out Bal and Auto Balance, dims Spread with its own
// reason and splits the output meter; with stereo chains the row is gone and
// the stereo feed is labelled Stereo.
struct FaceplateDualMonoTests : juce::UnitTest {
  FaceplateDualMonoTests() : juce::UnitTest("Faceplate dual mono", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  static Knob* knob(PluginRoot& root, const juce::String& title) {
    return dynamic_cast<Knob*>(drive::find(root, [&](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.getTitle() == title && c.isShowing();
    }));
  }

  static juce::Component* shownWithHelp(PluginRoot& root, const juce::String& prefix) {
    return drive::find(root, [&](juce::Component& c) {
      return c.isShowing() && c.getHelpText().startsWith(prefix);
    });
  }

  static DimGroup* spreadGroup(PluginRoot& root) {
    return dynamic_cast<DimGroup*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<DimGroup*>(&c) != nullptr && c.getHelpText().startsWith("Spread");
    }));
  }

  static DbMeter* meter(PluginRoot& root, const char* name) {
    return dynamic_cast<DbMeter*>(
        drive::find(root, [name](juce::Component& c) { return c.getName() == name; }));
  }

  // The open popover's rows, top to bottom: the Clickables inside the one
  // showing Popover (the input-mode menu is the only popover these tests open).
  static std::vector<Clickable*> menuRowButtons(PluginRoot& root) {
    std::vector<Clickable*> rows;
    auto* menu = drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Popover*>(&c) != nullptr && c.isShowing();
    });
    if (menu == nullptr) return rows;
    for (auto* child : menu->getChildren())
      if (auto* row = dynamic_cast<Clickable*>(child); row != nullptr && row->isShowing()) rows.push_back(row);
    std::sort(rows.begin(), rows.end(), [](Clickable* a, Clickable* b) { return a->getY() < b->getY(); });
    return rows;
  }

  static juce::StringArray menuRows(PluginRoot& root) {
    juce::StringArray labels;
    for (auto* r : menuRowButtons(root)) labels.add(r->getName());
    return labels;
  }

  // The row's help is the table entry for its key, whatever the copy says.
  void expectRowHelp(PluginRoot& root, const juce::String& label, help::Key key) {
    for (auto* r : menuRowButtons(root))
      if (r->getName() == label) {
        expectEquals(r->getHelpText(), help::text(key), "help for " + label);
        return;
      }
    expect(false, "no menu row named " + label);
  }

  void pick(PluginRoot& root, const juce::String& row) {
    drive::clickByHelp(root, "Input Mode:");
    pump(50);
    auto* target = drive::buttonNamed(root, row);
    expect(target != nullptr, "menu row present: " + row);
    if (target == nullptr) {
      drive::clickByHelp(root, "Input Mode:");
      pump(30);
      return;
    }
    drive::click(root, *target);
    pump(50);
  }

  void runTest() override {
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    {
      const auto* scenario = fixtures.find("chrome-input-mode");  // mono chain, stereo source
      if (scenario == nullptr) {
        expect(false, "chrome-input-mode scenario missing");
        return;
      }
      MockBackend backend(scenario->data);
      juce::DocumentWindow window("faceplate dual mono", juce::Colours::black, 0);
      ScaledHost host(backend, *scenario, fixtures.root);
      window.setContentNonOwned(&host, true);
      showTestWindow(window);
      pump(400);
      auto& root = host.pluginRoot();

      beginTest("a mono chain on a stereo source offers Stereo SUM, Stereo Dual Mono, Left and Right");
      drive::clickByHelp(root, "Input Mode:");
      pump(50);
      expectEquals(menuRows(root).joinIntoString("|"),
                   juce::String("Stereo SUM (L+R)|Stereo Dual Mono (L&R)|Left|Right"));
      expectRowHelp(root, "Stereo SUM (L+R)", help::Key::inputModeSum);
      expectRowHelp(root, "Stereo Dual Mono (L&R)", help::Key::inputModeDualMono);
      drive::clickByHelp(root, "Input Mode:");  // toggles the menu closed
      pump(50);
      expect(knob(root, "Bal") == nullptr, "no balance to trim with one voice");
      expect(shownWithHelp(root, "Auto Balance") == nullptr);
      auto* spread = spreadGroup(root);
      expect(spread != nullptr && !spread->isOff(), "a stereo rig keeps Spread available");
      auto* out = meter(root, "output meter");
      expect(out != nullptr && !out->stereo(), "a mono chain without spread meters one channel");

      beginTest("Dual Mono brings out Bal and Auto Balance, dims Spread and splits the meter");
      pick(root, "Stereo Dual Mono (L&R)");
      expectEquals(backend.getChainState(-1)["inputMode"].toString(), juce::String("dual"));
      expect(static_cast<bool>(backend.getChainState(-1)["dualMonoActive"]));
      expect(knob(root, "Bal") != nullptr);
      expect(shownWithHelp(root, "Auto Balance") != nullptr);
      spread = spreadGroup(root);
      expect(spread != nullptr && spread->isOff());
      if (spread != nullptr)
        expect(spread->getHelpText().startsWith("Spread off in Dual Mono"), "the dim reason names Dual Mono");
      out = meter(root, "output meter");
      expect(out != nullptr && out->stereo(), "two real channels leave the chain");
      auto* in = meter(root, "input meter");
      expect(in != nullptr && in->stereo(), "both source channels are in play");

      beginTest("Left is one voice again");
      pick(root, "Left");
      expectEquals(backend.getChainState(-1)["inputMode"].toString(), juce::String("left"));
      expect(knob(root, "Bal") == nullptr);
      expect(shownWithHelp(root, "Auto Balance") == nullptr);
      spread = spreadGroup(root);
      expect(spread != nullptr && !spread->isOff());
      out = meter(root, "output meter");
      expect(out != nullptr && !out->stereo());
      in = meter(root, "input meter");
      expect(in != nullptr && !in->stereo(), "a one-channel feed meters one channel");

      beginTest("Stereo SUM (L+R) restores the default");
      pick(root, "Stereo SUM (L+R)");
      expectEquals(backend.getChainState(-1)["inputMode"].toString(), juce::String("stereo"));
      expect(!static_cast<bool>(backend.getChainState(-1)["dualMonoActive"]));
      in = meter(root, "input meter");
      expect(in != nullptr && in->stereo());
      window.setVisible(false);
    }

    {
      const auto* scenario = fixtures.find("main-stereo");  // stereo chains, stereo source
      if (scenario == nullptr) {
        expect(false, "main-stereo scenario missing");
        return;
      }
      MockBackend backend(scenario->data);
      backend.setInputMode("dual");  // a stale session value: stereo chains have no dual mono
      juce::DocumentWindow window("faceplate dual mono (stereo chains)", juce::Colours::black, 0);
      ScaledHost host(backend, *scenario, fixtures.root);
      window.setContentNonOwned(&host, true);
      showTestWindow(window);
      pump(400);
      auto& root = host.pluginRoot();

      beginTest("stereo chains offer Stereo, Left and Right, and never Dual Mono");
      expect(!static_cast<bool>(backend.getChainState(-1)["dualMonoActive"]));
      drive::clickByHelp(root, "Input Mode:");
      pump(50);
      expectEquals(menuRows(root).joinIntoString("|"), juce::String("Stereo|Left|Right"));
      expectRowHelp(root, "Stereo", help::Key::inputModeStereo);
      drive::clickByHelp(root, "Input Mode:");
      pump(50);
      expect(knob(root, "Bal") != nullptr, "two chains always have a balance");
      auto* spread = spreadGroup(root);
      expect(spread != nullptr && !spread->isOff(), "Spread stays live for stereo chains");
      window.setVisible(false);
    }
  }
};

// core/WindowFit: the width picks the scale, a taller box stretches the
// column, a shorter one letterboxes; the constrainer's minimum height follows
// the width. Then the screens' response to a stretched column, through the
// same ScaledHost the captures use: header, faceplate and hint bar hold their
// heights, the titles stay put and the lanes / block card centre in the room.
struct WindowFitTests : juce::UnitTest {
  WindowFitTests() : juce::UnitTest("Window fit", "ui") {}

  static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

  template <typename T>
  static T* findView(juce::Component& root) {
    return dynamic_cast<T*>(drive::find(root, [](juce::Component& c) { return dynamic_cast<T*>(&c) != nullptr; }));
  }

  // The scenario with the column stretched by `stretch` design px.
  static Scenario stretched(const Scenario& base, int stretch) {
    Scenario copy{base.id, base.data.clone(), base.hasDrive};
    copy.data.getDynamicObject()->setProperty("stretch", stretch);
    return copy;
  }

  void runTest() override {
    using design::fit;
    constexpr int kMinH = design::kHeight + design::kHintHeight;  // 614: the design box with the hint bar

    beginTest("the design box at any scale fits exactly");
    expectWithinAbsoluteError(fit(1024, kMinH, kMinH).scale, 1.0, 1e-9);
    expectEquals(fit(1024, kMinH, kMinH).stretch, 0);
    expectWithinAbsoluteError(fit(2048, 2 * kMinH, kMinH).scale, 2.0, 1e-9);
    expectEquals(fit(2048, 2 * kMinH, kMinH).stretch, 0);

    beginTest("a taller box keeps the width's scale and stretches by the surplus");
    const auto ipad = fit(1024, 768, kMinH);  // a 4:3 screen at 1x
    expectWithinAbsoluteError(ipad.scale, 1.0, 1e-9);
    expectEquals(ipad.stretch, 768 - kMinH);
    const auto scaled = fit(1536, 1152, kMinH);  // the same shape at 1.5x
    expectWithinAbsoluteError(scaled.scale, 1.5, 1e-9);
    expectEquals(scaled.stretch, 768 - kMinH);
    expectEquals(fit(1024, 614 + 7, kMinH).stretch, 7);

    beginTest("the stretch is capped");
    expectEquals(fit(1024, kMinH + design::kMaxStretch + 300, kMinH).stretch, design::kMaxStretch);

    beginTest("a box too short for its width scales by height and letterboxes, with no stretch");
    const auto squat = fit(1024, 500, kMinH);
    expectWithinAbsoluteError(squat.scale, 500.0 / kMinH, 1e-9);
    expectEquals(squat.stretch, 0);
    expect(fit(0, 0, kMinH).scale > 0.0, "an unsized box still fits at a positive scale");

    beginTest("the constrainer's height range follows the width");
    design::FitConstrainer c;
    c.setMinDesignHeight(kMinH);
    expectEquals(c.minHeightFor(1024), kMinH);
    expectEquals(c.minHeightFor(2048), 2 * kMinH);
    expectEquals(c.maxHeightFor(1024), kMinH + design::kMaxStretch);
    const juce::Rectangle<int> limits(0, 0, 10000, 10000);
    auto check = [&](juce::Rectangle<int> b, bool top = false) {
      const auto previous = b;
      c.checkBounds(b, previous, limits, top, false, !top, true);
      return b;
    };
    expectEquals(check({0, 0, 1024, 500}).getHeight(), kMinH);                     // under the minimum: raised
    expectEquals(check({0, 0, 1536, 700}).getHeight(), c.minHeightFor(1536));      // wider: the minimum rises with it
    expectEquals(check({0, 0, 1024, 700}).getHeight(), 700);                       // taller is allowed
    expectEquals(check({0, 0, 1024, 5000}).getHeight(), c.maxHeightFor(1024));     // up to the cap
    expectEquals(check({0, 0, 800, 400}).getWidth(), design::kWidth);              // the width floors at 1x
    expectEquals(check({0, 0, 800, 400}).getHeight(), kMinH);
    expectEquals(check({0, 0, 3000, 3000}).getWidth(), juce::roundToInt(design::kWidth * design::kMaxScale));
    expectEquals(check({0, 100, 1024, 500}, /*top=*/true).getBottom(), 600);        // a top drag keeps the bottom
    expectEquals(check({0, 100, 1024, 500}, /*top=*/true).getHeight(), kMinH);

    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    const auto* mono = fixtures.find("main-mono");
    const auto* detail = fixtures.find("main-detail");
    if (mono == nullptr || detail == nullptr) {
      expect(false, "main-mono / main-detail scenarios missing");
      return;
    }
    constexpr int kStretch = 160;

    beginTest("the root takes the stretch into its column; header, faceplate and hint bar hold");
    {
      MockBackend backend(mono->data);
      ScaledHost host(backend, stretched(*mono, kStretch), fixtures.root);
      pump(100);
      auto& root = host.pluginRoot();
      expectEquals(host.getHeight(), kMinH + kStretch);
      expectEquals(root.stretch(), kStretch);
      expectEquals(root.designHeight(), kMinH + kStretch);
      expectWithinAbsoluteError(root.services().zoom.factor(), 1.0, 1e-9);
      auto* header = findView<PluginHeader>(root);
      auto* plate = findView<Faceplate>(root);
      auto* hints = findView<HintBar>(root);
      auto* chain = findView<ChainView>(root);
      expect(header != nullptr && plate != nullptr && hints != nullptr && chain != nullptr);
      if (header == nullptr || plate == nullptr || hints == nullptr || chain == nullptr) return;
      expectEquals(header->getHeight(), design::kHeaderHeight);
      expectEquals(root.getLocalArea(header, header->getLocalBounds()).getY(), 0);
      expectEquals(plate->getHeight(), design::kPlateHeight);
      const auto plateInRoot = root.getLocalArea(plate, plate->getLocalBounds());
      expectEquals(plateInRoot.getBottom(), design::kHeight + kStretch);
      const auto hintsInRoot = root.getLocalArea(hints, hints->getLocalBounds());
      expectEquals(hintsInRoot.getY(), design::kHeight + kStretch);
      expectEquals(hintsInRoot.getBottom(), root.getHeight());
      expectEquals(chain->getHeight(), design::kColumnHeight + kStretch);

      beginTest("the gallery keeps its title in place and centres the lanes in the room");
      const auto scroller = chain->scroller().getBounds();
      expectEquals(scroller.getY(), ChainView::kPadY);  // the title's row stays under the header
      expectEquals(scroller.getHeight(), design::kColumnHeight + kStretch - 2 * ChainView::kPadY);
      auto* lane = findView<GalleryLane>(root);
      expect(lane != nullptr);
      if (lane == nullptr) return;
      const auto laneInChain = chain->getLocalArea(lane, lane->getLocalBounds());
      // Within a pixel of centre (integer halves on both sides).
      expectWithinAbsoluteError(laneInChain.getCentreY(), chain->getHeight() / 2, 1);
    }

    beginTest("without stretch nothing moves");
    {
      MockBackend backend(mono->data);
      ScaledHost host(backend, *mono, fixtures.root);
      pump(100);
      auto& root = host.pluginRoot();
      expectEquals(root.stretch(), 0);
      expectEquals(host.getHeight(), kMinH);
      auto* chain = findView<ChainView>(root);
      expect(chain != nullptr);
      if (chain == nullptr) return;
      expectEquals(chain->getHeight(), design::kColumnHeight);
      expectEquals(chain->scroller().getY(), ChainView::kPadY);
    }

    beginTest("the block detail keeps its back link in place and centres the card below it");
    {
      MockBackend backend(detail->data);
      ScaledHost host(backend, stretched(*detail, kStretch), fixtures.root);
      pump(400);
      auto& root = host.pluginRoot();
      auto* view = findView<BlockDetail>(root);
      auto* back = findView<BackLink>(root);
      auto* card = findView<BlockCard>(root);
      expect(view != nullptr && back != nullptr && card != nullptr);
      if (view == nullptr || back == nullptr || card == nullptr) return;
      expectEquals(view->getHeight(), design::kColumnHeight + kStretch);
      expectEquals(view->getLocalArea(back, back->getLocalBounds()).getY(), BlockDetail::kPadY);
      const auto cardInView = view->getLocalArea(card, card->getLocalBounds());
      expectEquals(cardInView.getY(), kStretch / 2 + BlockDetail::kPadY + BackLink::kHeight + BlockDetail::kBackGap);
      expectEquals(view->getHeight() - cardInView.getBottom(), kStretch / 2 + BlockDetail::kPadY);
    }

    beginTest("a stretched window gives the strip its chrome from the stretch, not the scale");
    {
      MockBackend backend(mono->data);
      // Hints off: the window opens at the core height plus the stretch.
      auto scenario = stretched(*mono, kStretch);
      scenario.data.getDynamicObject()->setProperty("hints", false);
      ScaledHost host(backend, scenario, fixtures.root);
      pump(100);
      auto& root = host.pluginRoot();
      expectEquals(root.stretch(), kStretch);
      expectEquals(host.getHeight(), design::kHeight + kStretch);
      // A host that refuses the grow: the column gives up the strip's height.
      const int before = host.getHeight();
      root.services().hints.setEnabled(true);
      pump(50);
      expectEquals(host.getHeight(), before + design::kHintHeight);  // the grow was asked for
      host.setSize(host.getWidth(), before);  // the refusal
      pump(50);
      expectWithinAbsoluteError(root.services().zoom.factor(), 1.0, 1e-9);
      expectEquals(root.stretch(), kStretch - design::kHintHeight);
      expectEquals(root.designHeight(), before);
    }
  }
};

// AuthFlow::deviceOnly (the iOS AUv3): Login opens the sign-in screen on
// the device page itself, with no Copy Link (the app line under the cards
// is in the scenario capture); the expired code offers New code alone.
// Drives the `load-oauth-app-device*` scenarios.
struct DeviceOnlySignInTests : juce::UnitTest {
  DeviceOnlySignInTests() : juce::UnitTest("Sign-in: device only", "ui") {}

  void runTest() override {
    using Phase = ToneSession::AuthFlow::Phase;
    {
      beginTest("Login lands on the device page: code shown, nothing to copy");
      LiveScenario live(*this, "load-oauth-app-device");
      if (live.backend == nullptr || live.peer == nullptr) return;
      auto& root = live.root();
      live.drive();  // Account: → Login
      const auto& flow = root.services().session.authFlow();
      expect(flow.phase == Phase::leaving && flow.deviceOnly && flow.device.has_value());
      expect(root.signInShown());
      auto* screen = drive::find(root, [](juce::Component& c) { return c.getName() == "sign in"; });
      expect(screen != nullptr);
      if (screen == nullptr) return;
      const auto shown = [&](const juce::String& name) {
        auto* b = drive::buttonNamed(*screen, name);
        return b != nullptr && b->isShowing();
      };
      expect(!shown("Copy Link"), "Copy Link has no URL to copy in an extension");
      expect(!shown("Sign in on a different device"), "the device page is the page");
      expect(drive::find(*screen, [](juce::Component& c) { return dynamic_cast<QrCode*>(&c) != nullptr && c.isShowing(); }) !=
             nullptr);

      beginTest("the back arrow abandons the flow and takes the screen down");
      if (auto* back = drive::find(*screen, [](juce::Component& c) { return dynamic_cast<BackLink*>(&c) != nullptr; }))
        drive::click(root, *back);
      expect(LiveScenario::until([&] { return !root.signInShown(); }));
      expect(flow.phase == Phase::idle);
    }
    {
      beginTest("an expired code offers New code alone");
      LiveScenario live(*this, "load-oauth-app-device-expired");
      if (live.backend == nullptr || live.peer == nullptr) return;
      auto& root = live.root();
      live.drive();
      auto* screen = drive::find(root, [](juce::Component& c) { return c.getName() == "sign in"; });
      expect(screen != nullptr);
      if (screen == nullptr) return;
      auto* newCode = drive::buttonNamed(*screen, "New code");
      expect(newCode != nullptr && newCode->isShowing());
      auto* copy = drive::buttonNamed(*screen, "Copy Link");
      expect(copy == nullptr || !copy->isShowing(), "no authorize URL to copy in an extension");
    }
  }
};


HtmlTests htmlTests;
FontTests fontTests;
RichFlowTests richFlowTests;
WindowFitTests windowFitTests;
AccessibilityTests accessibilityTests;
BrowserAccessibilityTests browserAccessibilityTests;
ToggleContextAccessibilityTests toggleContextAccessibilityTests;
PresetAccessibilityTests presetAccessibilityTests;
AudioSettingsAccessibilityTests audioSettingsAccessibilityTests;
ChainAccessibilityTests chainAccessibilityTests;
DialogAccessibilityTests dialogAccessibilityTests;
FocusPolicyTests focusPolicyTests;
SettingsKeyboardTests settingsKeyboardTests;
ScrollSurfacesTests scrollSurfacesTests;
TouchScrollTests touchScrollTests;
PopoverFollowTests popoverFollowTests;
PointerTests pointerTests;
GlowCornerTests glowCornerTests;
PresetReorderTests presetReorderTests;
ChainCrossLaneDragTests chainCrossLaneDragTests;
BlockSizeToggleTests blockSizeToggleTests;
BlockNormalizeControlTests blockNormalizeControlTests;
IrImageTests irImageTests;
MidiMapCommitTests midiMapCommitTests;
TunerMuteTests tunerMuteTests;
TunerMidiToggleTests tunerMidiToggleTests;
PresetPcLabelTests presetPcLabelTests;
KnobReadoutTests knobReadoutTests;
FaceplateEffectsTests faceplateEffectsTests;
FaceplateDualMonoTests faceplateDualMonoTests;
UpdateCheckTests updateCheckTests;
ConnectionGateTests connectionGateTests;
PitchTests pitchTests;
OAuthTests oauthTests;
LoopbackServerTests loopbackServerTests;
PaginatorTests paginatorTests;
DragScrollerTests dragScrollerTests;
UiPrefsTests uiPrefsTests;
Tone3000ClientTests tone3000ClientTests;
Tone3000SessionTests tone3000SessionTests;
DeviceOnlySignInTests deviceOnlySignInTests;
ToneModelTests toneModelTests;
ToneQueryTests toneQueryTests;
ReadoutTests readoutTests;

// ---------------------------------------------------------------------------
// What a screen reader says. Each Tab stop is read the way NVDA and
// VoiceOver read it by default: name, role, value, state, then the help
// text. The rules below catch speech that is missing, garbled or wordy; they
// cannot judge the voice itself (accessibility.md covers the manual pass).

// Every help::announce() of the run (installed by runSelfTests before any
// suite starts), for the speech checks.
juce::StringArray& announcementLog() {
  static juce::StringArray log;
  return log;
}

namespace speech {

// Longest name a control should have (tone and preset titles excepted: they
// are the user's content, read in full), and the longest status message.
constexpr int kMaxNameWords = 6;
constexpr int kMaxAnnouncementWords = 20;
constexpr int kMaxValueChars = 40;
// Settings toggles read the line under the switch, which can run long;
// that copy is the app's own and stays as it is. Elsewhere, 20 words.
constexpr int kMaxDescriptionWords = 20;

juce::String roleWords(juce::AccessibilityRole role) {
  using R = juce::AccessibilityRole;
  switch (role) {
    case R::button: return "button";
    case R::toggleButton: return "toggle button";
    case R::radioButton: return "radio button";
    case R::comboBox: return "combo box";
    case R::slider: return "slider";
    case R::editableText: return "edit";
    case R::group: return "grouping";
    case R::list: return "list";
    case R::listItem: return "list item";
    case R::menuItem: return "menu item";
    case R::hyperlink: return "link";
    case R::progressBar: return "progress bar";
    default: return {};
  }
}

int wordCount(const juce::String& text) { return juce::StringArray::fromTokens(text, " \t\n", "").size(); }

// Text meant for developers, not listeners: identifiers, placeholders and
// numbers that failed to format.
bool soundsLikeCode(const juce::String& text) {
  if (text.containsChar('_') || text.containsChar('{') || text.containsChar('}')) return true;
  if (text.containsIgnoreCase("t3k.") || text.contains("%s") || text.contains("%d")) return true;
  for (auto word : juce::StringArray::fromTokens(text, " ,.:;()/", "")) {
    if (word == "nan" || word == "NaN" || word == "inf" || word == "null" || word == "undefined") return true;
    // camelCase inside a lower-case word ("osFactor"); "iPad", "McIntosh"
    // and all-caps words like "TONE3000" pass.
    for (int i = 2; i + 1 < word.length(); ++i)
      if (juce::CharacterFunctions::isLowerCase(word[0]) && juce::CharacterFunctions::isLowerCase(word[i - 1]) &&
          juce::CharacterFunctions::isUpperCase(word[i]) && juce::CharacterFunctions::isLowerCase(word[i + 1]))
        return true;
  }
  return false;
}

// "Input Input" or "the the": a doubled word reads as a stutter.
bool repeatsWord(const juce::String& text) {
  const auto words = juce::StringArray::fromTokens(text.toLowerCase(), " ,.:;", "");
  for (int i = 1; i < words.size(); ++i)
    if (words[i] == words[i - 1] && words[i].containsOnly("abcdefghijklmnopqrstuvwxyz")) return true;
  return false;
}

// More than two decimal places reads as noise ("minus six point zero four
// one seven decibels").
bool overlyPrecise(const juce::String& value) {
  for (int i = 1; i < value.length(); ++i) {
    if (value[i] != '.' || !juce::CharacterFunctions::isDigit(value[i - 1])) continue;
    int digits = 0;
    for (int j = i + 1; j < value.length() && juce::CharacterFunctions::isDigit(value[j]); ++j) ++digits;
    if (digits > 2) return true;
  }
  return false;
}

struct Stop {
  juce::Component* component = nullptr;
  juce::String name, role, value, state, help, description;
  bool content = false;  // names the user's content (a tone or preset title)
  // The name with any tone title in it taken out: what the word limit
  // applies to ("Branch after <title>" is two words of ours).
  juce::String ownWords;
  // The description is copy printed beside the control (a settings
  // switch's line): reading it out matches the screen.
  bool descriptionOnScreen = false;

  juce::String line() const {
    juce::StringArray parts;
    for (const auto& p : {name, role, value, state})
      if (p.isNotEmpty()) parts.add(p);
    return parts.joinIntoString(", ");
  }
  // Help that opens by repeating the name ("Undo: step back" after "Undo").
  bool helpRepeatsName() const {
    return name.isNotEmpty() && (help.equalsIgnoreCase(name) || help.startsWithIgnoreCase(name + ":") ||
                                 help.startsWithIgnoreCase(name + "."));
  }
};

Stop describe(juce::Component& c) {
  Stop s;
  s.component = &c;
  auto* h = c.getAccessibilityHandler();
  if (h == nullptr) return s;
  s.name = h->getTitle().trim();
  s.role = roleWords(h->getRole());
  if (auto* v = h->getValueInterface()) s.value = v->getCurrentValueAsString().trim();
  const auto state = h->getCurrentState();
  if (state.isCheckable()) s.state = state.isChecked() ? "checked" : "not checked";
  else if (state.isExpandable()) s.state = state.isExpanded() ? "expanded" : "collapsed";
  s.help = h->getHelp().trim();
  s.description = h->getDescription().trim();
  s.ownWords = s.name;
  for (auto* p = &c; p != nullptr && !s.content; p = p->getParentComponent())
    s.content = dynamic_cast<ToneCard*>(p) != nullptr || dynamic_cast<GalleryTile*>(p) != nullptr;
  // Readable paragraphs and list rows (presets, menu rows) carry text the
  // user or a creator wrote; it is read in full.
  const auto role = h->getRole();
  if (role == juce::AccessibilityRole::staticText || role == juce::AccessibilityRole::listItem) s.content = true;
  s.descriptionOnScreen = dynamic_cast<PillToggle*>(&c) != nullptr;
  return s;
}

// What is wrong with one stop's speech; empty when it reads well.
juce::StringArray problems(const Stop& s) {
  juce::StringArray out;
  if (s.name.isEmpty()) out.add("has no name");
  if (!s.content && wordCount(s.ownWords) > kMaxNameWords)
    out.add("name is over " + juce::String(kMaxNameWords) + " words");
  if (s.role.isNotEmpty() && s.name.endsWithIgnoreCase(" " + s.role.fromLastOccurrenceOf(" ", false, false)))
    out.add("name repeats its role (" + s.role + ")");
  if (soundsLikeCode(s.name) || soundsLikeCode(s.value)) out.add("reads as code");
  if (repeatsWord(s.name)) out.add("name repeats a word");
  if (s.value.length() > kMaxValueChars) out.add("value is over " + juce::String(kMaxValueChars) + " characters");
  if (overlyPrecise(s.value)) out.add("value has more than two decimals");
  // Hint copy leads with the control's name; a reader that speaks help on
  // focus (NVDA does) would say the name twice.
  if (s.helpRepeatsName()) out.add("help repeats the name: " + s.help.substring(0, 50));
  // Read on every visit, so it stays short.
  if (wordCount(s.description) > kMaxDescriptionWords && !s.content && !s.descriptionOnScreen)
    out.add("description is over " + juce::String(kMaxDescriptionWords) + " words");
  return out;
}

// The Tab walk of whatever is in front, as the keyboard sees it: from
// nothing focused, Tab until the walk wraps or leaves the UI.
std::vector<Stop> tabWalk(LiveScenario& live) {
  std::vector<Stop> stops;
  live.key(juce::KeyPress::escapeKey);
  juce::Component* first = nullptr;
  for (int i = 0; i < 400; ++i) {
    if (!live.key(juce::KeyPress::tabKey)) break;
    LiveScenario::pump(10);
    auto* f = LiveScenario::focused();
    if (f == nullptr || f == first) break;
    if (first == nullptr) first = f;
    stops.push_back(describe(*f));
  }
  live.key(juce::KeyPress::escapeKey);
  // Tone titles on screen are the user's content wherever they appear.
  juce::StringArray titles;
  std::function<void(juce::Component&)> visit = [&](juce::Component& c) {
    if (auto* tile = dynamic_cast<GalleryTile*>(&c); tile != nullptr && tile->getTitle().isNotEmpty())
      titles.addIfNotAlreadyThere(tile->getTitle());
    for (auto* child : c.getChildren()) visit(*child);
  };
  visit(live.root());
  for (auto& stop : stops)
    for (const auto& title : titles) stop.ownWords = stop.ownWords.replace(title, "").trim();
  return stops;
}

juce::Component* titled(juce::Component& root, const juce::String& title) {
  return drive::find(root, [&](juce::Component& c) {
    auto* h = c.getAccessibilityHandler();
    return c.isShowing() && !a11y::isGroup(c) && h != nullptr && h->getTitle() == title;
  });
}

}  // namespace speech

struct SpeechTests : juce::UnitTest {
  SpeechTests() : juce::UnitTest("Speech", "ui") {}

  // One screen, opened the way its scenario opens it, then `open`.
  void auditScreen(const juce::String& scenarioId, const std::function<void(LiveScenario&)>& open = {}) {
    beginTest("every Tab stop reads clearly: " + scenarioId);
    LiveScenario live(*this, scenarioId);
    if (!live.ok) return;
    live.drive();
    if (open) open(live);
    LiveScenario::pump(100);
    const auto stops = speech::tabWalk(live);
    expect(!stops.empty(), scenarioId + ": Tab reached no control");
    for (const auto& stop : stops) {
      const auto issues = speech::problems(stop);
      expect(issues.isEmpty(), scenarioId + ": \"" + stop.line() + "\" " + issues.joinIntoString("; "));
    }
    // T3K_SPEECH_TRANSCRIPT=1 prints every stop, for reading by ear.
    if (juce::SystemStats::getEnvironmentVariable("T3K_SPEECH_TRANSCRIPT", "").isNotEmpty())
      for (const auto& stop : stops)
        logMessage("  " + stop.line() + (stop.description.isNotEmpty() ? ". " + stop.description : juce::String()) +
                   (stop.help.isNotEmpty() ? "  |  " + stop.help : juce::String()));
  }

  void runTest() override {
    auditScreen("main-stereo");
    auditScreen("main-mono");
    auditScreen("main-detail");
    auditScreen("main-detail-eq-sliders");
    auditScreen("main-detail-info");
    auditScreen("main-tuner-idle");
    auditScreen("browser-search");
    auditScreen("browser-signed-out");
    auditScreen("settings-plugin");  // its drive opens Settings from the account menu
    auditScreen("settings-system", [](LiveScenario& live) { live.root().openSettings(SettingsScreen::Tab::system); });

    beginTest("group names are short and do not say group");
    {
      LiveScenario live(*this, "main-stereo");
      if (live.ok) {
        live.root().openSettings(SettingsScreen::Tab::plugin);
        LiveScenario::pump(200);
        std::function<void(juce::Component&)> visit = [&](juce::Component& c) {
          if (a11y::isGroup(c) && c.isShowing()) {
            const auto title = c.getTitle();
            expect(title.isNotEmpty(), "a group has no name");
            expect(speech::wordCount(title) <= 3, "group name too long: " + title);
            expect(!title.containsIgnoreCase("group"), "the reader already says grouping: " + title);
          }
          for (auto* child : c.getChildren()) visit(*child);
        };
        visit(live.root());
        live.root().closeSettings();
      }
    }

    beginTest("choosing Mono or Stereo is spoken");
    {
      LiveScenario live(*this, "main-stereo");
      if (live.ok) {
        const int before = announcementLog().size();
        const auto said = [&] {
          juce::StringArray since;
          for (int i = before; i < announcementLog().size(); ++i) since.add(announcementLog()[i]);
          return since;
        };
        if (auto* mono = speech::titled(live.root(), "Mono")) {
          mono->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
          LiveScenario::pump(100);
        }
        expect(said().contains("Mono selected"), "heard: " + said().joinIntoString(" / "));
        if (auto* stereo = speech::titled(live.root(), "Stereo")) {
          stereo->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
          LiveScenario::pump(100);
        }
        expect(said().contains("Stereo selected"), "heard: " + said().joinIntoString(" / "));
      }
    }

    // Everything announced while the suites before this one ran (see
    // runSelfTests), plus the above.
    beginTest("status messages are short and plain");
    const auto& log = announcementLog();
    expect(!log.isEmpty(), "the run produced status messages to check");
    for (const auto& said : log) {
      // The device sign-in instructions carry the activation code and the
      // address to type it at: long, but all of it is needed at once.
      const bool instructions = said.startsWith("Sign in on your phone.");
      expect(instructions || speech::wordCount(said) <= speech::kMaxAnnouncementWords,
             "over " + juce::String(speech::kMaxAnnouncementWords) + " words: \"" + said + "\"");
      expect(!speech::soundsLikeCode(said), "reads as code: \"" + said + "\"");
      expect(!speech::repeatsWord(said), "repeats a word: \"" + said + "\"");
    }
    logMessage(juce::String(log.size()) + " status messages checked");
  }
};

// The settings that change how the plugin is used without sight or a mouse:
// Show Info Bar (the hint text), every Plugin
// Settings toggle from a screen reader, and the groups the period and comma
// keys stop at on both Settings pages.
struct AccessibilitySettingsTests : juce::UnitTest {
  AccessibilitySettingsTests() : juce::UnitTest("Accessibility settings", "ui") {}

  static bool checked(juce::Component& c) {
    auto* h = c.getAccessibilityHandler();
    return h != nullptr && h->getCurrentState().isChecked();
  }
  static void toggle(juce::Component& c) {
    auto& actions = c.getAccessibilityHandler()->getActions();
    if (!actions.invoke(juce::AccessibilityActionType::toggle)) actions.invoke(juce::AccessibilityActionType::press);
    LiveScenario::pump(50);
  }
  static bool press(LiveScenario& live, juce::juce_wchar c) {
    return live.peer->handleKeyPress(juce::KeyPress(c, {}, c));
  }
  static juce::String groupOfFocus(juce::Component& scope) {
    auto* group = a11y::enclosingGroup(LiveScenario::focused(), &scope);
    return group != nullptr ? group->getTitle() : juce::String("(none)");
  }
  // Nothing focused, without Escape: Escape is the screen's Back now and
  // would close Settings.
  static void clearFocus() {
    juce::Component::unfocusAllComponents();
    LiveScenario::pump(20);
  }
  // Period from nothing focused until the walk comes round: the groups in order.
  juce::StringArray walkGroups(LiveScenario& live, juce::Component& scope) {
    clearFocus();
    juce::StringArray visited;
    for (int i = 0; i < 30; ++i) {
      expect(press(live, '.'), "period moves to a group");
      LiveScenario::pump(20);
      const auto g = groupOfFocus(scope);
      if (visited.contains(g)) break;
      visited.add(g);
    }
    return visited;
  }

  void runTest() override {
    LiveScenario live(*this, "settings-system");  // standalone: both Settings tabs
    if (!live.ok) return;
    auto& root = live.root();
    const bool hintsWere = root.services().hints.enabled();
    root.services().hints.setEnabled(true);

    root.openSettings(SettingsScreen::Tab::plugin);
    LiveScenario::pump(300);
    auto* settings = root.settings();
    expect(settings != nullptr);
    if (settings == nullptr) return;

    beginTest("Show Info Bar is a named toggle that hides and shows the hint bar");
    if (auto* infoBar = speech::titled(*settings, "Show Info Bar"); infoBar != nullptr) {
      expect(infoBar->getAccessibilityHandler()->getRole() == juce::AccessibilityRole::toggleButton);
      expect(checked(*infoBar));
      toggle(*infoBar);
      expect(!root.services().hints.enabled() && !checked(*infoBar), "off");
      auto* bar = drive::find(root, [](juce::Component& c) { return dynamic_cast<HintBar*>(&c) != nullptr; });
      expect(bar != nullptr && !bar->isVisible(), "the hint bar hides");
      toggle(*infoBar);
      expect(root.services().hints.enabled() && checked(*infoBar), "back on");
      expect(bar != nullptr && bar->isVisible(), "the hint bar returns");
    } else {
      expect(false, "no Show Info Bar toggle");
    }

    beginTest("period walks the Plugin Settings sections in reading order");
    {
      const auto groups = walkGroups(live, *settings);
      int last = -1;
      for (auto name : {"Settings pages", "Display", "NAM models", "Audio processing", "MIDI Mapping", "Diagnostics"}) {
        const int at = groups.indexOf(name);
        expect(at > last, juce::String(name) + " in order: " + groups.joinIntoString(", "));
        last = juce::jmax(last, at);
      }
      expect(!groups.contains("(none)"), "every stop is in a group: " + groups.joinIntoString(", "));
    }

    beginTest("comma walks back");
    {
      clearFocus();
      expect(press(live, '.'));
      const auto firstGroup = groupOfFocus(*settings);
      expect(press(live, '.'));
      expect(groupOfFocus(*settings) != firstGroup);
      expect(press(live, ','));
      expectEquals(groupOfFocus(*settings), firstGroup);
    }

    beginTest("every Plugin Settings toggle flips from the screen reader and reads back");
    {
      std::vector<juce::Component::SafePointer<juce::Component>> toggles;
      std::function<void(juce::Component&)> visit = [&](juce::Component& c) {
        if (dynamic_cast<PillToggle*>(&c) != nullptr && c.isShowing() && c.isEnabled()) toggles.emplace_back(&c);
        for (auto* child : c.getChildren()) visit(*child);
      };
      visit(*settings);
      expect(toggles.size() >= 6, "found " + juce::String((int)toggles.size()) + " toggles");
      for (auto& t : toggles) {
        if (t == nullptr) continue;
        const auto name = t->getAccessibilityHandler()->getTitle();
        expect(name.isNotEmpty(), "a settings toggle has no name");
        const bool was = checked(*t);
        toggle(*t);
        if (t == nullptr) continue;
        expect(checked(*t) != was, name + " flips");
        toggle(*t);
        if (t == nullptr) continue;
        expect(checked(*t) == was, name + " flips back");
      }
    }

    beginTest("opening Settings over a focused control moves focus in; closing hands it back");
    {
      root.closeSettings();
      LiveScenario::pump(100);
      auto* account = drive::byHelpPrefix(root, "Account:");
      expect(account != nullptr);
      if (account != nullptr) {
        LiveScenario::focus(*account);
        expect(LiveScenario::focused() == account);
        root.openSettings(SettingsScreen::Tab::plugin);
        LiveScenario::pump(100);
        expect(root.settings() != nullptr && root.settings()->isParentOf(LiveScenario::focused()),
               "focus is on the Settings page, not behind it");
        expect(live.key(juce::KeyPress::tabKey), "Tab moves within the page");
        expect(root.settings() != nullptr && root.settings()->isParentOf(LiveScenario::focused()));
        root.closeSettings();
        LiveScenario::until([&] { return root.settings() == nullptr; });
        LiveScenario::pump(50);
        expect(LiveScenario::focused() == account, "focus is back on the account button");
      }
      clearFocus();
    }

    beginTest("period walks the System Settings sections");
    root.openSettings(SettingsScreen::Tab::system);
    LiveScenario::pump(300);
    if (auto* system = root.settings()) {
      const auto groups = walkGroups(live, *system);
      for (auto name : {"Monitoring", "Audio Interface", "MIDI Inputs"})
        expect(groups.contains(name), juce::String(name) + ": " + groups.joinIntoString(", "));
      expect(groups.indexOf("Monitoring") < groups.indexOf("Audio Interface"));
    }

    root.closeSettings();
    root.services().hints.setEnabled(hintsWere);
    clearFocus();
  }
};

// Every toggle on the faceplate and the chain, clicked with the mouse the
// way a sighted user would: the state a screen reader reports follows the
// click, and the click takes no keyboard focus (as upstream).
struct ToggleClickTests : juce::UnitTest {
  ToggleClickTests() : juce::UnitTest("Toggle clicks", "ui") {}

  static bool inGroup(juce::Component& c, juce::Component& scope, const juce::StringArray& names) {
    for (auto* g = a11y::enclosingGroup(&c, &scope); g != nullptr;
         g = a11y::enclosingGroup(g->getParentComponent(), &scope))
      if (names.contains(g->getTitle())) return true;
    return false;
  }

  void runTest() override {
    beginTest("clicks flip each toggle and the reported state follows");
    LiveScenario live(*this, "main-stereo");
    if (!live.ok) return;
    auto& root = live.root();
    const juce::StringArray scopes{"Global controls", "Chain", "Left chain", "Right chain"};
    std::vector<juce::Component::SafePointer<juce::Component>> toggles;
    std::function<void(juce::Component&)> visit = [&](juce::Component& c) {
      auto* h = c.getAccessibilityHandler();
      if (c.isShowing() && c.isEnabled() && h != nullptr && h->getCurrentState().isCheckable() &&
          inGroup(c, root, scopes))
        toggles.emplace_back(&c);
      for (auto* child : c.getChildren()) visit(*child);
    };
    visit(root);
    expect(toggles.size() >= 4, "found " + juce::String((int)toggles.size()) + " toggles");
    auto mouse = live.mouse();
    int flipped = 0;
    for (auto& t : toggles) {
      if (t == nullptr || !t->isShowing()) continue;  // an earlier click changed the layout
      drive::scrollIntoView(*t);
      const auto name = t->getAccessibilityHandler()->getTitle();
      const bool was = t->getAccessibilityHandler()->getCurrentState().isChecked();
      // The selected option of a one-of-many choice (Mono / Stereo) stays
      // selected when clicked again: nothing to flip.
      const bool radioLike = was && dynamic_cast<SegmentedText*>(t->getParentComponent()) != nullptr;
      // Point first, as a hand does: tile chrome shows on hover.
      mouse.at(live.centre(*t), false);
      LiveScenario::pump(300);
      auto* group = a11y::enclosingGroup(t.getComponent(), &root);
      mouse.click(live.centre(*t));
      LiveScenario::pump(50);
      if (t == nullptr || radioLike) continue;
      expect(t->getAccessibilityHandler()->getCurrentState().isChecked() != was, name + " flips on click");
      // A click never takes keyboard focus (upstream): nothing in the
      // toggle's group is focused by it.
      if (t->getWantsKeyboardFocus()) {
        auto* f = LiveScenario::focused();
        expect(f != t.getComponent() && (f == nullptr || group == nullptr || !group->isParentOf(f)),
               name + " does not take focus on click");
      }
      ++flipped;
      if (!t->isShowing()) continue;
      mouse.at(live.centre(*t), false);
      LiveScenario::pump(300);
      mouse.click(live.centre(*t));
      LiveScenario::pump(50);
      if (t != nullptr && t->isShowing())
        expect(t->getAccessibilityHandler()->getCurrentState().isChecked() == was, name + " flips back");
    }
    expect(flipped >= 4, juce::String(flipped) + " toggles clicked");
    live.key(juce::KeyPress::escapeKey);
  }
};

// The keyboard and screen-reader paths added on top of upstream, and the
// upstream rules they must not change for the mouse.
struct KeyboardPathsTests : juce::UnitTest {
  KeyboardPathsTests() : juce::UnitTest("Keyboard paths", "ui") {}

  static juce::Component* titledStartingWith(juce::Component& root, const juce::String& prefix) {
    return drive::find(root, [&](juce::Component& c) {
      auto* h = c.getAccessibilityHandler();
      return c.isShowing() && !a11y::isGroup(c) && h != nullptr && h->getTitle().startsWith(prefix);
    });
  }
  static juce::StringArray spokenSince(int before) {
    juce::StringArray since;
    for (int i = before; i < announcementLog().size(); ++i) since.add(announcementLog()[i]);
    return since;
  }

  void runTest() override {
    beginTest("Escape closes Settings and returns focus to its opener");
    {
      LiveScenario live(*this, "settings-plugin");
      if (live.ok) {
        auto& root = live.root();
        auto* account = drive::byHelpPrefix(root, "Account:");
        expect(account != nullptr);
        if (account != nullptr) {
          LiveScenario::focus(*account);
          root.openSettings(SettingsScreen::Tab::plugin);
          LiveScenario::pump(200);
          expect(root.settings() != nullptr && root.settings()->isParentOf(LiveScenario::focused()),
                 "focus is on the Settings page");
          expect(live.key(juce::KeyPress::escapeKey));
          expect(LiveScenario::until([&] { return root.settings() == nullptr; }), "Escape closes Settings");
          expect(LiveScenario::until([&] { return LiveScenario::focused() == account; }),
                 "focus is back on the account button");
        }
      }
    }

    beginTest("A2-Lite / A2-Full: Tab reaches the radio group; arrows select and the default follows");
    {
      LiveScenario live(*this, "settings-plugin");
      if (live.ok) {
        auto& root = live.root();
        root.openSettings(SettingsScreen::Tab::plugin);
        LiveScenario::pump(300);
        juce::Component::unfocusAllComponents();
        RadioOption* reached = nullptr;
        for (int i = 0; i < 200 && reached == nullptr; ++i) {
          if (!live.key(juce::KeyPress::tabKey)) break;
          LiveScenario::pump(10);
          reached = dynamic_cast<RadioOption*>(LiveScenario::focused());
        }
        expect(reached != nullptr, "Tab reaches an A2 size option");
        if (reached != nullptr) {
          auto handler = reached->createAccessibilityHandler();
          expect(handler->getRole() == juce::AccessibilityRole::radioButton);
          expect(reached->getTitle() == "A2-Lite" || reached->getTitle() == "A2-Full");
          // Make the reached option the selected one first.
          if (!reached->selected()) {
            expect(live.key(juce::KeyPress::spaceKey));
            LiveScenario::pump(100);
          }
          expect(LiveScenario::until([&] { return reached->selected(); }));
          const bool fullBefore = isSlimSizeFull(root.services().chain.state().namSlimSizeDefault);
          expect(live.key(juce::KeyPress::downKey));
          auto* other = dynamic_cast<RadioOption*>(LiveScenario::focused());
          expect(other != nullptr && other != reached, "Down moves focus to the other option");
          expect(LiveScenario::until([&] {
            return isSlimSizeFull(root.services().chain.state().namSlimSizeDefault) != fullBefore;
          }), "the default model size follows");
          if (other != nullptr) {
            expect(LiveScenario::until([&] { return other->selected() && !reached->selected(); }));
            expect(other->getAccessibilityHandler()->getCurrentState().isChecked());
          }
          expect(live.key(juce::KeyPress::upKey));
          expect(LiveScenario::until([&] {
            return isSlimSizeFull(root.services().chain.state().namSlimSizeDefault) == fullBefore;
          }), "Up selects it back");
          expect(LiveScenario::focused() == reached);
        }
        juce::Component::unfocusAllComponents();
      }
    }

    beginTest("EQ graph: arrows choose a band; a Frequency arrow steps the band's frequency");
    {
      LiveScenario live(*this, "main-detail-eq-curve");
      if (live.ok) {
        live.drive();
        auto& root = live.root();
        auto* graph = titledStartingWith(root, "Band");
        expect(graph != nullptr, "the EQ graph is a named Tab stop");
        auto* editor = graph != nullptr ? graph->findParentComponentOfClass<BlockEqView>() : nullptr;
        expect(editor != nullptr);
        if (graph != nullptr && editor != nullptr && editor->bands().size() > 1) {
          expect(graph->getAccessibilityHandler()->getRole() == juce::AccessibilityRole::slider);
          LiveScenario::focus(*graph);
          expect(live.key(juce::KeyPress::homeKey));
          expect(graph->getAccessibilityHandler()->getTitle().startsWith("Band 1"));
          expect(live.key(juce::KeyPress::rightKey));
          expect(graph->getAccessibilityHandler()->getTitle().startsWith("Band 2"), "Right selects band 2");
          expectWithinAbsoluteError(graph->getAccessibilityHandler()->getValueInterface()->getCurrentValue(), 2.0, 1e-9);
          auto* freq = speech::titled(root, "Frequency");
          expect(freq != nullptr, "the Frequency chip is a named Tab stop");
          if (freq != nullptr) {
            expect(freq->getAccessibilityHandler()->getRole() == juce::AccessibilityRole::slider);
            const double before = editor->bands()[1].freqHz;
            LiveScenario::focus(*freq);
            expect(live.key(juce::KeyPress::upKey));
            expect(LiveScenario::until([&] { return editor->bands()[1].freqHz > before; }), "Up raises the frequency");
            expect(live.key(juce::KeyPress::downKey));
            expect(LiveScenario::until([&] { return std::abs(editor->bands()[1].freqHz - before) < before * 0.01; }),
                   "Down brings it back");
          }
        }
        juce::Component::unfocusAllComponents();
      }
    }

    beginTest("Delete resets a focused knob to its default");
    {
      LiveScenario live(*this, "main-mono");
      if (live.ok) {
        Knob::Options options;
        options.label = "Test";
        options.scale = &scales::gainDb();
        options.defaultValue = 0.5f;
        Knob knob(options);
        knob.setBounds(0, 0, 60, 60);
        live.root().addAndMakeVisible(knob);
        knob.setValue(0.9f);
        std::vector<float> emitted;
        knob.onChange = [&](float v) { emitted.push_back(v); };
        expect(knob.createAccessibilityHandler()->getHelp().contains("Delete resets"));
        // Without keyboard focus Delete is not the knob's.
        expect(!knob.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
        LiveScenario::focus(knob);
        expect(live.key(juce::KeyPress::deleteKey));
        expectWithinAbsoluteError(knob.value(), 0.5f, 1e-6f);
        expect(!emitted.empty() && std::abs(emitted.back() - 0.5f) < 1e-6f, "the reset is reported");
        live.root().removeChildComponent(&knob);
      }
    }

    beginTest("ModelSelect: Next announces the model and its position");
    {
      ModelSelect select;
      select.setOptions({{"a", "Clean"}, {"b", "Crunch"}, {"c", "Lead"}});
      select.setValue("a");
      select.onChange = [&](const juce::String& id) { select.setValue(id); };
      auto* trigger = drive::find(select, [](juce::Component& c) { return c.getName() == "model select"; });
      expect(trigger != nullptr);
      if (trigger != nullptr) {
        auto handler = trigger->createAccessibilityHandler();
        expect(handler->getRole() == juce::AccessibilityRole::comboBox);
        expectEquals(handler->getTitle(), juce::String("Model"));
        expectEquals(handler->getValueInterface()->getCurrentValueAsString(), juce::String("Clean, 1 of 3"));
      }
      auto* next = dynamic_cast<Clickable*>(drive::find(select, [](juce::Component& c) {
        auto* b = dynamic_cast<Clickable*>(&c);
        return b != nullptr && b->accessibleName() == "Next model";
      }));
      expect(next != nullptr);
      if (next != nullptr && next->onClick) {
        const int before = announcementLog().size();
        next->onClick();
        expect(spokenSince(before).contains("Crunch, 2 of 3"), "said: " + spokenSince(before).joinIntoString(" | "));
        if (trigger != nullptr)
          expectEquals(trigger->createAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(),
                       juce::String("Crunch, 2 of 3"));
      }
    }

    beginTest("presets move by keyboard: Space, Down, Space reorders and is spoken");
    {
      LiveScenario live(*this, "chrome-preset-browse");
      if (live.ok) {
        auto& root = live.root();
        drive::clickByHelp(root, "Presets:");
        drive::wait(300);
        drive::clickByHelp(root, "Reorder:");  // grips show in reorder mode
        drive::wait(200);
        // The topmost grip on screen.
        juce::Component* grip = nullptr;
        drive::find(root, [&](juce::Component& c) {
          auto* h = c.getAccessibilityHandler();
          if (c.isShowing() && h != nullptr && h->getTitle().startsWith("Move ") &&
              (grip == nullptr || c.getScreenY() < grip->getScreenY()))
            grip = &c;
          return false;
        });
        expect(grip != nullptr, "a preset row has a Move grip");
        if (grip != nullptr) {
          const auto name = grip->getTitle().fromFirstOccurrenceOf("Move ", false, false);
          // The order within the grip's own section (factory or user): moves
          // never cross it, and the store may list the sections either way.
          bool factory = false;
          for (const auto& p : root.services().presets.presets())
            if (p.name == name) factory = p.factory;
          const auto order = [&] {
            juce::StringArray names;
            for (const auto& p : root.services().presets.presets())
              if (p.factory == factory) names.add(p.name);
            return names;
          };
          const auto before = order();
          const int at = before.indexOf(name);
          expect(at >= 0, name + " is in the preset list");
          const int spoken = announcementLog().size();
          LiveScenario::focus(*grip);
          expect(live.key(juce::KeyPress::spaceKey));
          expect(live.key(juce::KeyPress::downKey));
          expect(live.key(juce::KeyPress::spaceKey));
          expect(LiveScenario::until([&] { return order().indexOf(name) == at + 1; }),
                 name + " moved down one: " + before.joinIntoString(", ") + " -> " + order().joinIntoString(", "));
          const auto said = spokenSince(spoken);
          expect(said.size() > 0 && said[0].startsWith("Moving " + name), "said: " + said.joinIntoString(" | "));
          expect(said.joinIntoString("|").contains(name + ", position "),
                 "the new position is spoken: " + said.joinIntoString(" | "));
          expect(said.contains("Move complete"), "said: " + said.joinIntoString(" | "));
          expect(LiveScenario::until([&] {
            auto* f = LiveScenario::focused();
            return f != nullptr && f->getTitle() == "Move " + name;
          }), "focus stays on the moved row's grip");
        }
        juce::Component::unfocusAllComponents();
      }
    }

    beginTest("a mouse click on a button or a knob takes no keyboard focus (upstream)");
    {
      LiveScenario live(*this, "main-mono");
      if (live.ok) {
        auto& root = live.root();
        juce::Component::unfocusAllComponents();
        LiveScenario::pump(20);
        auto* undo = drive::byHelpPrefix(root, "Undo:");
        auto* input = drive::find(root, [](juce::Component& c) {
          return dynamic_cast<Knob*>(&c) != nullptr && c.isShowing() && c.getTitle() == "Input";
        });
        expect(undo != nullptr && input != nullptr);
        auto mouse = live.mouse();
        if (undo != nullptr) {
          mouse.click(live.centre(*undo));
          LiveScenario::pump(30);
          expect(LiveScenario::focused() == nullptr, "a button click leaves nothing focused");
        }
        if (input != nullptr) {
          mouse.click(live.centre(*input));
          LiveScenario::pump(30);
          expect(LiveScenario::focused() == nullptr, "a knob click leaves nothing focused");
        }

        beginTest("the hint bar shows upstream's text for a knob and a button");
        if (undo != nullptr) {
          mouse.at(live.centre(*undo), false);
          LiveScenario::pump(50);
          expectEquals(root.services().hints.current(), juce::String("Undo: revert last chain edit."));
          expectEquals(undo->getHelpText(), juce::String("Undo: revert last chain edit."));
        }
        if (input != nullptr) {
          mouse.at(live.centre(*input), false);
          LiveScenario::pump(50);
#if JUCE_WINDOWS
          const auto upstream = juce::String::fromUTF8(
              "Input: chain input level, \xc2\xb1" "24 dB. Shift+drag: fine \xc2\xb7 double-click: type \xc2\xb7 Alt+click: reset");
          expectEquals(root.services().hints.current(), upstream);
#endif
          expectEquals(root.services().hints.current(), help::text(help::Key::inputLevel));
          // The keyboard instructions are spoken only.
          expect(!root.services().hints.current().contains("Delete"));
        }
        mouse.at(live.centre(root).translated(0, -2000.0f), false);
      }
    }
  }
};

// Last, so the speech checks see every status message the suites above spoke.
AccessibilitySettingsTests accessibilitySettingsTests;
ToggleClickTests toggleClickTests;
KeyboardPathsTests keyboardPathsTests;
SpeechTests speechTests;

}  // namespace

int runSelfTests() {
  // Preserve the last test reached if a native assertion or crash prevents
  // the final report (GUI applications otherwise only write debugger output).
  struct ReportingRunner : juce::UnitTestRunner {
    void logMessage(const juce::String& message) override { std::cout << message << std::endl; }
  } runner;
  runner.setAssertOnFailure(false);
  help::observeAnnouncements([](const juce::String& text) { announcementLog().add(text); });
  const auto filter = juce::SystemStats::getEnvironmentVariable("T3K_UI_TEST_FILTER", "");
  if (filter.isEmpty()) runner.runTestsInCategory("ui");
  else {
    juce::Array<juce::UnitTest*> selected;
    for (auto* test : juce::UnitTest::getAllTests())
      if (test->getCategory() == "ui" && test->getName().containsIgnoreCase(filter)) selected.add(test);
    if (selected.isEmpty()) { std::cout << "No UI tests match the filter" << std::endl; return 1; }
    runner.runTests(selected);
  }
  int failures = 0;
  for (int i = 0; i < runner.getNumResults(); ++i) {
    const auto* r = runner.getResult(i);
    failures += r->failures;
    std::cout << r->unitTestName << " / " << r->subcategoryName << ": " << r->passes << " passed, " << r->failures
              << " failed" << std::endl;
    for (const auto& m : r->messages) std::cout << "  " << m << std::endl;
  }
  return failures == 0 ? 0 : 1;
}

}  // namespace t3k::ui::testbed
