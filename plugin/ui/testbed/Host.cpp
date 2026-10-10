#include "Host.h"

#include "core/Design.h"

namespace t3k::ui::testbed {

namespace {

// Seed UiPrefs the way a real install would already hold them: the
// scenario's own entries, the auth tokens + cached user when `auth`, and the
// hints toggle.
void seedPrefs(UiPrefs& prefs, const Scenario& scenario, const juce::var& fixtures) {
  auto seed = [](const juce::var& obj, auto&& put) {
    if (auto* props = obj.getDynamicObject())
      for (const auto& [name, value] : props->getProperties()) put(name.toString(), value.toString());
  };
  seed(scenario.data["localStorage"], [&](auto k, auto v) { prefs.set(k, v); });
  seed(scenario.data["sessionStorage"], [&](auto k, auto v) { prefs.session[k] = v; });
  if (static_cast<bool>(scenario.data.getProperty("auth", false))) {
    auto* tokens = new juce::DynamicObject();
    tokens->setProperty("access_token", "mock-access-token");
    tokens->setProperty("refresh_token", "mock-refresh-token");
    tokens->setProperty("expires_at", juce::Time::currentTimeMillis() + 12 * 3600 * 1000);
    prefs.setJson(UiPrefs::kTokens, juce::var(tokens));
    prefs.setJson(UiPrefs::kCachedUser, fixtures["user"]);
  }
  if (!scenario.hints()) prefs.setBool(UiPrefs::kShowHints, false);
}

// The suite's fixtureSvg for keys without a real photo: a deterministic
// two-stop gradient with a translucent disc, keyed by a hash of the name.
// (The SVG also drew the key as a label; at avatar size it is invisible and
// is left out.)
juce::Image placeholderImage(const juce::String& key) {
  juce::uint32 hash = 7;
  for (const auto c : key.toStdString()) hash = hash * 31u + static_cast<juce::uint32>(static_cast<unsigned char>(c));
  const float hue = static_cast<float>(hash % 360) / 360.0f;
  const float hue2 = static_cast<float>((hash % 360 + 40) % 360) / 360.0f;
  const bool avatar = key.startsWith("avatar");
  auto hsl = [](float h, float sat, float light) {
    return juce::Colour::fromHSL(h, sat, light, 1.0f);
  };
  constexpr int kSize = 400;
  juce::Image image(juce::Image::ARGB, kSize, kSize, true);
  juce::Graphics g(image);
  g.setGradientFill(juce::ColourGradient(hsl(hue, 0.45f, avatar ? 0.45f : 0.26f), 0, 0,
                                         hsl(hue2, 0.55f, avatar ? 0.30f : 0.12f), kSize, kSize, false));
  g.fillAll();
  g.setColour(hsl(hue2, 0.50f, 0.35f).withAlpha(0.35f));
  const float cx = 120.0f + static_cast<float>(hash % 160), cy = 90.0f + static_cast<float>(hash % 120);
  g.fillEllipse(cx - 90, cy - 90, 180, 180);
  // The key as a bold 34px label, baseline at y = 212.
  const auto label = key.replaceCharacters("-_", "  ").toUpperCase();
  g.setColour(juce::Colours::white.withAlpha(0.82f));
  g.setFont(juce::Font(juce::FontOptions("Helvetica", 34.0f, juce::Font::bold)));
  g.drawSingleLineText(label, kSize / 2, 212, juce::Justification::horizontallyCentred);
  return image;
}

}  // namespace

juce::File fixturesDir() { return juce::File(T3K_TESTBED_FIXTURES); }

ScaledHost::ScaledHost(Backend& backend, const Scenario& scenario, const juce::var& fixtures)
    : zoom_(scenario.zoom()),
      session(scenario.data, fixtures),
      services(backend, session, *this, prefs, /*updateNotice=*/true) {
  seedPrefs(prefs, scenario, fixtures);
  // Artwork comes from fixtures/img (real gear photos) instead of the
  // network, and every fetch fails under `imagesOffline`.
  services.images.offline = static_cast<bool>(scenario.data.getProperty("imagesOffline", false));
  services.images.localOverride =
      [host = fixtures["imgHost"].toString()](const juce::String& url) -> std::optional<juce::Image> {
    if (host.isEmpty() || !url.startsWith(host)) return std::nullopt;
    const auto key = url.fromLastOccurrenceOf("/", false, false);
    const auto file = fixturesDir().getChildFile("img").getChildFile(key + ".jpg");
    if (file.existsAsFile()) return juce::ImageFileFormat::loadFrom(file);
    return placeholderImage(key);
  };
  root = std::make_unique<PluginRoot>(services);
  addAndMakeVisible(*root);
  setVisible(true);  // offscreen captures have no window to make us visible
  setSize(juce::roundToInt(design::kWidth * zoom_), juce::roundToInt(root->designHeight() * zoom_));
  resized();  // the root's chrome report already sized us; fit it now that it exists
}

void ScaledHost::setExtraContentHeight(int total, int) {
  if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) {
    const double scale = getWidth() / static_cast<double>(design::kWidth);
    window->setContentComponentSize(juce::roundToInt(design::kWidth * scale),
                                    juce::roundToInt((design::kHeight + total) * scale));
  } else {
    setSize(juce::roundToInt(design::kWidth * zoom_),  // offscreen capture: the scenario's zoom, 1:1 by default
            juce::roundToInt((design::kHeight + total) * zoom_));
  }
}

void ScaledHost::resized() {
  if (root == nullptr) return;
  const double scale = juce::jmax(0.05, juce::jmin(getWidth() / double(design::kWidth),
                                                   getHeight() / double(root->designHeight())));
  root->setTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
  root->setTopLeftPosition(juce::roundToInt((getWidth() - design::kWidth * scale) / 2), 0);
  services.zoom.set(scale);
}

}  // namespace t3k::ui::testbed
