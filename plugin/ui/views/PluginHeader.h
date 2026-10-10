// Full-width top bar (port of PluginHeader.tsx): logo, preset controls,
// stereo toggle, tuner, undo/redo and the account menu.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AccountMenu.h"
#include "PresetBar.h"
#include "StereoModeToggle.h"
#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class PluginHeader : public juce::Component, private ChainStore::Listener, private ToneSession::Listener {
public:
  static constexpr int kHeight = 64;

  explicit PluginHeader(Services& services);
  ~PluginHeader() override;

  PresetBar& presetBar() { return presetBar_; }

  void setTunerShown(bool shown);

  std::function<void(bool show)> onToggleTuner;
  std::function<void(bool stereo)> onStereoToggle;
  std::function<void()> onUndo, onRedo, onOpenSettings, onLogin, onLogout;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class LogoLink;

  void chainChanged(const ChainState& state) override;
  // Signed in / out, or the identity refreshed: the account pill's menu
  // and avatar follow the session.
  void sessionChanged() override;

  Services& services_;
  juce::String avatarUrl_;
  std::unique_ptr<LogoLink> logo_;
  PresetBar presetBar_;
  StereoModeToggle stereo_;
  IconButton tuner_;
  IconButton undo_{Icon::Undo2, 28};
  IconButton redo_{Icon::Redo2, 28};
  AccountMenu account_;
  bool tunerShown_ = false;
};

}  // namespace t3k::ui
