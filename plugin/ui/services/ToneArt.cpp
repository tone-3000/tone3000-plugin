#include "ToneArt.h"

#include "model/ToneQuery.h"

#include <set>

namespace t3k::ui {

namespace {

juce::String cacheKey(const juce::File& folder) { return folder.getFullPathName().toLowerCase(); }

// A miss cached before the creator search was: asked once more.
constexpr int kMissVersion = 2;

bool titleMatches(const juce::String& a, const juce::String& b) {
  if (a.isEmpty() || b.isEmpty()) return false;
  if (a == b) return true;
  const auto& shorter = a.length() <= b.length() ? a : b;
  const auto& longer = a.length() <= b.length() ? b : a;
  return longer.contains(shorter) && shorter.length() * 5 >= longer.length() * 4;
}

}  // namespace

juce::var ToneArt::Art::toVar() const {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("image", image);
  obj->setProperty("username", username);
  obj->setProperty("avatar_url", avatarUrl);
  obj->setProperty("url", url);
  if (gear.isNotEmpty()) obj->setProperty("gear", gear);
  return juce::var(obj);
}

ToneArt::ToneArt(ToneSession& session, UiPrefs& prefs) : session_(session), prefs_(prefs) {}

ToneArt::~ToneArt() { stopTimer(); }

juce::String ToneArt::normalized(const juce::String& text) {
  juce::String out;
  for (auto c : text.toLowerCase())
    if (juce::CharacterFunctions::isLetterOrDigit(c)) out += c;
  return out;
}

juce::String ToneArt::creatorOf(const juce::File& folder) {
  auto nams = folder.findChildFiles(juce::File::findFiles, false, "*.nam");
  if (nams.isEmpty()) return {};
  std::sort(nams.begin(), nams.end(), [](const juce::File& a, const juce::File& b) {
    return a.getFileName().compareNatural(b.getFileName()) < 0;
  });
  // The metadata can sit anywhere in the JSON (often after the weights), so
  // the whole file is searched; a capture is a few hundred KB.
  const juce::String text = nams.getReference(0).loadFileAsString();
  const int key = text.indexOf("\"modeled_by\"");
  if (key < 0) return {};
  const int open = text.indexOfChar(text.indexOfChar(key + 12, ':'), '"');
  const int close = open < 0 ? -1 : text.indexOfChar(open + 1, '"');
  return open < 0 || close < 0 ? juce::String() : text.substring(open + 1, close).trim();
}

juce::String ToneArt::leadingWords(const juce::String& folderName, const juce::String& creator) {
  const auto who = normalized(creator);
  juce::StringArray words;
  words.addTokens(folderName.replaceCharacter('_', ' '), " ", "");
  juce::StringArray kept;
  for (const auto& word : words)
    if (const auto plain = normalized(word); plain.isNotEmpty() && plain != who && kept.size() < 2) kept.add(word.trim());
  return kept.joinIntoString(" ");
}

bool ToneArt::modelsMatch(const juce::StringArray& captures, const std::vector<Model>& models) {
  if (captures.isEmpty()) return false;
  std::set<juce::String> names;
  for (const auto& m : models) names.insert(normalized(m.name));
  int found = 0;
  for (const auto& capture : captures)
    if (names.count(capture) > 0) ++found;
  return found > 0 && (found * 2 >= captures.size() || found >= 5);
}

juce::StringArray ToneArt::captureNames(const juce::File& folder) {
  auto files = folder.findChildFiles(juce::File::findFiles, false, "*.nam;*.wav");
  if (files.isEmpty()) {
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*.nam;*.wav", juce::File::findFiles)) {
      files.add(entry.getFile());
      if (files.size() >= 300) break;
    }
  }
  juce::StringArray names;
  for (const auto& f : files) names.addIfNotAlreadyThere(normalized(f.getFileNameWithoutExtension()));
  names.removeEmptyStrings();
  return names;
}

