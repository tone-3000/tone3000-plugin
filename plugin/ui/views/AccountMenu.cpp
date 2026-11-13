#include "AccountMenu.h"

#include "core/CustomIcons.h"
#include "core/Fonts.h"
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

// Profile row
AccountMenu::ProfileRow::ProfileRow() : Clickable({}) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  addAndMakeVisible(avatar);
}

void AccountMenu::ProfileRow::setUsername(const juce::String& username) {
  username_ = username;
  setTitle(username);  // opens the profile, to a screen reader
  repaint();
}

void AccountMenu::ProfileRow::paintButton(juce::Graphics& g, bool highlighted, bool) {
  constexpr auto m = MenuRow::kDropdown;
  auto box = getLocalBounds();
  if (highlighted && isEnabled())
    paint::fill(g, box.toFloat(), 8.0f, juce::Colours::white.withAlpha(0.08f));
  auto content = box.reduced(m.padX, 0);
  content.removeFromLeft(m.icon + m.gap);
  paint::text(g, username_, content, Fonts::sans(m.fontPx), theme::kWhite);
}

void AccountMenu::ProfileRow::resized() {
  constexpr auto m = MenuRow::kDropdown;
  avatar.setBounds(m.padX, (getHeight() - m.icon) / 2, m.icon, m.icon);
}

// Menu
AccountMenu::Menu::Menu() {
  addChildComponent(profile);
  addAndMakeVisible(settings);
  addChildComponent(login);
  addChildComponent(logout);
  relayout();
}

void AccountMenu::Menu::relayout() {
  const int rows = profile.isVisible() ? 3 : 2;
  setSize(kMenuMinWidth, kBorder * 2 + kMenuPad * 2 + MenuRow::kHeight * rows);
  resized();
}

void AccountMenu::Menu::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, kMenuRadius, theme::kSurfaceRaised);
  paint::border(g, box, kMenuRadius, theme::kBorder);
}

void AccountMenu::Menu::resized() {
  auto area = contentBounds().reduced(kMenuPad);
  if (profile.isVisible()) profile.setBounds(area.removeFromTop(MenuRow::kHeight));
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
  menu_.profile.onClick = [this] {
    menu_.close();
    if (onOpenProfile) onOpenProfile();
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
  authenticated_ = authenticated;
  menu_.login.setVisible(!authenticated);
  menu_.logout.setVisible(authenticated);
  updateProfileRow();
}

void AccountMenu::setUsername(const juce::String& username) {
  username_ = username;
  menu_.profile.setUsername(username);
  updateProfileRow();
}

void AccountMenu::setAvatar(ImageLoader& images, const juce::String& url) {
  avatar_.setImage(images, url);
  menu_.profile.avatar.setImage(images, url);
}

void AccountMenu::updateProfileRow() {
  const bool show = authenticated_ && username_.isNotEmpty();
  if (show == menu_.profile.isVisible()) return;
  menu_.profile.setVisible(show);
  menu_.relayout();
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
