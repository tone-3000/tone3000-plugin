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
#include "BinaryData.h"  // Contains embedded Web UI assets (HTML/CSS/JS)

class TONE3000Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit TONE3000Editor(TONE3000Processor&);
  ~TONE3000Editor() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  void parentHierarchyChanged() override;

  int getControlParameterIndex(juce::Component&) override {
    return controlParameterIndexReceiver.getControlParameterIndex();
  }

private:
  TONE3000Processor& processor;

  // One shared dark theme for JUCE-drawn surfaces (standalone settings dialog
  // etc.); installed as the default LookAndFeel in the editor constructor.
  juce::SharedResourcePointer<DarkLookAndFeel> darkLookAndFeel;

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
  juce::WebSliderRelay inputBalanceRelay{"inputBalance"};
  juce::WebSliderRelay outputBalanceRelay{"outputBalance"};
  juce::WebToggleButtonRelay spreadEnabledRelay{"spreadEnabled"};
  juce::WebSliderRelay spreadAmountRelay{"spreadAmount"};
  juce::WebSliderRelay spreadJitterRelay{"spreadJitter"};
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

  // Attachments
  juce::WebSliderParameterAttachment inputLevelWebAttachment{
      *processor.parameters.getParameter("inputLevel"), inputLevelRelay, nullptr};
  juce::WebSliderParameterAttachment outputLevelWebAttachment{
      *processor.parameters.getParameter("outputLevel"), outputLevelRelay, nullptr};
  juce::WebSliderParameterAttachment inputBalanceWebAttachment{
      *processor.parameters.getParameter("inputBalance"), inputBalanceRelay, nullptr};
  juce::WebSliderParameterAttachment outputBalanceWebAttachment{
      *processor.parameters.getParameter("outputBalance"), outputBalanceRelay, nullptr};
  juce::WebToggleButtonParameterAttachment spreadEnabledWebAttachment{
      *processor.parameters.getParameter("spreadEnabled"), spreadEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment spreadAmountWebAttachment{
      *processor.parameters.getParameter("spreadAmount"), spreadAmountRelay, nullptr};
  juce::WebSliderParameterAttachment spreadJitterWebAttachment{
      *processor.parameters.getParameter("spreadJitter"), spreadJitterRelay, nullptr};
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

  std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);
  juce::String getMimeForExtension(const juce::String& extension);

  friend juce::WebBrowserComponent::Options
  EditorWebViewSetup::buildMainWebViewOptions(TONE3000Editor*);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Editor)
};