#include "MockSession.h"

#include <juce_events/juce_events.h>

#include <algorithm>

namespace t3k::ui::testbed {

MockSession::MockSession(const juce::var& scenario, const juce::var& fixtures)
    : authenticated_(static_cast<bool>(scenario.getProperty("auth", false))),
      offline_(static_cast<bool>(scenario.getProperty("offlineAfterLoad", false))),
      gatedPage_(fixtures["gatedPage"]),
      user_(fixtures["user"]),
      apiTones_(fixtures["apiTones"]),
      api_(scenario["api"]) {
  const auto query = scenario["query"].toString();
  if (query.contains("t3k-nav-error")) {
    flow_.phase = AuthFlow::Phase::error;
    flow_.error = "Could not reach TONE3000. Check your internet connection and try again.";
  } else if (query.contains("code=")) {
    flow_.phase = AuthFlow::Phase::returning;
  }
  // The suite's browserLanding: a canceled browse-intent return opens the
  // in-plugin browser on arrival. Posted: the root subscribes after we exist.
  const auto intent = scenario["sessionStorage"]["t3k.loginIntent"].toString();
  if (query.contains("canceled=true") && intent == "browse")
    juce::MessageManager::callAsync([self = juce::WeakReference<MockSession>(this)] {
      if (self != nullptr && self->onAuthenticated) self->onAuthenticated();
    });
}

MockSession::~MockSession() { masterReference.clear(); }

void MockSession::setFlow(AuthFlow::Phase phase, juce::String error) {
  flow_ = {};
  flow_.phase = phase;
  flow_.error = std::move(error);
  notifyAuthFlowChanged();
}

void MockSession::clearAuthError() { setFlow(AuthFlow::Phase::idle); }

std::optional<User> MockSession::user() const {
  if (!authenticated_ || !user_.isObject()) return std::nullopt;
  return User::parse(user_);
}

juce::var MockSession::apiTone(int toneId) const {
  const auto* tones = apiTones_.getArray();
  if (tones == nullptr || tones->isEmpty()) return {};
  for (const auto& t : *tones)
    if (static_cast<int>(t["id"]) == toneId) return t;
  return tones->getFirst();
}

juce::var MockSession::override(const char* group) const {
  return api_.isObject() ? api_[group] : juce::var();
}

template <typename T>
void MockSession::answer(const char* group, Reply<T> reply, std::function<Result<T>()> fallback) {
  const auto spec = override(group);
  if (spec.isString() && spec.toString() == "stall") return;  // never replies
  auto result = spec.isString() && spec.toString() == "error"
                    ? Result<T>::fail("Request failed (500).")
                    : fallback();
  juce::MessageManager::callAsync([cb = std::move(reply), r = std::move(result)]() mutable {
    cb(std::move(r));
  });
}

void MockSession::getTone(int toneId, Reply<Tone> reply) {
  answer<Tone>("tone", std::move(reply), [this, toneId] {
    const auto spec = override("tone");
    return Result<Tone>::ok(Tone::parse(spec.isObject() ? spec : apiTone(toneId)));
  });
}

void MockSession::listToneModels(int toneId, const juce::String&, Reply<std::vector<Model>> reply) {
  answer<std::vector<Model>>("models", std::move(reply), [this, toneId] {
    std::vector<Model> models;
    const auto spec = override("models");
    if (spec.isObject()) {
      if (const auto* rows = spec["data"].getArray())
        for (const auto& m : *rows) models.push_back(Model::parse(m));
      return Result<std::vector<Model>>::ok(std::move(models));
    }
    // Pad the picker with the active tone's real sibling models, like the
    // suite's /models handler.
    const auto tone = apiTone(toneId);
    const auto* own = tone["models"].getArray();
    if (own == nullptr || own->isEmpty()) return Result<std::vector<Model>>::ok(std::move(models));
    for (const auto& m : *own) models.push_back(Model::parse(m));
    const auto base = own->getFirst();
    const char* extra[] = {"AC30-6 TB Brl 2", "AC30-6 TB Brl 4", "AC30-6 TB Nrm 2", "AC30-6 TB Nrm 3"};
    int i = 1;
    for (const auto* name : extra) {
      auto copy = juce::var(base.getDynamicObject()->clone().release());
      copy.getDynamicObject()->setProperty("id", static_cast<int>(base["id"]) + i++);
      copy.getDynamicObject()->setProperty("name", name);
      models.push_back(Model::parse(copy));
    }
    return Result<std::vector<Model>>::ok(std::move(models));
  });
}

namespace {
bool containsIgnoreCase(const std::vector<juce::String>& haystack, const juce::String& needle) {
  return std::any_of(haystack.begin(), haystack.end(), [&](const auto& s) { return s.equalsIgnoreCase(needle); });
}
// Any of the picked names, or nothing picked.
bool anyPicked(const std::vector<juce::String>& picked, const std::vector<juce::String>& carried) {
  return picked.empty() || std::any_of(picked.begin(), picked.end(), [&](const auto& p) {
           return containsIgnoreCase(carried, p);
         });
}
}  // namespace

std::vector<Tone> MockSession::matching(const ToneQuery& q) const {
  std::vector<Tone> out;
  const auto text = q.text.trim();
  const auto* rows = apiTones_.getArray();
  if (rows == nullptr) return out;
  for (const auto& row : *rows) {
    auto tone = Tone::parse(row);
    if (text.isNotEmpty() && !tone.title.containsIgnoreCase(text)) continue;
    if (q.gear.isNotEmpty() && !tone.gear.equalsIgnoreCase(q.gear)) continue;
    if (q.format.isNotEmpty() && !tone.format.equalsIgnoreCase(q.format)) continue;
    if (!anyPicked(q.tags, tone.tags) || !anyPicked(q.makes, tone.makes)) continue;
    if (!q.creators.empty() && (!tone.user || !containsIgnoreCase(q.creators, tone.user->username))) continue;
    if (q.verified && (!tone.user || !tone.user->isVerified)) continue;
    out.push_back(std::move(tone));
  }
  return out;
}

void MockSession::searchTones(const ToneQuery& query, int page, int pageSize, Reply<TonePage> reply) {
  if (query.profile != Profile::none) {
    // The suite's fixed gated page, whatever the page asked for, narrowed
    // by the title search and gear the stream endpoints take.
    answer<TonePage>("gated", std::move(reply), [this, query] {
      const auto spec = override("gated");
      auto gated = TonePage::parse(spec.isObject() ? spec : gatedPage_);
      const auto text = query.text.trim();
      if (text.isNotEmpty() || query.gear.isNotEmpty()) {
        std::erase_if(gated.data, [&](const Tone& tone) {
          return (text.isNotEmpty() && !tone.title.containsIgnoreCase(text)) ||
                 (query.gear.isNotEmpty() && !tone.gear.equalsIgnoreCase(query.gear));
        });
        gated.page = 1;
        gated.totalPages = 1;
      }
      return Result<TonePage>::ok(std::move(gated));
    });
    return;
  }
  answer<TonePage>("search", std::move(reply), [this, query, page, pageSize] {
    const auto spec = override("search");
    if (spec.isObject()) return Result<TonePage>::ok(TonePage::parse(spec));
    auto all = matching(query);
    TonePage out;
    out.totalPages = std::max(1, (static_cast<int>(all.size()) + pageSize - 1) / pageSize);
    out.page = juce::jlimit(1, out.totalPages, page);
    const auto first = static_cast<size_t>((out.page - 1) * pageSize);
    const auto last = std::min(all.size(), first + static_cast<size_t>(pageSize));
    out.data.assign(all.begin() + static_cast<std::ptrdiff_t>(first), all.begin() + static_cast<std::ptrdiff_t>(last));
    return Result<TonePage>::ok(std::move(out));
  });
}

void MockSession::listTrending(const juce::String& gear, Reply<std::vector<Tone>> reply) {
  answer<std::vector<Tone>>("trending", std::move(reply), [this, gear] {
    constexpr size_t kFeedSize = 10;  // the endpoint's fixed cap
    std::vector<Tone> out;
    const auto spec = override("trending");
    const auto* rows = spec.isObject() ? spec["data"].getArray() : apiTones_.getArray();
    if (rows == nullptr) return Result<std::vector<Tone>>::ok(std::move(out));
    for (const auto& row : *rows) {
      auto tone = Tone::parse(row);
      if (gear.isNotEmpty() && !tone.gear.equalsIgnoreCase(gear)) continue;
      out.push_back(std::move(tone));
      if (out.size() == kFeedSize) break;
    }
    return Result<std::vector<Tone>>::ok(std::move(out));
  });
}

void MockSession::listTaxonomy(Taxonomy kind, const juce::String& text, Reply<std::vector<TaxonomyEntry>> reply) {
  answer<std::vector<TaxonomyEntry>>("taxonomy", std::move(reply), [this, kind, text] {
    const auto spec = override("taxonomy");
    const char* group = kind == Taxonomy::tags ? "tags" : kind == Taxonomy::makes ? "makes" : "creators";
    std::vector<TaxonomyEntry> entries;
    auto add = [&](const juce::String& name, const juce::String& avatarUrl) {
      const bool seen = std::any_of(entries.begin(), entries.end(),
                                    [&](const auto& e) { return e.name.equalsIgnoreCase(name); });
      if (!seen && (text.isEmpty() || name.containsIgnoreCase(text))) entries.push_back({name, avatarUrl});
    };
    if (spec.isObject()) {
      if (const auto* listed = spec[group].getArray())
        for (const auto& n : *listed) add(n.toString(), {});
    } else if (const auto* rows = apiTones_.getArray()) {
      // Every distinct name the fixture tones carry, in first-seen order.
      for (const auto& row : *rows) {
        const auto tone = Tone::parse(row);
        if (kind == Taxonomy::creators) {
          if (tone.user) add(tone.user->username, tone.user->avatarUrl);
        } else {
          for (const auto& name : kind == Taxonomy::tags ? tone.tags : tone.makes) add(name, {});
        }
      }
    }
    return Result<std::vector<TaxonomyEntry>>::ok(std::move(entries));
  });
}

void MockSession::selectTone(int toneId, Done done) {
  getTone(toneId, [self = juce::WeakReference<MockSession>(this), fin = std::move(done)](Result<Tone> tone) {
    if (self == nullptr) return;
    if (!tone) return fin(tone.error);
    if (self->onToneSelected) self->onToneSelected(*tone);
    fin({});
  });
}

void MockSession::setToneFavorite(int, bool, Done done) {
  juce::MessageManager::callAsync([cb = std::move(done)] { cb({}); });
}

void MockSession::ensureNativeAuth(Done done) {
  juce::MessageManager::callAsync([cb = std::move(done)] { cb({}); });
}

void MockSession::login(LoginIntent intent) {
  intent_ = intent;
  const auto authorize = override("authorize").toString();
  if (authorize == "stall" || authorize == "browser-failed") {
    // The browser never comes back: the flow sits leaving with what the
    // screen needs from it.
    setFlow(AuthFlow::Phase::leaving);
    flow_.authorizeUrl =
        "https://www.tone3000.com/api/v1/oauth/authorize?response_type=code&client_id=pk_test&redirect_uri=http"
        "%3A%2F%2F127.0.0.1%3A49152%2Fcallback&code_challenge=mock&code_challenge_method=S256&state=mock";
    if (authorize == "browser-failed") flow_.browserProblem = "Your browser couldn't be opened.";
    notifyAuthFlowChanged();
    return;
  }
  finishSignIn();
}

void MockSession::finishSignIn() {
  setFlow(AuthFlow::Phase::idle);
  authenticated_ = true;
  notifySessionChanged();
  if (intent_ == LoginIntent::browse && onAuthenticated) onAuthenticated();
}

void MockSession::cancelFlow() {
  if (flow_.phase == AuthFlow::Phase::leaving || flow_.phase == AuthFlow::Phase::returning)
    setFlow(AuthFlow::Phase::idle);
}

void MockSession::startDeviceFlow() {
  if (flow_.phase != AuthFlow::Phase::leaving) return;
  flow_.device = AuthFlow::Device{};
  notifyAuthFlowChanged();
  const auto spec = override("device").toString();
  if (spec == "stall") return;
  juce::MessageManager::callAsync([self = juce::WeakReference<MockSession>(this), spec] {
    if (self == nullptr || !self->flow_.device) return;
    auto& device = *self->flow_.device;
    if (spec == "error") {
      device.state = AuthFlow::Device::State::failed;
      device.error = "Couldn't get a code from TONE3000.";
    } else if (spec == "expired") {
      device.state = AuthFlow::Device::State::failed;
      device.error = "That code expired.";
    } else {
      device.state = AuthFlow::Device::State::waiting;
      device.userCode = "BCDF-GHJK";
      device.verificationUri = "https://www.tone3000.com/activate";
      device.verificationUriComplete = "https://www.tone3000.com/activate?user_code=BCDF-GHJK";
    }
    self->notifyAuthFlowChanged();
    if (spec == "approve")
      juce::MessageManager::callAsync([self] {
        if (self != nullptr && self->flow_.device) self->finishSignIn();
      });
  });
}

void MockSession::logout() {
  authenticated_ = false;
  notifySessionChanged();
}

void MockSession::probeSecureConnection(std::function<void(Probe)> reply) {
  juce::MessageManager::callAsync([cb = std::move(reply)] { cb(Probe::inconclusive); });
}

void MockSession::fetchPluginVersion(Reply<juce::var> reply) {
  // Only scenarios that set `api.version` have a version endpoint at all.
  answer<juce::var>("version", std::move(reply), [this] {
    const auto spec = override("version");
    return spec.isObject() ? Result<juce::var>::ok(spec) : Result<juce::var>::fail("Not found (404).");
  });
}

}  // namespace t3k::ui::testbed
