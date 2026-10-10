// The TONE3000 session as the views see it (port of useToneSession.ts +
// useT3kSelect.ts): who is signed in, the catalog calls the detail card and
// the tone browser make, and the OAuth flows. Replies land on the message
// thread; callers wrap them in an AsyncScope so a swap mid-flight or a
// closed card never hears back.
//
// Implementations: Tone3000Session (HTTP client, token store, loopback OAuth
// redirect) in the plugin, MockSession in the testbed, and SignedOutSession
// below when no session backing exists.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

#include "core/Result.h"
#include "model/Tone.h"
#include "model/ToneQuery.h"

namespace t3k::ui {

// One page of tone results (the API's PaginatedResponse<Tone[]>).
// One option of a taxonomy filter: a tag or make name, or a creator's
// username with their avatar.
struct TaxonomyEntry {
  juce::String name;
  juce::String avatarUrl;  // creators only
};

struct TonePage {
  std::vector<Tone> data;
  int page = 1;
  int totalPages = 1;

  static TonePage parse(const juce::var& payload) {
    TonePage out;
    if (const auto* rows = payload["data"].getArray())
      for (const auto& t : *rows) out.data.push_back(Tone::parse(t));
    out.page = static_cast<int>(payload.getProperty("page", 1));
    out.totalPages = static_cast<int>(payload.getProperty("total_pages", 1));
    return out;
  }
};

class ToneSession {
public:
  struct Listener {
    virtual ~Listener() = default;
    // Signed in / out, or the identity refreshed.
    virtual void sessionChanged() = 0;
    // The OAuth flow moved phase (see AuthFlow).
    virtual void authFlowChanged() {}
  };

  // Where a sign-in stands (useT3kSelect's OAuthPhase). The sign-in screen
  // takes over the plugin while one is in flight:
  //   idle       nothing going on
  //   leaving    the browser has been sent to tone3000.com and the loopback
  //              listener is waiting for it (or a device code is pending)
  //   returning  back with an authorization code; tokens are being resolved
  //   error      the flow failed; `error` is the user-facing reason, and
  //              retryFlow() restarts whichever flow it was
  //
  // While leaving, the screen offers the two ways round a browser that did
  // not open: `authorizeUrl` to copy into any browser (the loopback listener
  // takes the redirect from whichever browser completes it), and the device
  // flow (RFC 8628: a code entered on another device) once
  // startDeviceFlow() is called. `browserProblem` is set when the launcher
  // itself refused to open the system browser, so the copy says so.
  struct AuthFlow {
    enum class Phase { idle, leaving, returning, error };
    Phase phase = Phase::idle;
    juce::String error;
    juce::String authorizeUrl;
    juce::String browserProblem;

    struct Device {
      enum class State { requesting, waiting, failed };
      State state = State::requesting;
      juce::String userCode;                // "BCDF-GHJK", as shown to the user
      juce::String verificationUri;         // where to type it
      juce::String verificationUriComplete;  // the QR code's target
      juce::String error;                   // failed: why, for the user
    };
    std::optional<Device> device;
  };

  template <typename T>
  using Reply = std::function<void(Result<T>)>;
  // "" = success.
  using Done = std::function<void(const juce::String& error)>;

  // Why the user is being sent to sign in: `browse` (the tone browser's
  // sign-in gate) reopens the tone browser on return; a plain sign-in
  // (account menu, info panel) lands on the main screen.
  enum class LoginIntent { plain, browse };

  virtual ~ToneSession() = default;

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

  // A fully resolved tone (first model embedded) landed from selectTone().
  // Native already holds a fresh access token when this fires. Owned by the
  // load flow.
  std::function<void(const Tone& tone)> onToneSelected;
  // A browse-intent login finished: the caller opens the tone browser.
  std::function<void()> onAuthenticated;

  virtual bool authenticated() const = 0;
  virtual std::optional<User> user() const = 0;
  // An OAuth return is still being resolved (the token exchange is in
  // flight): the browser holds its fetch until this clears.
  bool authPending() const { return authFlow().phase == AuthFlow::Phase::returning; }

  // Catalog
  // GET /tones/{id}: the full tone (description, makes, tags, url, models).
  virtual void getTone(int toneId, Reply<Tone> reply) = 0;
  // GET /models?tone_id=: every model of the tone (NAM keeps the A2
  // architecture filter, which is all the plugin loads).
  virtual void listToneModels(int toneId, const juce::String& format,
                              Reply<std::vector<Model>> reply) = 0;
  // PUT / DELETE /tones/{id}/favorite (idempotent).
  virtual void setToneFavorite(int toneId, bool favorite, Done done) = 0;
  // One page of the browser's results: GET /tones/search for the query, or
  // the profile filter's GET /tones/{downloaded|favorited|created}.
  virtual void searchTones(const ToneQuery& query, int page, int pageSize, Reply<TonePage> reply) = 0;
  // The signed-out preview's feed: GET /tones/trending, the homepage's top
  // 10, narrowed to one gear type when `gear` is set. Needs no session.
  virtual void listTrending(const juce::String& gear, Reply<std::vector<Tone>> reply) = 0;
  // The options a taxonomy filter offers: GET /tags, /makes or /users, the
  // most-used first, narrowed by `text` when given (up to one page).
  virtual void listTaxonomy(Taxonomy kind, const juce::String& text, Reply<std::vector<TaxonomyEntry>> reply) = 0;
  // Resolve a tone picked in the browser: the tone, its first loadable
  // model and a fresh token for native, then onToneSelected. `done` reports
  // failure (the card shows a pick error).
  virtual void selectTone(int toneId, Done done) = 0;

