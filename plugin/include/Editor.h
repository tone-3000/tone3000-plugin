#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#if JucePlugin_Build_Standalone && ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include "DarkLookAndFeel.h"
#include "EditorWebViewSetup.h"
#include "Processor.h"
#include "StandaloneAudioSettings.h"
#include "BinaryData.h"  // Contains embedded Web UI assets (HTML/CSS/JS)

class TONE3000Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit TONE3000Editor(TONE3000Processor&);
  ~TONE3000Editor() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  void parentHierarchyChanged() override;

  /** Extra height above the fixed plugin UI for chrome strips — the app banner
      (standalone only) and/or the hint bar. The webview reports the combined
      height whenever a strip appears/disappears so the window grows instead of
      squishing the core UI. Works in hosts too: setSize() becomes a host
      resize request via the plugin wrapper. */
  void setExtraContentHeight(int pixels);

  int getControlParameterIndex(juce::Component&) override {
    return controlParameterIndexReceiver.getControlParameterIndex();
  }

private:
  TONE3000Processor& processor;

  // Design-space size of the plugin UI. The window is this times the user's
  // scale factor: resizable (aspect-locked) from 1x up to kMaxScale. The web
  // UI observes its actual viewport width and applies a matching CSS zoom
  // (see useUiScale in the web UI), so native only manages the window box.
  // The window can additionally grow by the chrome-strip height (see
  // setExtraContentHeight).
  static constexpr int kWidth = 1024;
  static constexpr int kBaseHeight = 600; // base height of the plugin UI
  static constexpr double kMaxScale = 2.0;
  int extraContentHeight = 0;
  int totalHeight() const { return kBaseHeight + extraContentHeight; }

  // Scale is never stored — the window width is the source of truth, so a
  // host-driven resize and our own setSize agree by construction.
  double currentScale() const { return getWidth() / static_cast<double>(kWidth); }
  void applyScaledSize(double scale);
  void updateResizeConstraints();

  // One shared dark theme for JUCE-drawn surfaces (standalone settings dialog
  // etc.); installed as the default LookAndFeel in the editor constructor.
  juce::SharedResourcePointer<DarkLookAndFeel> darkLookAndFeel;

  // Bespoke audio settings bridge — only constructed in the standalone app
  // (nullptr in hosts, where the DAW owns devices and the System Settings tab
  // never renders). Device-manager changes are pushed to the webview as
  // `audioDeviceChanged` events.
  std::unique_ptr<StandaloneAudioSettings> audioSettings;

  //==============================================================================
  // WebView UI
  //==============================================================================
  std::unique_ptr<EditorWebViewSetup::GuardedWebView> mainWebView;  // Main Plugin UI

  // Guards against loading the main URL before the editor has a real
  // top-level NSWindow (Standalone races; harmless in AU/VST3 hosts).
  bool mainUrlLoaded = false;
  void loadMainUrlIfNeeded();

  // Chain-change push: a lightweight native timer watches the processor's
  // revision counter (an atomic read — far cheaper than the webview polling
  // across the bridge) and emits a `chainChanged` event when it moves. The
  // UI resyncs on the event and keeps only a slow safety-net poll.
  void timerCallback() override;
  juce::uint32 lastPushedRevision = 0;

  juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

  juce::WebSliderRelay inputLevelRelay{"inputLevel"};
  juce::WebSliderRelay outputLevelRelay{"outputLevel"};
  juce::WebSliderRelay outputBalanceRelay{"outputBalance"};
  juce::WebToggleButtonRelay spreadEnabledRelay{"spreadEnabled"};
  juce::WebSliderRelay spreadOffsetRelay{"spreadOffset"};
  juce::WebSliderRelay spreadWobbleRelay{"spreadWobble"};
  juce::WebToggleButtonRelay stereoOffsetEnabledRelay{"stereoOffsetEnabled"};
  juce::WebSliderRelay stereoOffsetTimeRelay{"stereoOffsetTime"};
  juce::WebSliderRelay chainPanLeftRelay{"chainPanLeft"};
  juce::WebSliderRelay chainPanRightRelay{"chainPanRight"};
  juce::WebToggleButtonRelay chainPanLinkedRelay{"chainPanLinked"};
  juce::WebSliderRelay bassRelay{"toneBass"};
  juce::WebSliderRelay midRelay{"toneMid"};
  juce::WebSliderRelay trebleRelay{"toneTreble"};
  juce::WebSliderRelay gateThresholdRelay{"gateThreshold"};
  juce::WebToggleButtonRelay gateEnabledRelay{"gateEnabled"};
  juce::WebToggleButtonRelay toneEqEnabledRelay{"toneEqEnabled"};
  juce::WebToggleButtonRelay calibrateInputRelay{"calibrateInput"};
  juce::WebSliderRelay inputCalibrationLevelRelay{"inputCalibrationLevel"};
  juce::WebToggleButtonRelay osEnabledRelay{"osEnabled"};
  juce::WebComboBoxRelay osFactorRelay{"osFactor"};

  // Attachments
  juce::WebSliderParameterAttachment inputLevelWebAttachment{
      *processor.parameters.getParameter("inputLevel"), inputLevelRelay, nullptr};
  juce::WebSliderParameterAttachment outputLevelWebAttachment{
      *processor.parameters.getParameter("outputLevel"), outputLevelRelay, nullptr};
  juce::WebSliderParameterAttachment outputBalanceWebAttachment{
      *processor.parameters.getParameter("outputBalance"), outputBalanceRelay, nullptr};
  juce::WebToggleButtonParameterAttachment spreadEnabledWebAttachment{
      *processor.parameters.getParameter("spreadEnabled"), spreadEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment spreadOffsetWebAttachment{
      *processor.parameters.getParameter("spreadOffset"), spreadOffsetRelay, nullptr};
  juce::WebSliderParameterAttachment spreadWobbleWebAttachment{
      *processor.parameters.getParameter("spreadWobble"), spreadWobbleRelay, nullptr};
  juce::WebToggleButtonParameterAttachment stereoOffsetEnabledWebAttachment{
      *processor.parameters.getParameter("stereoOffsetEnabled"), stereoOffsetEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment stereoOffsetTimeWebAttachment{
      *processor.parameters.getParameter("stereoOffsetTime"), stereoOffsetTimeRelay, nullptr};
  juce::WebSliderParameterAttachment chainPanLeftWebAttachment{
      *processor.parameters.getParameter("chainPanLeft"), chainPanLeftRelay, nullptr};
  juce::WebSliderParameterAttachment chainPanRightWebAttachment{
      *processor.parameters.getParameter("chainPanRight"), chainPanRightRelay, nullptr};
  juce::WebToggleButtonParameterAttachment chainPanLinkedWebAttachment{
      *processor.parameters.getParameter("chainPanLinked"), chainPanLinkedRelay, nullptr};
  juce::WebToggleButtonParameterAttachment gateEnabledWebAttachment{
      *processor.parameters.getParameter("gateEnabled"), gateEnabledRelay, nullptr};
  juce::WebToggleButtonParameterAttachment toneEqEnabledWebAttachment{
      *processor.parameters.getParameter("toneEqEnabled"), toneEqEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment bassWebAttachment{
      *processor.parameters.getParameter("toneBass"), bassRelay, nullptr};
  juce::WebSliderParameterAttachment midWebAttachment{*processor.parameters.getParameter("toneMid"),
                                                      midRelay, nullptr};
  juce::WebSliderParameterAttachment trebleWebAttachment{
      *processor.parameters.getParameter("toneTreble"), trebleRelay, nullptr};
  juce::WebSliderParameterAttachment gateThresholdWebAttachment{
      *processor.parameters.getParameter("gateThreshold"), gateThresholdRelay, nullptr};
  juce::WebToggleButtonParameterAttachment calibrateInputWebAttachment{
      *processor.parameters.getParameter("calibrateInput"), calibrateInputRelay, nullptr};
  juce::WebSliderParameterAttachment inputCalibrationLevelWebAttachment{
      *processor.parameters.getParameter("inputCalibrationLevel"), inputCalibrationLevelRelay, nullptr};
  juce::WebToggleButtonParameterAttachment osEnabledWebAttachment{
      *processor.parameters.getParameter("osEnabled"), osEnabledRelay, nullptr};
  juce::WebComboBoxParameterAttachment osFactorWebAttachment{
      *processor.parameters.getParameter("osFactor"), osFactorRelay, nullptr};

  std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);
  juce::String getMimeForExtension(const juce::String& extension);

  friend juce::WebBrowserComponent::Options
  EditorWebViewSetup::buildMainWebViewOptions(TONE3000Editor*);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Editor)
};