#include "AccountSection.h"

#include <cmath>
#include <optional>

#include "T3kConfig.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "widgets/Avatar.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
// Only the iOS app offers account deletion (see the header).
constexpr bool kOffersDeletion = design::kIos;

// The website's delete page. A browser that is not signed in is sent to
// sign in first and comes back to it.
const char* const kDeleteAccountPath = "/settings/delete-account";

juce::String siteUrl(const juce::String& path) { return juce::String(config::kApiOrigin) + path; }

// An app extension has no way to open a URL (docs/ios.md, Known gaps).
bool canOpenLinks() { return !(design::kIos && juce::SystemStats::isRunningInAppExtensionSandbox()); }
}  // namespace

// LinkRow
// A link out to the website: leading glyph, 14px label, external-link glyph,
// all in the link's colour. The leading slot holds a Lucide icon, or the
// avatar when there is none. The whole row is the target, like the form's
// other text links.
class AccountSection::LinkRow : public Clickable {
public:
  // The leading slot is the account dropdown's icon size; the trailing glyph
  // is smaller so it reads as a mark on the label, not a second icon.
  static constexpr int kLeading = 18, kLeadingGap = 10;
  static constexpr int kExternal = 14, kExternalGap = 6;
  static constexpr int kHeight = 20;

  LinkRow(const juce::String& label, std::optional<Icon> icon, juce::Colour colour)
      : Clickable(label), icon_(icon), colour_(colour) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    if (!icon_) addAndMakeVisible(avatar);
  }

  Avatar avatar;

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds().toFloat();
    if (icon_) Icons::draw(g, *icon_, box.withWidth(kLeading).withSizeKeepingCentre(kLeading, kLeading), colour_);

    const auto font = Fonts::sans(form::kBodyPx);
    const float lineH = static_cast<float>(Fonts::normalLineHeight(form::kBodyPx));
    const float textX = kLeading + kLeadingGap;
    const float textW = Fonts::width(font, getButtonText());
    g.setColour(colour_);
    g.setFont(font);
    g.drawSingleLineText(getButtonText(), juce::roundToInt(textX),
                         juce::roundToInt((box.getHeight() - lineH) / 2 + Fonts::cssBaseline(font, lineH)));

    const juce::Rectangle<float> external(std::round(textX + textW) + kExternalGap, (box.getHeight() - kExternal) / 2,
                                          kExternal, kExternal);
    Icons::draw(g, Icon::ExternalLink, external, colour_);
  }

  void resized() override { avatar.setBounds(0, (getHeight() - kLeading) / 2, kLeading, kLeading); }

private:
  std::optional<Icon> icon_;
  juce::Colour colour_;
};

// AccountSection
AccountSection::AccountSection(Services& services)
    : FieldRow("Account"),
      services_(services),
      profile_(std::make_unique<LinkRow>("Profile & Tones", std::nullopt, theme::kLinkBlue)),
      deleteAccount_(std::make_unique<LinkRow>("Delete Account", Icon::Trash2, theme::kBrandRed)),
      profileBox_(*profile_, LinkRow::kHeight),
      deleteAccountBox_(*deleteAccount_, LinkRow::kHeight) {
  setInlineLabel();
  content().setGap(form::kControlGap);
  content().add(profileBox_);
  if (kOffersDeletion) content().add(deleteAccountBox_);

  profile_->onClick = [this] {
    const auto user = services_.session.user();
    if (!user || user->username.isEmpty()) return;
    juce::URL(siteUrl("/" + juce::URL::addEscapeChars(user->username, false))).launchInDefaultBrowser();
  };
  deleteAccount_->onClick = [] { juce::URL(siteUrl(kDeleteAccountPath)).launchInDefaultBrowser(); };

  services_.session.addListener(this);
  sync();
}

AccountSection::~AccountSection() { services_.session.removeListener(this); }

void AccountSection::addTo(FormStack& page) {
  page_ = &page;
  page.add(*this);
  sync();
}

void AccountSection::sync() {
  const auto& session = services_.session;
  const auto user = session.user();
  const auto username = user ? user->username : juce::String();
  // The identity can land a moment after the sign-in. The profile link
  // needs the username; the delete page does not (the website knows who is
  // signed in), so that link never waits for it.
  const bool hasProfile = username.isNotEmpty();
  setHelp(hasProfile ? "Signed in as " + username + ". Your account is managed on tone3000.com."
                     : juce::String("Your account is managed on tone3000.com."));
  profile_->avatar.setImage(services_.images, user ? user->avatarUrl : juce::String());
  content().setShown(profileBox_, hasProfile);
  if (page_ != nullptr)
    page_->setShown(*this, session.authenticated() && canOpenLinks() && (hasProfile || kOffersDeletion));
}

}  // namespace t3k::ui
