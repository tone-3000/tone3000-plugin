#include "KemperRouting.h"

#include <gtest/gtest.h>
#include <set>
#include <utility>
#include <vector>

namespace {
using namespace t3k::capture::kemper;
using P = IoParameter;
using K = IoParameterSpec::Kind;

std::vector<int> bytes(const Messages& messages) {
  std::vector<int> result;
  for (const auto& message : messages)
    for (int i = 0; i < message.getRawDataSize(); ++i)
      result.push_back(message.getRawData()[i]);
  return result;
}

TEST(KemperRoutingTest, ManufacturerAddressInventoryHasNoDuplicateIdsOrAddresses) {
  std::set<int> ids;
  std::set<std::pair<int, int>> addresses;
  ASSERT_EQ(ioParameters().size(), 33u);
  for (const auto& spec : ioParameters()) {
    EXPECT_TRUE(ids.insert(static_cast<int>(spec.id)).second);
    EXPECT_TRUE(addresses.insert({spec.page, spec.index}).second);
    EXPECT_FALSE(spec.section.isEmpty());
    EXPECT_FALSE(spec.label.isEmpty());
    EXPECT_GE(spec.page, 0);
    EXPECT_LE(spec.page, 127);
    EXPECT_GE(spec.index, 0);
    EXPECT_LE(spec.index, 127);
    EXPECT_EQ(ioParameterSpec(spec.id), &spec);
  }
  EXPECT_EQ(ioParameterSpec(static_cast<P>(999)), nullptr);
}

TEST(KemperRoutingTest, InputAndOutputAddressesMatchMidi13Table) {
  const std::vector<std::pair<P, std::pair<int,int>>> expected{
    {P::inputNoiseGate,{9,3}}, {P::cleanSens,{9,4}}, {P::distortionSens,{9,5}},
    {P::mainVolume,{127,0}}, {P::headphoneVolume,{127,1}}, {P::monitorVolume,{127,2}},
    {P::directVolume,{127,3}}, {P::spdifVolume,{127,4}}, {P::monitorCabOff,{127,8}},
    {P::mainBass,{127,12}}, {P::mainMiddle,{127,13}}, {P::mainTreble,{127,14}},
    {P::mainPresence,{127,15}}, {P::outputLowCut,{127,16}}, {P::monitorBass,{127,17}},
    {P::monitorMiddle,{127,18}}, {P::monitorTreble,{127,19}}, {P::monitorPresence,{127,20}},
    {P::outputHighCut,{127,21}}, {P::auxToMain,{127,32}}, {P::auxToMonitor,{127,33}},
    {P::auxToHeadphones,{127,34}}, {P::spaceIntensity,{127,36}}, {P::spaceRouting,{127,37}},
    {P::koneEnabled,{127,38}}, {P::koneBassBoost,{127,39}}, {P::koneImprint,{127,40}},
    {P::koneDirectivity,{127,41}}, {P::koneSweetening,{127,42}}, {P::inputSource,{127,44}},
    {P::pureCabinetEnabled,{127,50}}, {P::pureCabinetLevel,{127,51}}, {P::auxMono,{127,59}}
  };
  ASSERT_EQ(expected.size(), ioParameters().size());
  for (const auto& [id, address] : expected) {
    const auto* spec = ioParameterSpec(id);
    ASSERT_NE(spec, nullptr);
    EXPECT_EQ(spec->page, address.first);
    EXPECT_EQ(spec->index, address.second);
  }
}

TEST(KemperRoutingTest, ContinuousControlsPreserveRawPositionsAndRejectOverflow) {
  for (const auto& spec : ioParameters()) {
    if (spec.kind != K::continuous) continue;
    for (int value : {0, 1, 8192, 16383})
      EXPECT_EQ(bytes(encodeIoParameter(16, spec.id, value)),
                bytes(encodeNrpn(16, spec.page, spec.index, value)));
    EXPECT_TRUE(encodeIoParameter(1, spec.id, -1).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, 16384).empty());
  }
}

TEST(KemperRoutingTest, ToggleControlsUseIntegerOneInLsb) {
  for (const auto& spec : ioParameters()) {
    if (spec.kind != K::toggle) continue;
    EXPECT_EQ(bytes(encodeIoParameter(1, spec.id, 1)),
              bytes(encodeNrpn(1, spec.page, spec.index, 1)));
    EXPECT_EQ(bytes(encodeIoParameter(1, spec.id, 0)),
              bytes(encodeNrpn(1, spec.page, spec.index, 0)));
    EXPECT_TRUE(encodeIoParameter(1, spec.id, 2).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, 127).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, 16383).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, -1).empty());
  }
}

TEST(KemperRoutingTest, ChoiceArityPreventsNormalizedValuesChangingWrongOption) {
  for (const auto& spec : ioParameters()) {
    if (spec.kind != K::choice) continue;
    ASSERT_GT(spec.choices.size(), 0);
    for (int value = 0; value < spec.choices.size(); ++value)
      EXPECT_EQ(bytes(encodeIoParameter(2, spec.id, value)),
                bytes(encodeNrpn(2, spec.page, spec.index, value)));
    EXPECT_TRUE(encodeIoParameter(1, spec.id, -1).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, spec.choices.size()).empty());
    EXPECT_TRUE(encodeIoParameter(1, spec.id, 8192).empty());
  }
  ASSERT_NE(ioParameterSpec(P::inputSource), nullptr);
  EXPECT_EQ(ioParameterSpec(P::inputSource)->choices.size(), 4);
  EXPECT_EQ(ioParameterSpec(P::koneImprint)->choices.size(), 19);
  EXPECT_EQ(ioParameterSpec(P::spaceRouting)->choices.size(), 2);
}