  // Native downloads
  // Native fetches model files itself with a Bearer header. Guarantee it
  // holds a fresh token before a download starts (refreshing near expiry);
  // an error means the session expired and the tokens were cleared.
  virtual void ensureNativeAuth(Done done) = 0;

  // Flows
  // The OAuth login flow: sign in on tone3000.com in the system browser and
  // come straight back.
  virtual void login(LoginIntent intent = LoginIntent::plain) = 0;
  virtual void logout() = 0;
  virtual const AuthFlow& authFlow() const = 0;
  // Restart whichever flow last left for tone3000.com (the error overlay's
  // Try again).
  virtual void retryFlow() = 0;
  // Give up waiting for the system browser to come back (the sign-in
  // screen's ←): stop listening and return to idle.
  virtual void cancelFlow() = 0;
  // Drop the error without restarting.
  virtual void clearAuthError() = 0;
  // While leaving: ask TONE3000 for a device code and poll for its
  // approval alongside the browser (AuthFlow::device follows). Called again
  // after a failure for a fresh code. Whichever of the two completes first
  // signs in.
  virtual void startDeviceFlow() = 0;

  // Reachability (useConnectionGate.ts)
  // Instant OS-level check: false means no network interface is up at all.
  // True doesn't promise the internet is reachable; the recovery paths do.
  virtual bool online() const = 0;
  // One HTTPS reachability probe of the TONE3000 origin, replying on the
  // message thread. Only the TLS handshake matters, not the response:
  //   ok           the handshake completed
  //   insecure     it failed at the network layer (DNS, refused, TLS)
  //   inconclusive timed out or errored oddly; no evidence either way
  enum class Probe { ok, insecure, inconclusive };
  virtual void probeSecureConnection(std::function<void(Probe)> reply) = 0;

  // Update check (useUpdateNotice.ts)
  // GET /plugin/version, with the Bearer when signed in (beta payloads),
  // X-Device-Id and X-Plugin-Version (the running build); the raw JSON
  // body, validated by UpdateCheck.
  virtual void fetchPluginVersion(Reply<juce::var> reply) = 0;

protected:
  void notifySessionChanged() {
    listeners_.call([](Listener& l) { l.sessionChanged(); });
  }
  void notifyAuthFlowChanged() {
    listeners_.call([](Listener& l) { l.authFlowChanged(); });
  }

private:
  juce::ListenerList<Listener> listeners_;
};

// No TONE3000 backing at all: signed out, every catalog call fails, so the
// UI behaves as it does with the network unreachable. A stand-in for hosts
// without a session (tests, tooling); the plugin runs Tone3000Session.
class SignedOutSession final : public ToneSession {
public:
  bool authenticated() const override { return false; }
  std::optional<User> user() const override { return std::nullopt; }
  void getTone(int, Reply<Tone> reply) override { reply(Result<Tone>::fail(kNotSignedIn)); }
  void listToneModels(int, const juce::String&, Reply<std::vector<Model>> reply) override {
    reply(Result<std::vector<Model>>::fail(kNotSignedIn));
  }
  void setToneFavorite(int, bool, Done done) override { done(kNotSignedIn); }
  void searchTones(const ToneQuery&, int, int, Reply<TonePage> reply) override {
    reply(Result<TonePage>::fail(kNotSignedIn));
  }
  void listTrending(const juce::String&, Reply<std::vector<Tone>> reply) override {
    reply(Result<std::vector<Tone>>::fail(kNotSignedIn));
  }
  void listTaxonomy(Taxonomy, const juce::String&, Reply<std::vector<TaxonomyEntry>> reply) override {
    reply(Result<std::vector<TaxonomyEntry>>::fail(kNotSignedIn));
  }
  void selectTone(int, Done done) override { done(kNotSignedIn); }
  void ensureNativeAuth(Done done) override { done(kNotSignedIn); }
  void login(LoginIntent) override {}
  void logout() override {}
  const AuthFlow& authFlow() const override { return flow_; }
  void retryFlow() override {}
  void cancelFlow() override {}
  void clearAuthError() override {}
  void startDeviceFlow() override {}
  bool online() const override { return true; }
  void probeSecureConnection(std::function<void(Probe)> reply) override { reply(Probe::inconclusive); }
  void fetchPluginVersion(Reply<juce::var> reply) override { reply(Result<juce::var>::fail(kNotSignedIn)); }

private:
  static constexpr const char* kNotSignedIn = "Not signed in.";
  AuthFlow flow_;
};

}  // namespace t3k::ui
