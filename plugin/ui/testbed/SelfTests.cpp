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
#include "core/Pitch.h"
#include "core/RichText.h"
#include "core/TextFlow.h"
#include "model/ChainState.h"
#include "model/Tone.h"
#include "model/ToneQuery.h"
#include "services/ConnectionGate.h"
#include "services/LoopbackServer.h"
#include "services/OAuth.h"
#include "services/Pointer.h"
#include "services/Tone3000Client.h"
#include "services/UiPrefs.h"
#include "services/UpdateCheck.h"
#include "views/browser/FilterChip.h"
#include "views/browser/Paginator.h"
#include "views/browser/ToneCard.h"
#include "views/gallery/GalleryGeometry.h"
#include "views/gallery/GalleryTile.h"
#include "widgets/Avatar.h"
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
#include "widgets/Popover.h"
#include "widgets/SegmentedText.h"

namespace t3k::ui::testbed {

namespace {

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

    beginTest("buttons take focus from Tab, never from a click");
    IconButton icon(Icon::X);
    expect(icon.getWantsKeyboardFocus());
    expect(!icon.getMouseClickGrabsKeyboardFocus());

    beginTest("a button's name: title, else text, else the hint lead");
    icon.setHelpText(help::text(help::Key::undo));
    expectEquals(icon.accessibleName(), help::lead(help::text(help::Key::undo)));
    expectEquals(icon.createAccessibilityHandler()->getTitle(), icon.accessibleName());
    expectEquals(icon.createAccessibilityHandler()->getHelp(), help::text(help::Key::undo));
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
    expect(!knob.getMouseClickGrabsKeyboardFocus());
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
    window.setVisible(true);
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

    beginTest("a click never focuses a button or a knob, and drops any focus held");
    auto* knob = dynamic_cast<Knob*>(drive::find(root, [](juce::Component& c) {
      return dynamic_cast<Knob*>(&c) != nullptr && c.isShowing() && c.isEnabled();
    }));
    auto* inputMode = drive::byHelpPrefix(root, "Input Mode:");
    expect(knob != nullptr && inputMode != nullptr);
    if (knob == nullptr || inputMode == nullptr) return;
    expect(key(*peer, juce::KeyPress::tabKey));
    expect(focused() != nullptr);
    click(*peer, *knob);
    expect(focused() == nullptr);
    expect(!key(*peer, juce::KeyPress::returnKey));  // Enter goes to the host after mouse work
    click(*peer, *inputMode);  // opens its menu: the panel takes focus, the button never does
    expect(focused() != inputMode);
    key(*peer, juce::KeyPress::escapeKey);
    pump(30);
    expect(focused() == nullptr);

    beginTest("a keyboard-opened menu: arrows walk the rows, Escape returns to the anchor");
    inputMode->grabKeyboardFocus();
    expect(focused() == inputMode);
    expect(key(*peer, juce::KeyPress::returnKey));  // Enter presses the button
    pump(30);
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
    window->setVisible(true);
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
    live.key(KP::escapeKey);
    expect(live.focused() == nullptr);

    beginTest("a focused control passes the page's keys up to the page");
    view.setViewPosition(0, 0);
    expect(live.key(KP::tabKey));  // the first stop: the Close button
    live.pump(30);
    expect(dynamic_cast<Clickable*>(live.focused()) != nullptr);
    expect(live.key(KP::downKey));
    expectEquals(view.getViewPositionY(), DragScroller::kKeyLineStep);
    expect(!live.key(KP::spaceKey));  // and Space still falls through to the host
    live.key(KP::escapeKey);
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
      live.pump(500);  // any drag's momentum has run out
      scroller->setViewPosition(0, 0);
    };
    const auto along = [&](float d) { return vertical ? juce::Point<float>(0.0f, d) : juce::Point<float>(d, 0.0f); };
    const auto inView = [&](const juce::Component& c) {
      const auto box = content->getLocalArea(&c, c.getLocalBounds());
      const int start = vertical ? box.getY() : box.getX(), end = vertical ? box.getBottom() : box.getRight();
      const int visible = vertical ? scroller->getMaximumVisibleHeight() : scroller->getMaximumVisibleWidth();
      return (start >= pos() && end <= pos() + visible) || end - start > visible;
    };
    const int forward = vertical ? KP::downKey : KP::rightKey;
    const auto middle = surface.grab ? live.at(*scroller, surface.grab(*scroller)) : live.centre(*scroller);

    // A plain wheel turn pans it, whichever way it runs.
    rewind();
    live.wheel(middle, 0.0f, -0.5f);
    expect(pos() > 0, "the wheel pans");

    // A mouse drag pans it.
    rewind();
    live.mouse().drag(middle, along(-150.0f));
    expect(pos() > 0, "a mouse drag pans");

    // A finger pans it, where the platform has touch sources.
    rewind();
    if (auto finger = live.finger()) {
      finger->drag(middle, along(-150.0f));
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
    window.setVisible(true);
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
    window.setVisible(true);
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
    window.setVisible(true);
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
    window.setVisible(true);
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
    window.setVisible(true);
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
// button's name (its accessible title), and a reopened tuner starts unmuted.
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

    beginTest("a reopened tuner starts unmuted");
    mute->onClick();
    root.setTunerShown(false);
    pump(50);
    root.setTunerShown(true);
    pump(100);
    expect(named(root, "Mute output") != nullptr);
    expect(named(root, "Unmute output") == nullptr);
    window.setVisible(false);
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
    window.setVisible(true);
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
    window.setVisible(true);
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
      window.setVisible(true);
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
      window.setVisible(true);
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

HtmlTests htmlTests;
FontTests fontTests;
RichFlowTests richFlowTests;
AccessibilityTests accessibilityTests;
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
MidiMapCommitTests midiMapCommitTests;
TunerMuteTests tunerMuteTests;
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
ToneModelTests toneModelTests;
ToneQueryTests toneQueryTests;
ReadoutTests readoutTests;

}  // namespace

int runSelfTests() {
  juce::UnitTestRunner runner;
  runner.setAssertOnFailure(false);
  runner.runTestsInCategory("ui");
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