ToneArt::Art ToneArt::artOf(const Tone& tone) {
  Art art;
  art.toneId = tone.id;
  art.title = tone.title;
  art.image = tone.images.empty() ? juce::String() : tone.images.front();
  art.url = tone.url;
  art.gear = tone.gear;
  if (tone.user) {
    art.username = tone.user->username;
    art.avatarUrl = tone.user->avatarUrl;
  }
  return art;
}

std::optional<ToneArt::Art> ToneArt::pick(const std::vector<Tone>& results, const juce::String& folderName,
                                          const juce::String& creator) {
  const auto title = normalized(folderName);
  const auto who = normalized(creator);
  for (const auto& tone : results) {
    if (tone.images.empty()) continue;  // nothing to show
    const auto toneTitle = normalized(tone.title);
    const bool titleOk = who.isEmpty() ? toneTitle == title : titleMatches(toneTitle, title);
    if (!titleOk) continue;
    if (who.isNotEmpty()) {
      if (!tone.user) continue;
      if (normalized(tone.user->username) != who && normalized(tone.user->displayName) != who) continue;
    }
    return artOf(tone);
  }
  return std::nullopt;
}

std::optional<std::optional<ToneArt::Art>> ToneArt::cached(const juce::File& folder) const {
  const auto all = prefs_.getJson(kCachePref);
  const auto entry = all[juce::Identifier(cacheKey(folder))];
  if (!entry.isObject()) return std::nullopt;
  if (static_cast<bool>(entry.getProperty("miss", false))) {
    if (static_cast<int>(entry.getProperty("v", 0)) < kMissVersion) return std::nullopt;
    return std::optional<Art>();
  }
  // A match remembered before gear was: asked once more, for its gear.
  if (!entry.hasProperty("gear")) return std::nullopt;
  Art art;
  art.toneId = entry.getProperty("id", 0);
  art.title = entry["title"].toString();
  art.image = entry["image"].toString();
  art.username = entry["username"].toString();
  art.avatarUrl = entry["avatar_url"].toString();
  art.url = entry["url"].toString();
  art.gear = entry["gear"].toString();
  return std::optional<Art>(art);
}

void ToneArt::store(const juce::File& folder, const std::optional<Art>& art) {
  auto all = prefs_.getJson(kCachePref);
  if (!all.isObject()) all = juce::var(new juce::DynamicObject());
  auto* entry = new juce::DynamicObject();
  if (art) {
    entry->setProperty("id", art->toneId);
    entry->setProperty("title", art->title);
    entry->setProperty("image", art->image);
    entry->setProperty("username", art->username);
    entry->setProperty("avatar_url", art->avatarUrl);
    entry->setProperty("url", art->url);
    entry->setProperty("gear", art->gear);
  } else {
    entry->setProperty("miss", true);
    entry->setProperty("v", kMissVersion);
  }
  all.getDynamicObject()->setProperty(juce::Identifier(cacheKey(folder)), juce::var(entry));
  prefs_.setJson(kCachePref, all);
}

bool ToneArt::cachedAnswer(const juce::Array<juce::File>& folders) const {
  for (const auto& folder : folders) {
    const auto hit = cached(folder);
    if (!hit) return false;  // never asked
    if (*hit) return true;   // a match
  }
  return true;  // all known misses
}

void ToneArt::lookup(const juce::Array<juce::File>& folders, const juce::String& creator,
                     std::function<void(std::optional<Art>)> done) {
  lookupFrom(folders, 0, creator, std::move(done));
}

