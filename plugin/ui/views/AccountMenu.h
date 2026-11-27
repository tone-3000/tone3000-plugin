// Account pill for the header (port of AccountMenu.tsx): hamburger + avatar
// in a bordered rounded-full button opening a dark dropdown with Settings
// and Login/Logout.
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
  void setAvatar(ImageLoader& images, const juce::String& url) { avatar_.setImage(images, url); }
  void openMenu();

  std::function<void()> onOpenSettings, onLogin, onLogout;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  void resized() override;

private:
  class Menu : public Popover {
  public:
    Menu();
    void paint(juce::Graphics& g) override;
    void resized() override;
    MenuRow settings{"Settings", Icon::Settings};
    MenuRow login{"Login", Icon::LogIn};
    MenuRow logout{"Logout", Icon::LogOut};
  };

  Avatar avatar_;
  Menu menu_;
};

}  // namespace t3k::ui
