// Settings takeover (Settings.tsx): a full-window black page with a 480px
// column (title + close, then the form) that scrolls as one. The standalone
// app adds a tab bar between System Settings (audio device + MIDI hardware;
// first, because setup is the main abandon risk) and Plugin Settings; hosted
// builds have one page and no tab bar. Mounted only while open, so page
// state resets for free each time.
//
// The page scrolls three ways, as the web page did: a wheel or trackpad, a
// touch drag (DragScroller), and the keyboard. Arrows, Page Up / Down,
// Home and End reach it either bubbling up from a focused control that
// does not take them (keyPressed) or from the root with nothing focused
// (scrollByKey, PluginRoot::FocusPolicy). The Tab walk is the page's own
// while it is up (a keyboard focus container: the chrome underneath is
// still showing to JUCE, and must stay out of reach) and the page follows
// the focus, so a stop below the fold scrolls into view. That keeps the
// page reachable from a pointer with no wheel: a mouse without one, or a
// touchscreen the OS presents as a mouse (Raspberry Pi OS's labwc does by
// default, mouseEmulation="yes"), where no drag pans.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "PluginSettingsPage.h"
#include "SystemSettingsPage.h"
#include "services/Services.h"
#include "widgets/DragScroller.h"
#include "widgets/form/FormItem.h"

namespace t3k::ui {

class SettingsScreen : public juce::Component, public FormHost, private juce::FocusChangeListener {
public:
  enum class Tab { system, plugin };

  static constexpr int kMaxWidth = 480;
  static constexpr int kPadTop = 28, kPadX = 24, kPadBottom = 40;
  static constexpr int kHeaderGap = 20, kTabBarGap = 28;
  // Kept around a control the keyboard focuses: a field's label and help
  // line above it, the next row's label below.
  static constexpr int kFocusMargin = 48;

  // `initialTab` only matters in the standalone app (hosted = plugin page).
  SettingsScreen(Services& services, Tab initialTab = Tab::system);
  ~SettingsScreen() override;

  std::function<void()> onClose;

  void setTab(Tab tab);

  // Scroll so the section headed `label` sits at the top of the page (under
  // the top padding), the way a reader lands on it; `centre` puts it mid-
  // viewport instead (scrollIntoViewIfNeeded).
  void scrollToHeading(const juce::String& label, bool centre = false);
  juce::Viewport& viewport() { return viewport_; }

  // The page's keyboard scrolling (DragScroller::scrollByKey); the root
  // sends the keys here when nothing has focus.
  bool scrollByKey(const juce::KeyPress& key) { return viewport_.scrollByKey(key); }

  void paint(juce::Graphics& g) override;
  void resized() override;
  // From a focused control on the page that passed the key on.
  bool keyPressed(const juce::KeyPress& key) override { return scrollByKey(key); }
  // A keyboard focus container (Tab wraps within the page) with no default
  // focus, as the root (see PluginRoot.h).
  std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;
  void itemHeightChanged() override { layoutColumn(); }

private:
  class Header;
  class TabBar;

  // Keyboard focus landing on the page brings its control into view.
  void globalFocusChanged(juce::Component* focused) override;
  void layoutColumn();
  // The column's left edge and width at the current window width.
  juce::Rectangle<int> columnBounds() const;

  bool standalone_;
  Tab tab_;
  DragScroller viewport_{DragScroller::Axis::vertical};
  juce::Component content_;
  FormStack stack_;
  std::unique_ptr<Header> header_;
  std::unique_ptr<TabBar> tabBar_;
  std::unique_ptr<SystemSettingsPage> system_;
  PluginSettingsPage plugin_;
};

}  // namespace t3k::ui
