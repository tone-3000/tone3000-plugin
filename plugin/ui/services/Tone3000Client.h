// TONE3000 OAuth token store + authenticated API client (port of
// tone3000-client.ts T3KClient). Tokens persist in UiPrefs (the web UI
// kept them in the webview's localStorage, so a sign-in from those builds
// does not carry over), and a signed-in user stays
// signed in across editor sessions; the access token refreshes
// transparently (proactively within 60 s of expiry, and once more after a
// stray 401). The prefs file is shared with every other host running the
// plugin, and a refresh rotates the pair, so before refreshing (and before
// giving up on a rejected refresh) the store is re-read: a pair another
// host rotated meanwhile is adopted rather than fought. Every reply lands
// on the message thread.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

#include "HttpClient.h"
#include "UiPrefs.h"
#include "core/Result.h"
#include "model/ToneQuery.h"

namespace t3k::ui {

struct Tokens {
  juce::String access;
  juce::String refresh;
  juce::int64 expiresAtMs = 0;  // Unix ms

  static std::optional<Tokens> fromVar(const juce::var& v);
  juce::var toVar() const;
  // The /oauth/token payload ({access_token, refresh_token, expires_in}).
  static std::optional<Tokens> fromTokenResponse(const juce::var& v, juce::int64 nowMs);
};

// A device authorization (RFC 8628 §3.2): the code the user types at
// `verificationUri` (or reaches through `verificationUriComplete`, the QR
// code's target), and how to poll for it.
struct DeviceAuthorization {
  juce::String deviceCode;  // the poll's secret, never shown
  juce::String userCode;    // "BCDF-GHJK"
  juce::String verificationUri;
  juce::String verificationUriComplete;
  int expiresInS = 600;
  int intervalS = 5;

  static std::optional<DeviceAuthorization> fromVar(const juce::var& v);
};

class Tone3000Client {
public:
  // Errors carry the web client's codes / messages ("token_refresh_failed",
  // "getTone failed: 404").
  template <typename T>
  using Reply = std::function<void(Result<T>)>;

  static constexpr int kRefreshLeadMs = 60'000;

  Tone3000Client(HttpTransport& http, UiPrefs& prefs, juce::String apiOrigin, juce::String publishableKey);

  // Fires for setTokens() and every automatic refresh (native's Bearer
  // copy follows it).
  std::function<void(const Tokens&)> onTokensUpdated;
  // The refresh token was rejected (or there was none): the tokens are gone
  // and the user has to sign in again.
  std::function<void()> onAuthRequired;
  // Clock, replaceable by tests.
  std::function<juce::int64()> now = [] { return juce::Time::currentTimeMillis(); };

  bool authenticated() const { return tokens().has_value(); }
  std::optional<Tokens> tokens() const;
  void setTokens(const Tokens& tokens);
  void clearTokens();

  // A valid access token, refreshing first when it is near expiry; concurrent
  // callers share one in-flight refresh.
  void getAccessToken(Reply<juce::String> reply);

  // OAuth token endpoint
  void exchangeCode(const juce::String& code, const juce::String& codeVerifier, const juce::String& redirectUri,
                    Reply<Tokens> reply);

  // Device flow (RFC 8628)
  // POST /oauth/device_authorization: a fresh code pair for this client.
  void requestDeviceAuthorization(Reply<DeviceAuthorization> reply);
  // One poll of the token endpoint for the device code. Until the user
  // acts it fails with the server's code, which the caller acts on:
  // "authorization_pending" (ask again after the interval), "slow_down"
  // (add 5 s to it), "expired_token", "access_denied". A failure with no
  // server code ("token_refresh_failed") is a transport error.
  void pollDeviceToken(const juce::String& deviceCode, Reply<Tokens> reply);

  // Authenticated calls
  // Bearer fetch with one retry on 401 (the expiry-check race).
  void fetch(const juce::String& path, const juce::String& method, const juce::String& jsonBody,
             Reply<HttpResponse> reply);
  // Bearer when a session exists, anonymous otherwise (and anonymous again
  // when the session cannot be refreshed): the version check.
  void fetchOptionalAuth(const juce::String& path, juce::StringPairArray headers, Reply<HttpResponse> reply);

  // Typed endpoints
  void getUser(Reply<juce::var> reply);
  void getTone(int toneId, Reply<juce::var> reply);
  void setFavorite(int toneId, bool favorite, Reply<bool> reply);
  // A tone listing by its ready-made path (ToneQuery::requestPath): the
  // PaginatedResponse payload.
  void listTones(const juce::String& path, Reply<juce::var> reply);
  // /tones/trending[?gear=]: the homepage's top-10 feed, `{ data: Tone[] }`.
  // Needs no session (the signed-out preview), Bearer when there is one.
  void listTrending(const juce::String& gear, Reply<juce::var> reply);
  // /tags, /makes or /users (creators), most-used first, one page of
  // `pageSize`, narrowed by `query` when non-empty.
  void listTaxonomy(Taxonomy kind, const juce::String& query, int pageSize, Reply<juce::var> reply);
  // /models?tone_id&page_size[&architecture]; architecture < 0 omits it.
  void listModels(int toneId, int pageSize, int architecture, Reply<juce::var> reply);
  // /plugin/version with X-Device-Id and X-Plugin-Version (the running
  // build), each omitted when empty.
  void fetchPluginVersion(const juce::String& deviceId, const juce::String& localVersion, Reply<juce::var> reply);

private:
  bool fresh(const Tokens& t) const { return now() <= t.expiresAtMs - kRefreshLeadMs; }
  void refresh(const juce::String& refreshToken);
  void settleRefresh(Result<juce::String> result);
  void postTokenForm(const juce::StringPairArray& form, Reply<Tokens> reply);
  // An x-www-form-urlencoded POST to an OAuth endpoint.
  void postForm(const juce::String& path, const juce::StringPairArray& form,
                std::function<void(HttpResponse)> onDone);
  void bearerRequest(const juce::String& path, const juce::String& method, const juce::String& jsonBody,
                     const juce::String& token, std::function<void(HttpResponse)> onDone);
  void anonymousRequest(const juce::String& path, const juce::StringPairArray& headers,
                        std::function<void(HttpResponse)> onDone);
  // GET expecting a JSON body; non-2xx → "<label> failed: <status>".
  void getJson(const juce::String& path, juce::String label, Reply<juce::var> reply);

  HttpTransport& http_;
  UiPrefs& prefs_;
  juce::String origin_;
  juce::String key_;
  std::vector<Reply<juce::String>> refreshWaiters_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(Tone3000Client)
};

}  // namespace t3k::ui
