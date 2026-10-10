// Full TONE3000 catalog payloads (port of tone.ts): the tone
// the API returns for GET /tones/{id} and the browser streams, and its
// model rows. ToneSummary (ChainState.h) is native's slim projection of the
// same thing; these carry what the detail card's info panel, the model
// picker and the tone browser need on top. `raw` keeps the payload as
// received so it can be handed back to native verbatim (refreshToneMetadata,
// switchModel, loadTone) without a lossy re-serialisation.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace t3k::ui {

struct User {
  juce::String id;
  juce::String username;
  // Only ever set for a verified creator; name() falls back to the username.
  juce::String displayName;
  bool isVerified = false;
  juce::String avatarUrl;

  const juce::String& name() const { return displayName.isNotEmpty() ? displayName : username; }

  static User parse(const juce::var& v);
};

struct Model {
  int id = 0;
  juce::String name;
  juce::String modelUrl;
  juce::var raw;

  static Model parse(const juce::var& v);
};

struct Tone {
  int id = 0;
  juce::String title;
  juce::String description;
  juce::String gear;
  juce::String format;
  std::vector<juce::String> images;
  int modelsCount = 0;
  int a2ModelsCount = 0;
  int favoritesCount = 0;
  std::optional<bool> isFavorite;
  int downloadsCount = 0;
  juce::String publishedAt;
  std::optional<User> user;
  std::vector<Model> models;
  // Gear makes/models and tags: names only (entries arrive as strings or
  // objects with a name; either way the UI shows the text).
  std::vector<juce::String> makes;
  std::vector<juce::String> tags;
  juce::String url;
  juce::var raw;

  bool isNam() const { return format.equalsIgnoreCase("nam"); }
  // Models this plugin loads: A2 for NAM, otherwise models_count.
  int catalogModelCount() const { return isNam() ? a2ModelsCount : modelsCount; }

  static Tone parse(const juce::var& v);
  // A copy with the favourite fields patched (the optimistic toggle), raw
  // included so the pushed metadata agrees with what the card shows.
  Tone withFavorite(bool favorite, int count) const;
  // A copy carrying `models` (the Select flow's `{ ...tone, models }`), raw
  // included: native reads the active model from the payload it is handed.
  Tone withModels(std::vector<Model> models) const;
  // The payload for native (loadTone / swapTone): raw as received.
  juce::String toJson() const { return juce::JSON::toString(raw, true); }
};

}  // namespace t3k::ui