void ToneArt::lookupFrom(juce::Array<juce::File> folders, int index, juce::String creator,
                         std::function<void(std::optional<Art>)> done) {
  if (index >= folders.size()) return done(std::nullopt);
  const juce::File folder = folders[index];
  if (const auto hit = cached(folder)) {
    if (*hit) return done(*hit);
    return lookupFrom(std::move(folders), index + 1, std::move(creator), std::move(done));  // a known miss
  }
  // Never asked: only the site can say, and only to a signed-in user. An
  // unanswered folder is not cached, so it is asked once signed in.
  if (!session_.authenticated()) return done(std::nullopt);
  enqueue([this, folders, index, creator, done] {
    const juce::File target = folders[index];
    // Answered while this one waited (the same folder asked twice): from
    // the cache, no second request.
    if (cached(target)) return lookupFrom(folders, index, creator, done);
    ToneQuery query;
    query.text = target.getFileName().replaceCharacter('_', ' ').trim();
    inFlight_ = true;
    lastRequestMs_ = juce::Time::currentTimeMillis();
    session_.searchTones(query, 1, 20, scope_.wrap([this, folders, index, creator, done, target](Result<TonePage> page) {
      inFlight_ = false;
      if (!page) return done(std::nullopt);  // a network error is not an answer: ask again another time
      const auto art = pick(page->data, target.getFileName(), creator);
      if (art || creator.isEmpty()) return answer(folders, index, creator, art, done);
      byCreator(folders, index, creator, done);
    }));
  });
}

void ToneArt::byCreator(juce::Array<juce::File> folders, int index, juce::String creator,
                        std::function<void(std::optional<Art>)> done) {
  const juce::File target = folders[index];
  auto captures = captureNames(target);
  const auto text = leadingWords(target.getFileName(), creator);
  if (captures.isEmpty() || text.isEmpty()) return answer(folders, index, creator, std::nullopt, done);
  enqueue([this, folders, index, creator, done, captures, text] {
    ToneQuery query;
    query.text = text;
    query.creators = {creator};
    inFlight_ = true;
    lastRequestMs_ = juce::Time::currentTimeMillis();
    session_.searchTones(query, 1, 20, scope_.wrap([this, folders, index, creator, done, captures](Result<TonePage> page) {
      inFlight_ = false;
      if (!page) return done(std::nullopt);  // not an answer: asked again another time
      // That creator's tones with a picture, at most three checked.
      const auto who = normalized(creator);
      std::vector<Tone> candidates;
      for (const auto& tone : page->data)
        if (!tone.images.empty() && tone.user &&
            (normalized(tone.user->username) == who || normalized(tone.user->displayName) == who) &&
            candidates.size() < 3)
          candidates.push_back(tone);
      checkCandidates(folders, index, creator, std::move(candidates), 0, captures, done);
    }));
  });
}

void ToneArt::checkCandidates(juce::Array<juce::File> folders, int index, juce::String creator,
                              std::vector<Tone> candidates, size_t next, juce::StringArray captures,
                              std::function<void(std::optional<Art>)> done) {
  if (next >= candidates.size()) return answer(folders, index, creator, std::nullopt, done);
  enqueue([this, folders, index, creator, candidates, next, captures, done] {
    const auto& tone = candidates[next];
    inFlight_ = true;
    lastRequestMs_ = juce::Time::currentTimeMillis();
    session_.listToneModels(tone.id, tone.format,
                            scope_.wrap([this, folders, index, creator, candidates, next, captures, done](
                                            Result<std::vector<Model>> models) {
      inFlight_ = false;
      if (!models) return done(std::nullopt);  // not an answer
      if (modelsMatch(captures, *models)) return answer(folders, index, creator, artOf(candidates[next]), done);
      checkCandidates(folders, index, creator, candidates, next + 1, captures, done);
    }));
  });
}

void ToneArt::answer(const juce::Array<juce::File>& folders, int index, const juce::String& creator,
                     const std::optional<Art>& art, const std::function<void(std::optional<Art>)>& done) {
  store(folders[index], art);
  if (art) return done(art);
  lookupFrom(folders, index + 1, creator, done);
}

void ToneArt::enqueue(std::function<void()> job) {
  queue_.push_back(std::move(job));
  if (!isTimerRunning()) startTimer(100);
  timerCallback();
}

void ToneArt::timerCallback() {
  if (queue_.empty()) {
    stopTimer();
    return;
  }
  if (inFlight_ || juce::Time::currentTimeMillis() - lastRequestMs_ < kSpacingMs) return;
  auto job = std::move(queue_.front());
  queue_.pop_front();
  job();
}

}  // namespace t3k::ui
