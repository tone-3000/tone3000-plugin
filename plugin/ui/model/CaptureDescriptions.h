#pragma once

#include <juce_core/juce_core.h>
#include <map>

namespace t3k::ui {

// Editorial summaries of the captures shipped in the factory rigs. Settings
// describe the source hardware, not additional EQ to apply in the plugin.
// Keep catalog payloads intact; unknown catalog tones retain creator copy.
inline juce::String captureDescription(int id, const juce::String& original) {
  static const std::map<int, juce::String> summaries = {
      {31010, "Use: clean or driven bass.\nSettings: clean capture has controls at midpoint; driven capture adds gain.\nIncludes: SansAmp Bass Driver pedal only."},
      {84925, "Use: clean to heavily driven bass.\nSettings: gain 1-9; Bass/Mid/Treble 6; mid frequency 220 Hz; Hi on.\nIncludes: modified SVT head captures and a B7K variant. Add a cabinet IR."},
      {60274, "Use: bass cabinet sound after an amp.\nSettings: cap, cap edge, cone and cone edge mic positions; distant C414 at 12/24 inches. Preamp boosts at 60 Hz/12 kHz.\nIncludes: Bassman 2x15 CTS speaker/mic IRs; no amp."},
      {79751, "Use: Bogner clean or lead guitar tones.\nSettings: vary by model; exact knob values are in the creator's linked spreadsheet. Calibrated send: 18.995 dBu.\nIncludes: amp-head DI captures; Mesa/Marshall cabinets used as loads. Add a cabinet IR; mic'd rigs are in the linked pack."},
      {79862, "Use: Greenback cabinet sound after a guitar amp.\nSettings: 50/50 G12M/G12H cabinet blend; SM57 Enhanced variant. Exact mic position not supplied.\nIncludes: Marshall full-stack cabinet IR; no amp. Full commercial pack is separate."},
      {43900, "Use: console color, especially on bass DI.\nSettings: gain 30/40/50/60; EQ +2 dB at 60 Hz, +3 dB at 4.8 kHz, +2 dB at 12 kHz.\nIncludes: Neve 31102 preamp/EQ captures from left and right channels."},
      {30435, "Use: Dumble-style guitar tones.\nSettings: normal input; Ford = Rock on; Carlton = Rock/Mids on; All Switches = Rock/Mids/Bright on. Knob values vary by model. Calibration: +12.4 dBu.\nIncludes: ODS #102 clone amp captures; no cabinet. Add a cabinet IR."},
      {62037, "Use: beefy or cutting high-gain cabinet sound.\nSettings: mics halfway between speaker center and edge, about 2 inches from grille.\nIncludes: Marshall 1960/Fane cabinet IRs using SM57, Royer 122 and U67; no amp."},
      {78376, "Use: vocal preamp color and compression.\nSettings: EQ -1 dB at 220 Hz; 80 Hz low cut; ratio 4:1; about 5 dB gain reduction during capture.\nIncludes: Neve 1073 and 1176 Rev F chain."},
      {77706, "Use: vintage Bluesbreaker guitar crunch.\nSettings: Presence 2, Bass 2.5, Mid 8, Treble 7, Volume I 6.5, Volume II 5.\nIncludes: JTM45 combo and G12 Alnico 2x12 cabinet, captured with R121/R160/U87. No separate cabinet IR needed."},
      {79572, "Use: Neve preamp color.\nSettings: knob values not supplied; captured through Apollo Twin X.\nIncludes: AMS Neve 1073LB preamp capture."},
      {71073, "Use: long natural catacomb reverb.\nSettings: recorded on location; mono MA 1 or stereo LA-120 pair.\nIncludes: room IRs; no amp or cabinet."},
      {80592, "Use: clean guitar/pedal platform on Lo; Plexi-style drive on Hi.\nSettings: exact knob values not supplied.\nIncludes: Matchless Brave 2x12 combo, modified G12M25 speakers, SM545/R121 microphones and recording chain."},
      {69103, "Use: smooth optical compression.\nSettings: exact hardware values not supplied.\nIncludes: one free Teletronix LA-2A capture; full pack is separate."},
      {45026, "Use: modern metal rhythm and saturated leads.\nSettings: Modern channel; exact knob values not supplied.\nIncludes: 2025 Dual Rectifier preamp only; no power amp or cabinet IR."},
      {79857, "Use: oversized V30 cabinet sound for high-gain guitar.\nSettings: mic/position and processing vary by IR; see model name.\nIncludes: free Mesa Standard 4x12A cabinet IR samples; no amp. Full commercial pack is separate."},
      {86750, "Use: FET compression.\nSettings: two attack settings; fastest release; 4:1 and 8:1 ratios. Captured through Audient iD24.\nIncludes: Lindell Lin76 compressor captures."},
      {75764, "Use: low/mid-gain grit or boosting a crunchy amp.\nSettings: G number is gain clock position; volume adjusted for level.\nIncludes: Morning Glory V4 pedal captures, Blue/Red gain modes and a sweet-spot model; no amp/cab."},
      {75774, "Use: Vox Normal/Top Boost guitar tones.\nSettings: NRM/TB indicate channel; OA means off-axis; exact knob values not supplied.\nIncludes: free AC30 C2X demo captures. DI = amp only; mic-labelled models include Alnico Blue cabinet sound. Full pack is separate."},
      {65088, "Use: tape-style color.\nSettings: exact hardware values not supplied; recorded through API 548C.\nIncludes: Echo Fix EF X2 recording-chain capture."},
      {66547, "Use: vintage Vox cleans and breakup.\nSettings: exact knob values not supplied.\nIncludes: AC50 amp, two Alnico Blue speakers and Midax horn; SM57/Coles microphones through Neve/SSL recording chain."},
      {74633, "Use: concrete-tank ambience.\nSettings: stereo sine-sweep capture on a Zoom handheld.\nIncludes: Black Lagoon water-reservoir reverb IR; no amp/cab."},
      {79132, "Use: warm vintage guitar crunch.\nSettings: Treble 10, Bass 2, Volume 7; low input.\nIncludes: Magnatone M10A and 1x12 cabinet with R121/R160/U87 microphones. No separate cabinet IR needed."},
      {68162, "Use: Big Muff guitar fuzz.\nSettings: knob values not supplied; captured directly from interface without a reamp box.\nIncludes: Sovtek Tall Font Big Muff pedal only; no amp/cab."},
  };
  const auto found = summaries.find(id);
  return found == summaries.end() ? original : found->second;
}

}  // namespace t3k::ui
