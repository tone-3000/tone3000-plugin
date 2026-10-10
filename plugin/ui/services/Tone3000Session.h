// The plugin's ToneSession (port of useToneSession.ts + useT3kSelect.ts on
// top of Tone3000Client): the signed-in identity, the OAuth login through
// the system browser with a loopback redirect, and native's copy of the
// access token. The webview's full-page redirect is gone, so the values the
// web parked in sessionStorage across it (login intent, PKCE) are plain
// fields here.
//
// Opening the browser is best effort: JUCE's launch reports success once
// the launcher process forks, so a browser that refuses to start (Chromium
// under root on Linux, say) would leave the user waiting on nothing. So
// the sign-in screen always carries the authorize URL to copy into any
// browser, and the device flow (RFC 8628) polls for a code entered on a
// phone. All three paths feed the same loopback / token endpoints, so the
// first to finish signs in and the others are dropped.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

#include "LoopbackServer.h"
#include "OAuth.h"
#include "Tone3000Client.h"
#include "ToneSession.h"
#include "UiPrefs.h"
#include "backend/Backend.h"
#include "core/AsyncScope.h"
#include "core/DelayedCall.h"

namespace t3k::ui {

class Tone3000Session final : public ToneSession {
public:
  struct Config {
    juce::String apiOrigin;
    juce::String publishableKey;
    int architecture = 2;  // NAM model architecture filter; < 0 disables

    // The build's .env values (T3kConfig.h).
    static Config fromBuild();
  };

  // useConnectionGate.ts PROBE_TIMEOUT_MS.
  static constexpr int kProbeTimeoutMs = 8000;

  Tone3000Session(Backend& backend, UiPrefs& prefs, HttpTransport& http, Config config);
  ~Tone3000Session() override;


  bool authenticated() const override { return client_.authenticated(); }
  std::optional<User> user() const override;

  void getTone(int toneId, Reply<Tone> reply) override;
  void listToneModels(int toneId, const juce::String& format, Reply<std::vector<Model>> reply) override;
  void setToneFavorite(int toneId, bool favorite, Done done) override;
  void searchTones(const ToneQuery& query, int page, int pageSize, Reply<TonePage> reply) override;
  void listTrending(const juce::String& gear, Reply<std::vector<Tone>> reply) override;
  void listTaxonomy(Taxonomy kind, const juce::String& text, Reply<std::vector<TaxonomyEntry>> reply) override;
  void selectTone(int toneId, Done done) override;
  void ensureNativeAuth(Done done) override;

  void login(LoginIntent intent) override;
  void logout() override;
  const AuthFlow& authFlow() const override { return flow_; }
  void retryFlow() override;
  void cancelFlow() override;
  void clearAuthError() override;
  void startDeviceFlow() override;

  bool online() const override;
  void probeSecureConnection(std::function<void(Probe)> reply) override;
  void fetchPluginVersion(Reply<juce::var> reply) override;

private:
  void setFlow(AuthFlow::Phase phase, juce::String error = {});
  void handleCallback(const juce::String& query);
  // Tokens landed (from either path): adopt them and close the flow.
  void finishSignIn(const Tokens& tokens);
  // Send the system browser to `url`; "" on success, else why not.
  static juce::String openBrowser(const juce::String& url);
  void pollDevice();
  void failDevice(juce::String why);
  void stopDeviceFlow();
  // Tone + its first loadable model (what native loads).
  void fetchToneAndModels(int toneId, Reply<Tone> reply);
  void pushToken(const juce::String& token);
  void refreshUser();

  Backend& backend_;
  UiPrefs& prefs_;
  HttpTransport& http_;
  Config config_;
  Tone3000Client client_;
  LoopbackServer loopback_;
  AsyncScope scope_;

  AuthFlow flow_;
  std::optional<oauth::Pkce> pkce_;
  juce::String redirectUri_;
  // The intent of the flow in flight / last left for (retryFlow reruns it).
  LoginIntent lastIntent_ = LoginIntent::plain;

  // The device flow in flight: its secret, the poll cadence and the code's
  // own deadline (a poll that never reaches the server would otherwise wait
  // on a code the server has long forgotten).
  juce::String deviceCode_;
  int devicePollMs_ = 0;
  juce::int64 deviceDeadlineMs_ = 0;
  DelayedCall devicePoll_;
};

}  // namespace t3k::ui