TEST(KemperRoutingTest, ModelMetadataDoesNotOfferHeadRackInputsOnPlayerOrStage) {
  const auto* input = ioParameterSpec(P::inputSource);
  ASSERT_NE(input, nullptr);
  EXPECT_TRUE(input->headRackOnly);
  EXPECT_FALSE(input->playerSupported);
  EXPECT_FALSE(ioParameterSpec(P::distortionSens)->playerSupported);
  EXPECT_FALSE(ioParameterSpec(P::spdifVolume)->playerSupported);
  EXPECT_FALSE(ioParameterSpec(P::directVolume)->playerSupported);
  EXPECT_TRUE(ioParameterSpec(P::cleanSens)->playerSupported);
  EXPECT_TRUE(ioParameterSpec(P::monitorCabOff)->playerSupported);
}

TEST(KemperRoutingTest, InvalidIdsChannelsAndDisconnectedSendNeverGenerateWrites) {
  EXPECT_TRUE(encodeIoParameter(1, static_cast<P>(999), 0).empty());
  EXPECT_TRUE(encodeIoParameter(0, P::mainVolume, 0).empty());
  EXPECT_TRUE(encodeIoParameter(17, P::mainVolume, 0).empty());
  Controller controller;
  EXPECT_TRUE(controller.setIoParameter(P::mainVolume, 8192).failed());
  EXPECT_TRUE(controller.setIoParameter(P::monitorCabOff, 16383).failed());
  EXPECT_FALSE(controller.isOpen());
}

TEST(KemperRoutingTest, UnsupportedHardwareFieldsHaveManualUiDestinationsOnly) {
  ASSERT_GE(manualIoSettings().size(), 20u);
  for (const auto& field : manualIoSettings()) {
    EXPECT_FALSE(field.section.isEmpty());
    EXPECT_FALSE(field.label.isEmpty());
    EXPECT_FALSE(field.destination.isEmpty());
    EXPECT_FALSE(field.notes.isEmpty());
  }
  bool source = false, reamp = false, usb = false;
  for (const auto& field : manualIoSettings()) {
    source |= field.label == "Output Source";
    reamp |= field.label == "Reamp Sens";
    usb |= field.label == "USB Reamping";
  }
  EXPECT_TRUE(source);
  EXPECT_TRUE(reamp);
  EXPECT_TRUE(usb);
}

TEST(KemperRoutingTest, ReadRequestMatchesPublishedSysexExampleWithoutAnyWriteFunction) {
  EXPECT_EQ(bytes(encodeRequestParameter(52, 0, 2)),
            (std::vector<int>{0xf0,0,0x20,0x33,2,0x7f,0x41,0,52,0,0xf7}));
  EXPECT_EQ(bytes(encodeRequestParameter(127, 4)),
            (std::vector<int>{0xf0,0,0x20,0x33,0,0x7f,0x41,0,127,4,0xf7}));
  EXPECT_TRUE(encodeRequestParameter(-1, 0).empty());
  EXPECT_TRUE(encodeRequestParameter(0, 128).empty());
  EXPECT_TRUE(encodeRequestParameter(0, 0, 1).empty());
  EXPECT_TRUE(encodeRequestParameter(0, 0, 0, 128).empty());
}

juce::MidiMessage responseMessage(std::vector<juce::uint8> data) {
  return juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));
}

TEST(KemperRoutingTest, ParsesOnlyCompleteKemperNumericRepliesWithOptionalMorphValue) {
  const auto basic = responseMessage({0,0x20,0x33,0,0x7f,1,0,127,8,0,1});
  const auto value = parseParameterResponse(basic);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->page, 127);
  EXPECT_EQ(value->index, 8);
  EXPECT_EQ(value->value14, 1);
  EXPECT_EQ(value->productType, 0);
  EXPECT_FALSE(value->morphedValue14.has_value());
  const auto morph = parseParameterResponse(responseMessage({0,0x20,0x33,2,0x7f,1,0,50,4,64,0,127,127}));
  ASSERT_TRUE(morph.has_value());
  EXPECT_EQ(morph->value14, 8192);
  ASSERT_TRUE(morph->morphedValue14.has_value());
  EXPECT_EQ(*morph->morphedValue14, 16383);
  EXPECT_EQ(morph->productType, 2);
}

TEST(KemperRoutingTest, IgnoresForeignMalformedRequestAndWrongInstanceReplies) {
  EXPECT_FALSE(parseParameterResponse(juce::MidiMessage::controllerEvent(1, 1, 0)));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x21,0x33,0,0x7f,1,0,127,8,0,1})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,3,0x7f,1,0,127,8,0,1})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,0,0x7f,0x41,0,127,8,0,1})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,0,0x7f,1,1,127,8,0,1})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,0,0x7f,1,0,127,8,0})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,0,0x7f,1,0,127,8,0,1,0})));
  EXPECT_FALSE(parseParameterResponse(responseMessage({0,0x20,0x33,0,0x7f,1,0,127,8,0,128})));
}

TEST(KemperRoutingTest, ReadbackCacheStartsEmptyAndRequestsRequireExplicitInput) {
  Controller controller;
  EXPECT_FALSE(controller.hasInput());
  EXPECT_FALSE(controller.cachedParameter(127, 0));
  EXPECT_FALSE(controller.cachedParameter(-1, 0));
  EXPECT_TRUE(controller.requestParameter(127, 0).failed());
  EXPECT_TRUE(controller.enableInput({}).failed());
  controller.disableInput();
  EXPECT_FALSE(controller.hasInput());
  EXPECT_FALSE(controller.cachedParameter(127, 0));
}
} // namespace
