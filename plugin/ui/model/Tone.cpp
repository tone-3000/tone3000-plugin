#include "Tone.h"

#include "VarReader.h"

namespace t3k::ui {

using namespace var_reader;

namespace {

// Makes/tags come straight off the network; narrow instead of trusting the
// declared types (entries may be bare strings or objects with a name).
std::vector<juce::String> names(const juce::var& obj, const char* key) {
  std::vector<juce::String> out;
  if (const auto* arr = obj[key].getArray())
    for (const auto& item : *arr) {
      const auto name = (item.isObject() ? item["name"] : item).toString().trim();
      if (name.isNotEmpty()) out.push_back(name);
    }
  return out;
}

}  // namespace

User User::parse(const juce::var& v) {
  User u;
  u.id = str(v, "id");
  u.username = str(v, "username");
  u.displayName = str(v, "display_name").trim();  // null → ""
  u.isVerified = boolean(v, "is_verified");
  u.avatarUrl = str(v, "avatar_url");
  return u;
}

Model Model::parse(const juce::var& v) { return {integer(v, "id"), str(v, "name"), str(v, "model_url"), v}; }

Tone Tone::parse(const juce::var& v) {
  Tone t;
  t.id = integer(v, "id");
  t.title = str(v, "title");
  t.description = str(v, "description");
  t.gear = str(v, "gear");
  t.format = str(v, "format");
  t.images = strings(v, "images");
  t.modelsCount = integer(v, "models_count");
  t.a2ModelsCount = integer(v, "a2_models_count");
  t.favoritesCount = integer(v, "favorites_count");
  t.isFavorite = optBool(v, "is_favorite");
  t.downloadsCount = integer(v, "downloads_count");
  t.publishedAt = str(v, "published_at");
  if (v["user"].isObject()) t.user = User::parse(v["user"]);
  t.models = list<Model>(v, "models", &Model::parse);
  t.makes = names(v, "makes");
  t.tags = names(v, "tags");
  t.url = str(v, "url");
  t.raw = v;
  return t;
}

Tone Tone::withFavorite(bool favorite, int count) const {
  Tone patched = *this;
  patched.isFavorite = favorite;
  patched.favoritesCount = count;
  if (auto* obj = raw.getDynamicObject()) {
    patched.raw = juce::var(obj->clone().release());
    patched.raw.getDynamicObject()->setProperty("is_favorite", favorite);
    patched.raw.getDynamicObject()->setProperty("favorites_count", count);
  }
  return patched;
}

Tone Tone::withModels(std::vector<Model> loaded) const {
  Tone patched = *this;
  juce::Array<juce::var> rows;
  for (const auto& m : loaded) rows.add(m.raw);
  patched.models = std::move(loaded);
  if (auto* obj = raw.getDynamicObject()) {
    patched.raw = juce::var(obj->clone().release());
    patched.raw.getDynamicObject()->setProperty("models", juce::var(rows));
  }
  return patched;
}

}  // namespace t3k::ui
