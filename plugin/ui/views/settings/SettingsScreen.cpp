#include "SettingsScreen.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/NoDefaultFocus.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/IconButton.h"
#include "widgets/form/FormStyle.h"
#include "widgets/form/FormText.h"

namespace t3k::ui {

// Header
// "Settings" (22px/600) with the 20px close glyph in a 28px box on the right.
class SettingsScreen::Header : public FormItem {
public:
  static constexpr float kTitlePx = 22;
  static constexpr int kClosePad = 4, kCloseGlyph = 20;
  static constexpr int kHeight = kCloseGlyph + 2 * kClosePad;

  Header() : close_(Icon::X, kHeight, kCloseGlyph) {
    close_.setName("Close settings");
    addAndMakeVisible(close_);
  }

  IconButton close_;

  float heightFor(float) const override { return kHeight; }

  void paint(juce::Graphics& g) override {
    const auto font = Fonts::sans(kTitlePx, true);
    const float lineH = static_cast<float>(Fonts::normalLineHeight(kTitlePx));
    paint::cssLine(g, "Settings", 0, std::floor((kHeight - lineH) / 2), lineH, static_cast<float>(getWidth()),
                   font, theme::kWhite);
  }

  void resized() override { close_.setBounds(getWidth() - kHeight, 0, kHeight, kHeight); }
};

// TabBar
// Full-width tab bar: equal-width tabs (optional 16px icon + 14px/600 label)
// with a 2px white underline on the active one over the list's 1px border.
class SettingsScreen::TabBar : public FormItem {
public:
  static constexpr float kLabelPx = 14;
  static constexpr int kPadY = 12, kIcon = 16, kIconGap = 8, kUnderline = 2;
  static constexpr int kHeight = 2 * kPadY + 16 /* line-height */ + kUnderline;

  class TabButton : public Clickable {
  public:
    TabButton(const juce::String& label, std::optional<Icon> icon) : Clickable(label), icon_(icon) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setSelected(bool selected) {
      selected_ = selected;
      repaint();
    }

    void paintButton(juce::Graphics& g, bool, bool) override {
      const auto fg = selected_ ? theme::kWhite : theme::kSubtle;
      const auto font = Fonts::sans(kLabelPx, true);
      const float lineH = static_cast<float>(Fonts::normalLineHeight(kLabelPx));
      const float textW = Fonts::width(font, getButtonText());
      const float contentW = textW + (icon_ ? kIcon + kIconGap : 0);
      float x = std::round((getWidth() - contentW) / 2);
      if (icon_) {
        Icons::draw(g, *icon_, juce::Rectangle<float>(x, kPadY + (lineH - kIcon) / 2, kIcon, kIcon), fg);
        x += kIcon + kIconGap;
      }
      paint::cssLine(g, getButtonText(), x, kPadY, lineH, textW + 2, font, fg);
      if (selected_) {
        g.setColour(theme::kWhite);
        g.fillRect(0, getHeight() - kUnderline, getWidth(), kUnderline);
      }
    }

  private:
    std::optional<Icon> icon_;
    bool selected_ = false;
  };

  TabBar() : system_("System Settings", Icon::Laptop), plugin_("Plugin Settings", std::nullopt) {
    addAndMakeVisible(system_);
    addAndMakeVisible(plugin_);
  }

  TabButton system_, plugin_;

  void setSelected(Tab tab) {
    system_.setSelected(tab == Tab::system);
    plugin_.setSelected(tab == Tab::plugin);
  }

  float heightFor(float) const override { return kHeight; }

  void paint(juce::Graphics& g) override {
    // The list's border sits 1px above the bottom (the buttons overlap it by
    // their -1px margin so the active underline covers it).
    g.setColour(form::kFieldBorder);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
  }

