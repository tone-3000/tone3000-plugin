#include "Tone3000Session.h"

#include <juce_events/juce_events.h>

#include "T3kConfig.h"

namespace t3k::ui {

namespace {
constexpr const char* kNoKeyMessage =
    "TONE3000 publishable key not configured. Set T3K_PUBLISHABLE_KEY at build time.";
constexpr const char* kLoopbackFailed = "Could not start the local sign-in listener. Try again.";
// The launcher itself refused (AuthFlow::browserProblem; the screen adds
// what to do instead). A browser that starts and then dies reports nothing
// here: that is what the always-on fallbacks are for.
constexpr const char* kBrowserFailed = "Your browser couldn't be opened.";
// The device flow's outcomes (AuthFlow::Device::error).
constexpr const char* kDeviceCodeFailed = "Couldn't get a code from TONE3000.";
constexpr const char* kDeviceExpired = "That code expired.";
constexpr const char* kDeviceDenied = "Sign-in was declined on the other device.";
constexpr const char* kDeviceFailed = "TONE3000 stopped waiting for that code.";
// RFC 8628 §3.5: slow_down adds 5 s to the poll interval.
constexpr int kSlowDownMs = 5000;
// Only the first model: native stores and loads the active model; the
// detail card pages the full catalog separately.
constexpr int kFirstModelOnly = 1;
// Tones max out at 300 models, so one page covers the picker.
constexpr int kAllModels = 300;
// The taxonomy endpoints' page cap: one page fills a filter menu.
constexpr int kTaxonomyPageSize = 25;
}  // namespace

Tone3000Session::Config Tone3000Session::Config::fromBuild() {
  return {config::kApiOrigin, config::kPublishableKey, config::kArchitecture};
}

Tone3000Session::Tone3000Session(Backend& backend, UiPrefs& prefs, HttpTransport& http, Config config)
    : backend_(backend),
      prefs_(prefs),
      http_(http),
      config_(std::move(config)),
      client_(http, prefs, config_.apiOrigin, config_.publishableKey) {
  // Every new or refreshed token set reaches native as it happens.
  client_.onTokensUpdated = [this](const Tokens& t) { pushToken(t.access); };
  client_.onAuthRequired = [this] {
    // The refresh token was rejected: the next + / login re-authenticates.
    // The identity is gone too.
    prefs_.remove(UiPrefs::kCachedUser);
    notifySessionChanged();
  };
  loopback_.onCallback = [this](const juce::String& query) { handleCallback(query); };

  // A remembered login never fires the token listener, so native would sit
  // tokenless until the next refresh: sync once, and refresh the identity.
  if (authenticated()) {
    ensureNativeAuth({});
    refreshUser();
  }
}

Tone3000Session::~Tone3000Session() = default;

// Identity
std::optional<User> Tone3000Session::user() const {
  if (!authenticated()) return std::nullopt;
  const auto cached = prefs_.getJson(UiPrefs::kCachedUser);
  if (!cached.isObject()) return std::nullopt;
  return User::parse(cached);
}

void Tone3000Session::refreshUser() {
  client_.getUser(scope_.wrap([this](Result<juce::var> r) {
    // The avatar is decorative; auth failures surface via the flows.
    if (!r || !r->isObject()) return;
    prefs_.setJson(UiPrefs::kCachedUser, *r);
    notifySessionChanged();
  }));
}

void Tone3000Session::pushToken(const juce::String& token) { backend_.setAccessToken(token); }

void Tone3000Session::ensureNativeAuth(Done done) {
  client_.getAccessToken(scope_.wrap([this, fin = std::move(done)](Result<juce::String> token) {
    if (token) pushToken(*token);
    if (fin) fin(token ? juce::String() : token.error);
  }));
}

void Tone3000Session::logout() {
  // Auth goes everywhere at once: the persisted tokens, any mid-flight PKCE
  // state, the cached identity and native's Bearer copy. (The system
  // browser's own tone3000.com session is the user's, and stays.)
  cancelFlow();
  client_.clearTokens();
  pkce_.reset();
  lastIntent_ = LoginIntent::plain;
  prefs_.remove(UiPrefs::kCachedUser);
  pushToken({});
  notifySessionChanged();
}

// Catalog
void Tone3000Session::getTone(int toneId, Reply<Tone> reply) {
  client_.getTone(toneId, [cb = std::move(reply)](Result<juce::var> r) {
    if (!r) return cb(Result<Tone>::fail(r.error));
    cb(Result<Tone>::ok(Tone::parse(*r)));
  });
}

void Tone3000Session::listToneModels(int toneId, const juce::String& format, Reply<std::vector<Model>> reply) {
  const bool nam = format.equalsIgnoreCase("nam");
  client_.listModels(toneId, kAllModels, nam ? config_.architecture : -1, [cb = std::move(reply)](Result<juce::var> r) {
    if (!r) return cb(Result<std::vector<Model>>::fail(r.error));
    std::vector<Model> models;
    if (const auto* rows = (*r)["data"].getArray())
      for (const auto& m : *rows) models.push_back(Model::parse(m));
    cb(Result<std::vector<Model>>::ok(std::move(models)));
  });
}

void Tone3000Session::setToneFavorite(int toneId, bool favorite, Done done) {
  client_.setFavorite(toneId, favorite, [fin = std::move(done)](Result<bool> r) {
    if (fin) fin(r ? juce::String() : r.error);
  });
}

void Tone3000Session::searchTones(const ToneQuery& query, int page, int pageSize, Reply<TonePage> reply) {
  client_.listTones(query.requestPath(page, pageSize, config_.architecture), [cb = std::move(reply)](Result<juce::var> r) {
    if (!r) return cb(Result<TonePage>::fail(r.error));
    cb(Result<TonePage>::ok(TonePage::parse(*r)));
  });
}

void Tone3000Session::listTrending(const juce::String& gear, Reply<std::vector<Tone>> reply) {
  client_.listTrending(gear, [cb = std::move(reply)](Result<juce::var> r) {
    if (!r) return cb(Result<std::vector<Tone>>::fail(r.error));
    std::vector<Tone> tones;
    if (const auto* rows = (*r)["data"].getArray())
      for (const auto& t : *rows) tones.push_back(Tone::parse(t));
    cb(Result<std::vector<Tone>>::ok(std::move(tones)));
  });
}

void Tone3000Session::listTaxonomy(Taxonomy kind, const juce::String& text, Reply<std::vector<TaxonomyEntry>> reply) {
  // Tags and makes filter by name; creators by username (what the API
  // matches `creators` against), so that is the name offered.
  const bool creators = kind == Taxonomy::creators;
  client_.listTaxonomy(kind, text, kTaxonomyPageSize, [creators, cb = std::move(reply)](Result<juce::var> r) {
    if (!r) return cb(Result<std::vector<TaxonomyEntry>>::fail(r.error));
    std::vector<TaxonomyEntry> entries;
    if (const auto* rows = (*r)["data"].getArray())
      for (const auto& row : *rows) {
        const auto name = row[creators ? "username" : "name"].toString().trim();
        if (name.isNotEmpty()) entries.push_back({name, creators ? row["avatar_url"].toString() : juce::String()});
      }
    cb(Result<std::vector<TaxonomyEntry>>::ok(std::move(entries)));
  });
}

void Tone3000Session::fetchToneAndModels(int toneId, Reply<Tone> reply) {
  getTone(toneId, [this, toneId, cb = std::move(reply)](Result<Tone> tone) {
    if (!tone) return cb(std::move(tone));
    // Only NAM tones take the architecture filter; IR and other formats
    // are not NAM architectures.
    const int architecture = tone->isNam() ? config_.architecture : -1;
    client_.listModels(toneId, kFirstModelOnly, architecture, [found = *tone, cb](Result<juce::var> models) {
      if (!models) return cb(Result<Tone>::fail(models.error));
      std::vector<Model> rows;
      if (const auto* arr = (*models)["data"].getArray())
        for (const auto& m : *arr) rows.push_back(Model::parse(m));
      cb(Result<Tone>::ok(found.withModels(std::move(rows))));
    });
  });
}

void Tone3000Session::selectTone(int toneId, Done done) {
  // Both in flight together, like the web's Promise.all: the tone with its
  // first model, and a fresh token pushed to native before the load starts.
  struct Pending {
    std::optional<Tone> tone;
    bool tokenReady = false;
    bool failed = false;
  };
  auto pending = std::make_shared<Pending>();
  auto settle = scope_.wrap([this, pending, done](const juce::String& error) {
    if (pending->failed) return;
    if (error.isNotEmpty()) {
      pending->failed = true;
      if (done) done(error);
      return;
    }
    if (!pending->tone || !pending->tokenReady) return;
    if (onToneSelected) onToneSelected(*pending->tone);
    if (done) done({});
  });
  fetchToneAndModels(toneId, [pending, settle](Result<Tone> tone) {
    if (!tone) return settle(tone.error);
    pending->tone = *tone;
    settle(juce::String());
  });
  ensureNativeAuth([pending, settle](const juce::String& error) {
    if (error.isNotEmpty()) return settle(error);
    pending->tokenReady = true;
    settle(juce::String());
  });
}

// Flows
// A phase move keeps the leaving state's extras (URL, browser problem,
// device code) only while it stays leaving.
void Tone3000Session::setFlow(AuthFlow::Phase phase, juce::String error) {
  if (phase != AuthFlow::Phase::leaving) flow_ = {};
  flow_.phase = phase;
  flow_.error = std::move(error);
  notifyAuthFlowChanged();
}

void Tone3000Session::clearAuthError() { setFlow(AuthFlow::Phase::idle); }

juce::String Tone3000Session::openBrowser(const juce::String& url) {
  return juce::URL(url).launchInDefaultBrowser() ? juce::String() : juce::String(kBrowserFailed);
}

void Tone3000Session::login(LoginIntent intent) {
  if (config_.publishableKey.isEmpty()) {
    setFlow(AuthFlow::Phase::error, kNoKeyMessage);
    return;
  }
  lastIntent_ = intent;
  stopDeviceFlow();
  // The sign-in screen comes up at once; the browser takes a beat.
  setFlow(AuthFlow::Phase::leaving);
  if (!loopback_.start()) {
    setFlow(AuthFlow::Phase::error, kLoopbackFailed);
    return;
  }
  pkce_ = oauth::Pkce::generate();
  redirectUri_ = loopback_.redirectUri();
  juce::StringPairArray extra;
  extra.set("menubar", "true");
  flow_.authorizeUrl = oauth::authorizeUrl(config_.apiOrigin, config_.publishableKey, redirectUri_, *pkce_, extra);
  // Best effort, whatever the platform: a browser that would not open is
  // no dead end, since the listener is up for a pasted link and the screen
  // offers the device flow either way.
  flow_.browserProblem = openBrowser(flow_.authorizeUrl);
  notifyAuthFlowChanged();
}

void Tone3000Session::retryFlow() { login(lastIntent_); }

void Tone3000Session::cancelFlow() {
  loopback_.stop();
  pkce_.reset();
  stopDeviceFlow();
  if (flow_.phase == AuthFlow::Phase::leaving || flow_.phase == AuthFlow::Phase::returning)
    setFlow(AuthFlow::Phase::idle);
}

void Tone3000Session::handleCallback(const juce::String& query) {
  // A callback after cancel (or a stale tab) has no PKCE state: ignore it.
  if (!pkce_ || flow_.phase != AuthFlow::Phase::leaving) return;
  const auto pkce = *pkce_;
  pkce_.reset();  // single use
  loopback_.stop();
  stopDeviceFlow();  // the browser won the race

  const auto cb = oauth::Callback::parse(query, pkce.state);
  switch (cb.kind) {
    case oauth::Callback::Kind::canceled:
      setFlow(AuthFlow::Phase::idle);
      return;
    case oauth::Callback::Kind::error:
      setFlow(AuthFlow::Phase::error, cb.error);
      return;
    case oauth::Callback::Kind::code:
      break;
  }
  setFlow(AuthFlow::Phase::returning);
  client_.exchangeCode(cb.code, pkce.verifier, redirectUri_, scope_.wrap([this](Result<Tokens> tokens) {
    if (flow_.phase != AuthFlow::Phase::returning) return;  // cancelled meanwhile
    if (!tokens) {
      setFlow(AuthFlow::Phase::error, tokens.error);
      return;
    }
    finishSignIn(*tokens);
  }));
}

void Tone3000Session::finishSignIn(const Tokens& tokens) {
  const bool wantsBrowser = lastIntent_ == LoginIntent::browse;
  loopback_.stop();
  pkce_.reset();
  stopDeviceFlow();
  client_.setTokens(tokens);
  notifySessionChanged();
  refreshUser();
  if (wantsBrowser && onAuthenticated) onAuthenticated();
  setFlow(AuthFlow::Phase::idle);
}

// Device flow
void Tone3000Session::startDeviceFlow() {
  if (flow_.phase != AuthFlow::Phase::leaving) return;
  stopDeviceFlow();
  flow_.device = AuthFlow::Device{};
  notifyAuthFlowChanged();
  client_.requestDeviceAuthorization(scope_.wrap([this](Result<DeviceAuthorization> r) {
    if (flow_.phase != AuthFlow::Phase::leaving || !flow_.device ||
        flow_.device->state != AuthFlow::Device::State::requesting)
      return;  // cancelled, or superseded by a newer request
    if (!r) {
      failDevice(kDeviceCodeFailed);
      return;
    }
    deviceCode_ = r->deviceCode;
    devicePollMs_ = r->intervalS * 1000;
    deviceDeadlineMs_ = juce::Time::currentTimeMillis() + static_cast<juce::int64>(r->expiresInS) * 1000;
    auto& device = *flow_.device;
    device.state = AuthFlow::Device::State::waiting;
    device.userCode = r->userCode;
    device.verificationUri = r->verificationUri;
    device.verificationUriComplete = r->verificationUriComplete;
    notifyAuthFlowChanged();
    devicePoll_.start(devicePollMs_, [this] { pollDevice(); });
  }));
}

void Tone3000Session::pollDevice() {
  if (juce::Time::currentTimeMillis() >= deviceDeadlineMs_) {
    failDevice(kDeviceExpired);
    return;
  }
  client_.pollDeviceToken(deviceCode_, scope_.wrap([this](Result<Tokens> tokens) {
    if (flow_.phase != AuthFlow::Phase::leaving || !flow_.device ||
        flow_.device->state != AuthFlow::Device::State::waiting)
      return;
    if (tokens) {
      finishSignIn(*tokens);
      return;
    }
    if (tokens.error == "slow_down") devicePollMs_ += kSlowDownMs;
    // Pending, slow_down and a transport hiccup all mean: ask again later.
    if (tokens.error == "authorization_pending" || tokens.error == "slow_down" ||
        tokens.error == "token_refresh_failed") {
      devicePoll_.start(devicePollMs_, [this] { pollDevice(); });
      return;
    }
    failDevice(tokens.error == "expired_token"  ? kDeviceExpired
               : tokens.error == "access_denied" ? kDeviceDenied
                                                 : kDeviceFailed);
  }));
}

void Tone3000Session::failDevice(juce::String why) {
  devicePoll_.cancel();
  deviceCode_.clear();
  if (!flow_.device) return;
  flow_.device->state = AuthFlow::Device::State::failed;
  flow_.device->error = std::move(why);
  notifyAuthFlowChanged();
}

void Tone3000Session::stopDeviceFlow() {
  devicePoll_.cancel();
  deviceCode_.clear();
  flow_.device.reset();
}

// Reachability
bool Tone3000Session::online() const {
  // navigator.onLine: false only when no interface but loopback is up.
  for (const auto& address : juce::IPAddress::getAllAddresses(true))
    if (address != juce::IPAddress::local() && address != juce::IPAddress::local(true) && !address.isNull()) return true;
  return false;
}

void Tone3000Session::probeSecureConnection(std::function<void(Probe)> reply) {
  HttpRequest request;
  request.url = juce::URL(config_.apiOrigin);
  request.method = "HEAD";
  request.timeoutMs = kProbeTimeoutMs;
  http_.send(std::move(request), scope_.wrap([cb = std::move(reply)](HttpResponse r) {
    // Any response means the handshake completed. No response after the
    // full timeout is a slow network, not evidence about TLS; a quick
    // failure is the network layer refusing (DNS, refused, TLS).
    if (!r.failed()) return cb(Probe::ok);
    cb(r.elapsedMs >= kProbeTimeoutMs - 500 ? Probe::inconclusive : Probe::insecure);
  }));
}

void Tone3000Session::fetchPluginVersion(Reply<juce::var> reply) {
  client_.fetchPluginVersion(backend_.uniqueDeviceId(), backend_.pluginVersion(), std::move(reply));
}

}  // namespace t3k::ui
