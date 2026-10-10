#include "AccountMenu.h"

#include "core/CustomIcons.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kMenuMinWidth = 190;
constexpr int kMenuPad = 8;
constexpr int kMenuRadius = 12;
constexpr int kMenuGap = 8;  // top: calc(100% + 8)
}  // namespace

// Menu
AccountMenu::Menu::Menu() {
  addAndMakeVisible(settings);
  addChildComponent(login);
  addChildComponent(logout);
  setSize(kMenuMinWidth, kBorder * 2 + kMenuPad * 2 + MenuRow::kHeight * 2);
}

void AccountMenu::Menu::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, kMenuRadius, theme::kSurfaceRaised);
  paint::border(g, box, kMenuRadius, theme::kBorder);
}

void AccountMenu::Menu::resized() {
  auto area = contentBounds().reduced(kMenuPad);
  settings.setBounds(area.removeFromTop(MenuRow::kHeight));
  const auto second = area.removeFromTop(MenuRow::kHeight);
  login.setBounds(second);
  logout.setBounds(second);
}

// Pill
AccountMenu::AccountMenu() : Clickable({}) {
  setHelpText(help::text(help::Key::account));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  addAndMakeVisible(avatar_);
  setSize(kWidth, kHeight);

  onClick = [this] {
    if (menu_.isOpen())
      menu_.close();
    else
      openMenu();
  };
  menu_.settings.onClick = [this] {
    menu_.close();
    if (onOpenSettings) onOpenSettings();
  };
  menu_.login.onClick = [this] {
    menu_.close();
    if (onLogin) onLogin();
  };
  menu_.logout.onClick = [this] {
    menu_.close();
    if (onLogout) onLogout();
  };
  setAuthenticated(false);
}

AccountMenu::~AccountMenu() { menu_.close(); }

void AccountMenu::setAuthenticated(bool authenticated) {
  menu_.login.setVisible(!authenticated);
  menu_.logout.setVisible(authenticated);
}

void AccountMenu::openMenu() { menu_.open(*this, Popover::Align::right, kMenuGap); }

void AccountMenu::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  paint::border(g, box, kHeight / 2.0f, theme::kBorder);
  // padding: 0 5px 0 12px (inside the 1px border), gap 10px.
  const auto glyph = juce::Rectangle<float>(18, 18).withCentre({1 + 12 + 9.0f, kHeight / 2.0f});
  Icons::draw(g, custom_icons::kHamburger, glyph, theme::kWhite);
}

void AccountMenu::resized() {
  avatar_.setBounds(1 + 12 + 18 + 10, (kHeight - 24) / 2, 24, 24);
}

}  // namespace t3k::ui
