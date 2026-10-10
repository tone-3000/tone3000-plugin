// Artwork for local captures from the TONE3000 tone their folder came from
// (plugin/docs/library.md). A linked capture collection is often a pile of
// TONE3000 downloads: folders named after the tone, files whose NAM metadata
// names the creator (`modeled_by`). One search for the folder name, and a
// result whose title matches the folder and whose creator matches the files
// is that tone: its photo goes on the block (ChainStore::setLocalToneArt).
//
// Responsibly, because it talks to the site on the user's behalf:
//  - only for a local block playing a file from disk (a Library load, a
//    drop, a restored project), and only while signed in;
//  - once per folder, ever: every answer is cached (UiPrefs), a miss too;
//  - one request at a time, at least kSpacingMs apart, queued;
//  - a capture's folder first, then up to two folders above it (a generic
//    "DI" or "favorites" inside the tone's folder), each cached on its own.
//
// A folder renamed by its owner ("Bogner Uberschall Rev Blue (E34L) - Amp
// Head" for the tone "Bogner Uberschall") misses the title search. When its
// files name a creator, that creator's tones are searched for the folder
// name's first words, and a tone whose models are the folder's files (by
// name) is the one: a few requests more, once per folder.
#pragma once

#include <juce_events/juce_events.h>

#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "ToneSession.h"
#include "UiPrefs.h"
#include "core/AsyncScope.h"

namespace t3k::ui {

class ToneArt : private juce::Timer {
public:
  static constexpr const char* kCachePref = "t3k.libraryArt";
  static constexpr int kSpacingMs = 1000;

  struct Art {
    int toneId = 0;
    juce::String title, image, username, avatarUrl, url;
    juce::String gear;  // the tone's catalog gear (truer than a file's metadata)
    // { image, username, avatar_url, url }: setLocalToneArt's shape.
    juce::var toVar() const;
  };

  ToneArt(ToneSession& session, UiPrefs& prefs);
  ~ToneArt() override;

  // The art for the first of `folders` (nearest first) that matches, or
  // nothing. Cached answers come back at once; the rest wait their turn in
  // the queue. `creator` is the captures' modeled_by ("" when they have none).
  void lookup(const juce::Array<juce::File>& folders, const juce::String& creator,
              std::function<void(std::optional<Art>)> done);

  // The cache alone answers a lookup of `folders` (a match, or every one a
  // known miss): no creator needed, nothing to ask.
  bool cachedAnswer(const juce::Array<juce::File>& folders) const;

  // The modeled_by of the first .nam in `folder` (natural order), or "".
  static juce::String creatorOf(const juce::File& folder);

  // The matching rule, pure: a result whose title matches the folder name
  // (letters and digits only, case ignored; or one contains the other at
  // 80% of its length) and whose creator matches `creator` the same way (its
  // username or display name). With no creator, only an exact title match.
  static std::optional<Art> pick(const std::vector<Tone>& results, const juce::String& folderName,
                                 const juce::String& creator);
  static juce::String normalized(const juce::String& text);
  // The creator search's text: the folder name's first two words, the
  // creator's name left out ("2DOR Fortin Evil Pumpkin" by 2dor: "Fortin Evil").
  static juce::String leadingWords(const juce::String& folderName, const juce::String& creator);
  // The tone whose models are the folder's captures: at least half of
  // `captures` (normalized names) among `models`' names, or five of them.
  static bool modelsMatch(const juce::StringArray& captures, const std::vector<Model>& models);
  // The normalized names of a folder's captures (its own files; else those
  // under it, up to 300).
  static juce::StringArray captureNames(const juce::File& folder);

private:
  void lookupFrom(juce::Array<juce::File> folders, int index, juce::String creator,
                  std::function<void(std::optional<Art>)> done);
  // The title search missed `folders[index]`: its creator's tones, by its
  // files (above).
  void byCreator(juce::Array<juce::File> folders, int index, juce::String creator,
                 std::function<void(std::optional<Art>)> done);
  // Checks `candidates` (from `next` on) by their models; the first match
  // answers, none: the folder is a miss.
  void checkCandidates(juce::Array<juce::File> folders, int index, juce::String creator, std::vector<Tone> candidates,
                       size_t next, juce::StringArray captures, std::function<void(std::optional<Art>)> done);
  // Done with `folders[index]` (cached): its art, or on to the next folder.
  void answer(const juce::Array<juce::File>& folders, int index, const juce::String& creator,
              const std::optional<Art>& art, const std::function<void(std::optional<Art>)>& done);
  // Runs `job` in the queue, a request at a time, kSpacingMs apart.
  void enqueue(std::function<void()> job);
  static Art artOf(const Tone& tone);
  // Cached: nullopt = never asked; an empty Art = asked, no match.
  std::optional<std::optional<Art>> cached(const juce::File& folder) const;
  void store(const juce::File& folder, const std::optional<Art>& art);
  void timerCallback() override;

  ToneSession& session_;
  UiPrefs& prefs_;
  std::deque<std::function<void()>> queue_;
  bool inFlight_ = false;
  juce::int64 lastRequestMs_ = 0;
  AsyncScope scope_;
};

}  // namespace t3k::ui
