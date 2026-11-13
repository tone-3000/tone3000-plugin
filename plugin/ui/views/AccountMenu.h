// Account pill for the header (port of AccountMenu.tsx): hamburger + avatar
// in a bordered rounded-full button opening a dark dropdown with the signed-in
// profile (avatar + username), Settings and Login/Logout.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "widgets/Avatar.h"
#include "widgets/Clickable.h"
#include "widgets/MenuRow.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class AccountMenu : public Clickable {
public:
  static constexpr int kHeight = 40;
  static constexpr int kWidth = 71;  // 12 + 18 + 10 + 24 + 5 + 2px border

  AccountMenu();
  ~AccountMenu() override;

  void setAuthenticated(bool authenticated);
  void setUsername(const juce::String& username);
  void setAvatar(ImageLoader& images, const juce::String& url);
  void openMenu();

  std::function<void()> onOpenProfile, onOpenSettings, onLogin, onLogout;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  void resized() override;

private:
  // Avatar + username in the dropdown's row style.
  class ProfileRow : public Clickable {
  public:
    ProfileRow();
    void setUsername(const juce::String& username);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;
    Avatar avatar;

  private:
    juce::String username_;
  };

  class Menu : public Popover {
  public:
    Menu();
    void paint(juce::Graphics& g) override;
    void resized() override;
    // Height follows the visible rows.
    void relayout();
    ProfileRow profile;
    MenuRow settings{"Settings", Icon::Settings};
    MenuRow login{"Login", Icon::LogIn};
    MenuRow logout{"Logout", Icon::LogOut};
  };

  void updateProfileRow();

  Avatar avatar_;
  Menu menu_;
  bool authenticated_ = false;
  juce::String username_;
};

}  // namespace t3k::ui
