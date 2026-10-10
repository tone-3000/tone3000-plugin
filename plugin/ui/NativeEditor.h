// The plugin's editor: owns the backend, the services and the PluginRoot,
// and manages the window box: the 1024 x
// 578(+chrome) design box times an aspect-locked user scale between 1x and
// kMaxScale, persisted on the processor, grown by the chrome strips' height
// on request.
//
// The root is laid out in design space and scaled with one AffineTransform:
// top-anchored and horizontally centred in whatever box the host actually
// gives us, so a refused resize letterboxes instead of squishing.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DarkLookAndFeel.h"
#include "backend/ProcessorBackend.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "services/HttpClient.h"
#include "services/Services.h"
#include "services/Tone3000Session.h"
#include "views/PluginRoot.h"
#include "widgets/KnobFace.h"

namespace t3k::ui {

class NativeEditor : public juce::AudioProcessorEditor, public Shell, private juce::Timer {
public:
  explicit NativeEditor(TONE3000Processor& owner);
  ~NativeEditor() override;

  void setExtraContentHeight(int total, int persistent) override;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;
  void parentHierarchyChanged() override;
  bool keyPressed(const juce::KeyPress& key) override;

  // JUCE 9 leaves Windows multi-touch off unless the editor opts in. Without
  // it the HWND is never registered for touch and WM_POINTER falls through to
  // DefWindowProc, which runs Windows' own tap/press-and-hold recogniser and
  // only emits emulated mouse messages once the finger lifts: our hold timers
  // (knob advanced popover, tile menus) never get a mouseDown to start from.
  // Opting in delivers touch as it happens. The cost is WM_GESTURE going away
  // (no mouseMagnify), which nothing here uses.
  bool usesWindowsMultiTouch() const override { return true; }

private:
  static constexpr int kMaxExtraHeight = 160;  // banner (~44) + hint bar (36) + headroom
  // How long a taller design box keeps the current scale while the host
  // resize is pending, before shrinking to fit.
  static constexpr int kShrinkGraceMs = 450;

  int designHeight() const { return design::kHeight + extraContentHeight_; }
  double currentScale() const { return getWidth() / static_cast<double>(design::kWidth); }
  double maxStartScale() const;
  void applyScaledSize(double scale);
  void updateResizeConstraints();
  void fitRoot();
  void timerCallback() override { fitRoot(); }

  // The per-machine preferences file. Every plugin instance in the process
  // reads and writes the same file, and PropertiesFile saves its whole
  // in-memory copy, so two instances each holding their own would clobber
  // each other's writes: one shared instance per process. Across processes
  // (each DAW, the standalone) UiPrefs merges under the lock and saves each
  // write itself, so the file never autosaves.
  struct PrefsFile {
    juce::InterProcessLock lock{"TONE3000.ui-preferences"};
    juce::PropertiesFile file{[this] {
      auto options = TONE3000Processor::uiPreferencesOptions();
      options.processLock = &lock;
      options.millisecondsBeforeSaving = -1;
      return options;
    }()};
  };

  // Release-level bring-up trace (GitHub issue #132: a host crashed on
  // editor open with nothing in the log after the processor's restore line,
  // so the crash could not be placed). One "[Editor]" line per stage, in
  // construction order: entry (with OS / host / display scale), fonts
  // resolved, peer attached (with renderer), first resized, first paint.
  // A truncated sequence in a user's log bisects the crash for free.
  // Declared first so the entry line lands before any other member runs.
  struct Trace {
    explicit Trace(const TONE3000Processor& processor);
  } trace_;
  // Forces the shared typefaces to resolve (into fontsHold_'s cache) and
  // logs it, before PluginRoot's text layout does the same silently.
  // Declared right before the root.
  struct FontsReady {
    FontsReady();
  };
  void logPeerAttached();
  juce::ComponentPeer* loggedPeer_ = nullptr;
  bool loggedFirstResize_ = false;
  bool loggedFirstPaint_ = false;

  TONE3000Processor& processor_;
  // Keep the UI's rasterised resources alive for as long as any editor
  // exists, and no longer: on Windows they are Direct2D/DirectWrite-backed
  // and must be gone before the host unloads the module (see KnobFaceCache
  // for the DllMain deadlock this prevents). Declared before every member
  // that draws or lays out text.
  Fonts::Hold fontsHold_;
  KnobFaceCacheHold knobFacesHold_;
  // One shared dark theme for JUCE-drawn surfaces (standalone dialogs).
  juce::SharedResourcePointer<DarkLookAndFeel> darkLookAndFeel_;
  juce::SharedResourcePointer<PrefsFile> prefsFile_;
  // Declared before the root: PluginRoot reports its chrome height from its
  // constructor, which lands in setExtraContentHeight.
  int extraContentHeight_ = 0;
  // Guards resized() against persisting a size we didn't choose (see
  // parentHierarchyChanged).
  bool restoringSize_ = false;
  juce::int64 shrinkAllowedAtMs_ = 0;
  ProcessorBackend backend_;
  // Owned here rather than in Services: the session persists its tokens in
  // the prefs, and Services needs the session.
  UiPrefs prefs_;
  HttpClient http_;
  Tone3000Session session_;
  Services services_;
  FontsReady fontsReady_;
  PluginRoot root_;
};

}  // namespace t3k::ui