  void resized() override {
    const int half = getWidth() / 2;
    system_.setBounds(0, 0, half, getHeight());
    plugin_.setBounds(half, 0, getWidth() - half, getHeight());
  }
};

// SettingsScreen
SettingsScreen::SettingsScreen(Services& services, Tab initialTab)
    : standalone_(services.chain.state().standalone),
      tab_(standalone_ ? initialTab : Tab::plugin),
      header_(std::make_unique<Header>()),
      plugin_(services) {
  // Its own Tab cycle while it covers the window: the controls underneath
  // are still showing to JUCE, and the walk must not reach them.
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  header_->close_.onClick = [this] {
    if (onClose) onClose();
  };
  stack_.add(*header_);
  if (standalone_) {
    tabBar_ = std::make_unique<TabBar>();
    tabBar_->system_.onClick = [this] { setTab(Tab::system); };
    tabBar_->plugin_.onClick = [this] { setTab(Tab::plugin); };
    stack_.add(*tabBar_, kHeaderGap);
    system_ = std::make_unique<SystemSettingsPage>(services);
    stack_.add(*system_, kTabBarGap);
    stack_.add(plugin_, kTabBarGap);
  } else {
    stack_.add(plugin_, kHeaderGap);
  }
  content_.addAndMakeVisible(stack_);

  viewport_.setViewedComponent(&content_, false);
  addAndMakeVisible(viewport_);

  setTab(tab_);
  juce::Desktop::getInstance().addFocusChangeListener(this);
}

SettingsScreen::~SettingsScreen() { juce::Desktop::getInstance().removeFocusChangeListener(this); }

std::unique_ptr<juce::ComponentTraverser> SettingsScreen::createKeyboardFocusTraverser() {
  return std::make_unique<NoDefaultFocus>();
}

void SettingsScreen::globalFocusChanged(juce::Component* focused) {
  // Only focus inside the scrolling content: a dropdown's panel opens in the
  // overlay layer and takes focus there, and the page should stay put under
  // it.
  if (focused == nullptr || !content_.isParentOf(focused)) return;
  viewport_.reveal(*focused, kFocusMargin);
}

void SettingsScreen::setTab(Tab tab) {
  tab_ = standalone_ ? tab : Tab::plugin;
  if (tabBar_) tabBar_->setSelected(tab_);
  if (system_) stack_.setShown(*system_, tab_ == Tab::system);
  stack_.setShown(plugin_, tab_ == Tab::plugin);
}

void SettingsScreen::scrollToHeading(const juce::String& label, bool centre) {
  juce::Component* heading = nullptr;
  // Walks visible components only (the other tab's page is hidden), without
  // isShowing(), which needs a window and fails in the headless testbed.
  std::function<void(juce::Component&)> visit = [&](juce::Component& c) {
    if (heading != nullptr || !c.isVisible()) return;
    if (auto* l = dynamic_cast<FormLabel*>(&c); l != nullptr && l->text() == label) {
      heading = l;
      return;
    }
    for (auto* child : c.getChildren()) visit(*child);
  };
  visit(stack_);
  if (heading == nullptr) return;
  const int top = content_.getLocalPoint(heading, juce::Point<int>()).y;
  viewport_.setViewPosition(0, centre ? top - (viewport_.getHeight() - heading->getHeight()) / 2 : top - kPadTop);
}

juce::Rectangle<int> SettingsScreen::columnBounds() const {
  const int width = juce::jmin(kMaxWidth, getWidth());
  return {(getWidth() - width) / 2, 0, width, 0};
}

void SettingsScreen::layoutColumn() {
  const auto column = columnBounds().reduced(kPadX, 0);
  const int stackH = juce::roundToInt(stack_.heightFor(static_cast<float>(column.getWidth())));
  const int contentH = juce::jmax(getHeight(), kPadTop + stackH + kPadBottom);
  content_.setSize(getWidth(), contentH);
  stack_.setBounds(column.getX(), kPadTop, column.getWidth(), stackH);
}

void SettingsScreen::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void SettingsScreen::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutColumn();
}

}  // namespace t3k::ui
