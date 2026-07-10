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

#include "EditorWebViewSetup.h"
#include "Processor.h"
#include "BinaryData.h"  // Contains embedded Web UI assets (HTML/CSS/JS)

class TONE3000Editor : public juce::AudioProcessorEditor {
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

  //==============================================================================
  // WebView UI
  //==============================================================================
  std::unique_ptr<juce::WebBrowserComponent> mainWebView;   // Main Plugin UI

  // Guards against loading the main URL before the editor has a real
  // top-level NSWindow (Standalone races; harmless in AU/VST3 hosts).
  bool mainUrlLoaded = false;
  void loadMainUrlIfNeeded();

  juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

  juce::WebSliderRelay inputLevelRelay{"inputLevel"};
  juce::WebSliderRelay outputLevelRelay{"outputLevel"};
  juce::WebSliderRelay inputBalanceRelay{"inputBalance"};
  juce::WebSliderRelay outputBalanceRelay{"outputBalance"};
  juce::WebToggleButtonRelay doublerEnabledRelay{"doublerEnabled"};
  juce::WebSliderRelay doublerSpreadRelay{"doublerSpread"};
  juce::WebSliderRelay doublerJitterRelay{"doublerJitter"};
  juce::WebSliderRelay chainPanLeftRelay{"chainPanLeft"};
  juce::WebSliderRelay chainPanRightRelay{"chainPanRight"};
  juce::WebToggleButtonRelay chainPanLinkedRelay{"chainPanLinked"};
  juce::WebToggleButtonRelay stereoOffsetEnabledRelay{"stereoOffsetEnabled"};
  juce::WebSliderRelay stereoOffsetSpreadRelay{"stereoOffsetSpread"};
  juce::WebSliderRelay stereoOffsetJitterRelay{"stereoOffsetJitter"};
  juce::WebSliderRelay bassRelay{"toneBass"};
  juce::WebSliderRelay midRelay{"toneMid"};
  juce::WebSliderRelay trebleRelay{"toneTreble"};
  juce::WebSliderRelay gateThresholdRelay{"gateThreshold"};
  juce::WebToggleButtonRelay gateEnabledRelay{"gateEnabled"};
  juce::WebToggleButtonRelay toneEqEnabledRelay{"toneEqEnabled"};
  juce::WebToggleButtonRelay normalizeRelay{"normalize"};
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
  juce::WebToggleButtonParameterAttachment doublerEnabledWebAttachment{
      *processor.parameters.getParameter("doublerEnabled"), doublerEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment doublerSpreadWebAttachment{
      *processor.parameters.getParameter("doublerSpread"), doublerSpreadRelay, nullptr};
  juce::WebSliderParameterAttachment doublerJitterWebAttachment{
      *processor.parameters.getParameter("doublerJitter"), doublerJitterRelay, nullptr};
  juce::WebSliderParameterAttachment chainPanLeftWebAttachment{
      *processor.parameters.getParameter("chainPanLeft"), chainPanLeftRelay, nullptr};
  juce::WebSliderParameterAttachment chainPanRightWebAttachment{
      *processor.parameters.getParameter("chainPanRight"), chainPanRightRelay, nullptr};
  juce::WebToggleButtonParameterAttachment chainPanLinkedWebAttachment{
      *processor.parameters.getParameter("chainPanLinked"), chainPanLinkedRelay, nullptr};
  juce::WebToggleButtonParameterAttachment stereoOffsetEnabledWebAttachment{
      *processor.parameters.getParameter("stereoOffsetEnabled"), stereoOffsetEnabledRelay, nullptr};
  juce::WebSliderParameterAttachment stereoOffsetSpreadWebAttachment{
      *processor.parameters.getParameter("stereoOffsetSpread"), stereoOffsetSpreadRelay, nullptr};
  juce::WebSliderParameterAttachment stereoOffsetJitterWebAttachment{
      *processor.parameters.getParameter("stereoOffsetJitter"), stereoOffsetJitterRelay, nullptr};
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
  juce::WebToggleButtonParameterAttachment normalizeWebAttachment{
      *processor.parameters.getParameter("normalize"), normalizeRelay, nullptr};
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