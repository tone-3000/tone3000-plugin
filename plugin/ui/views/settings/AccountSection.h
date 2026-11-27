// Account (the last section of both settings pages, signed in only): who the
// plugin is signed in as, with a link to their profile and tones on
// tone3000.com. The account lives on the website, so the plugin only links
// to it.
//
// The iOS app adds a Delete Account link under it. The App Store requires
// an app that lets people create an account to let them delete it too, from
// an option named for what it does that opens the page where the deletion
// happens (Guideline 5.1.1(v), see docs/ios.md), so the link goes to the
// website's delete page itself and not to the profile or the web settings.
// The other builds leave account management to the website.
//
// Both links are one row shape: a leading glyph (the user's avatar, the
// trash can), the label, and the external-link glyph after it, because the
// row leaves the plugin for the browser.
//
// Hidden when signed out, and in the iOS AUv3: an app extension cannot open
// a URL, so the links would do nothing there (the app carries them).
#pragma once

#include <memory>

#include "services/Services.h"
#include "widgets/form/FormRows.h"

namespace t3k::ui {

class AccountSection : public FieldRow, private ToneSession::Listener {
public:
  explicit AccountSection(Services& services);
  ~AccountSection() override;

  // Appends the section to a settings page, which it then shows and hides
  // itself in as the session changes.
  void addTo(FormStack& page);

private:
  class LinkRow;

  void sessionChanged() override { sync(); }
  void sync();

  Services& services_;
  FormStack* page_ = nullptr;
  std::unique_ptr<LinkRow> profile_, deleteAccount_;
  FormBox profileBox_, deleteAccountBox_;
};

}  // namespace t3k::ui
