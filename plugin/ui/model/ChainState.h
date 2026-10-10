// Typed chain state (port of chain.ts). Parsed once per revision
// from the backend's `getChainState` var; views read plain structs.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace t3k::ui {

enum class ChainSide { left, right };
juce::String toString(ChainSide side);
ChainSide chainSideFromString(const juce::String& s);

// How a stereo source feeds the chain (see Processor.h, InputMode). `stereo`
// is the natural routing for the chain mode (a mono chain sums L+R; stereo
// chains take one channel each); `dualMono` runs a mono chain once per
// channel (two independent voices); `left` / `right` fold one channel onto
// both. Wire strings: "stereo" / "dual" / "left" / "right".
enum class InputMode { stereo, left, right, dualMono };
juce::String toString(InputMode mode);
InputMode inputModeFromString(const juce::String& s);

enum class EqBandType { lowcut, lowshelf, bell, highshelf, highcut };
juce::String toString(EqBandType type);
EqBandType eqBandTypeFromString(const juce::String& s);

inline constexpr int kEqNumBands = 6;
inline constexpr double kEqMinFreqHz = 20;
inline constexpr double kEqMaxFreqHz = 20000;
inline constexpr double kEqMaxAbsGainDb = 15;
inline constexpr double kEqMinQ = 0.1;
inline constexpr double kEqMaxQ = 10;

struct EqBand {
  EqBandType type = EqBandType::bell;
  double freqHz = 1000;
  double gainDb = 0;
  double q = 1;

  // A bell/shelf at ~0 dB is inert; cuts shape by nature.
  bool isActive() const;
  juce::var toVar() const;
};

// The two type choices a band position allows (first: cut/shelf, last:
// shelf/cut, middle: bell only).
std::vector<EqBandType> eqBandTypeOptions(int index);

struct BlockEqParams {
  bool enabled = false;
  bool pre = false;
  std::vector<EqBand> bands;

  bool isFlat() const;
};

// NAM slimmable-size requests: 0 = lite, 1 = full; >= 0.5 displays as full.
inline constexpr double kSlimSizeLite = 0;
inline constexpr double kSlimSizeFull = 1;
inline bool isSlimSizeFull(double slimSize) { return slimSize >= 0.5; }

struct BlockParams {
  bool enabled = true;
  bool normalize = true;
  double slimSize = 0;
  double inputGain = 0.5;
  double outputGain = 0.5;
  double mix = 1;
  BlockEqParams eq;
};

struct ToneModelRef {
  int id = 0;
  juce::String name;
  juce::String modelUrl;  // local tones only
};

struct ToneUserRef {
  juce::String username;
  juce::String avatarUrl;
};

// Slim tone projection shipped by native (makeToneSummary in ProcessorChain.cpp).
struct ToneSummary {
  int id = 0;
  juce::String title;
  juce::String format;
  juce::String gear;
  bool local = false;
  juce::String image;  // first image only
  std::optional<ToneUserRef> user;
  juce::String publishedAt;
  std::vector<ToneModelRef> models;
  int modelsCount = 0;
  int a2ModelsCount = 0;
  int downloadsCount = 0;
  int favoritesCount = 0;
  std::optional<bool> isFavorite;
  juce::String url;

  // Models this plugin loads: A2 for NAM, otherwise models_count.
  int catalogModelCount() const;
  bool isNam() const { return format.equalsIgnoreCase("nam"); }
};

struct ChainItem {
  std::string blockId;
  bool isInsert = true;

  // Tone-block fields (unused for inserts).
  ToneSummary tone;
  int activeModelId = 0;
  bool loaded = false;
  bool loadFailed = false;
  bool modelLoading = false;
  bool irLong = false;
  std::optional<double> inputLevelDbu;
  std::optional<double> outputLevelDbu;
  BlockParams params;

  bool isTone() const { return !isInsert; }
};

struct PresetInfo {
  juce::String id;
  juce::String name;
  bool factory = false;
  juce::String category;  // "" = root ("Your Presets")
  bool favorite = false;
};

struct ActivePreset {
  juce::String id;
  juce::String name;
};

struct ChainBranch {
  ChainSide side = ChainSide::left;
  std::string afterBlockId;
};

struct ChainState {
  int revision = 0;
  bool canUndo = false;
  bool canRedo = false;
  bool canPasteBlock = false;
  bool atDefault = false;
  std::optional<ActivePreset> preset;
  bool stereoEnabled = false;
  ChainSide activeSide = ChainSide::left;
  bool stereoInput = false;
  bool stereoOutput = true;
  bool standalone = false;
  InputMode inputMode = InputMode::stereo;
  // Dual mono actually running (mode selected on a mono chain with a stereo
  // source and a stereo rig). The faceplate shows Balance and the stereo
  // output meter, and dims Spread, while this is set.
  bool dualMonoActive = false;
  double namSlimSizeDefault = 0;
  bool multiCore = true;
  double sampleRate = 48000;
  std::vector<ChainItem> chain;
  std::optional<std::vector<ChainItem>> chainRight;
  std::optional<ChainBranch> branch;

  // The `{ revision, unchanged: true }` short reply.
  static bool isUnchanged(const juce::var& response);
  static ChainState parse(const juce::var& v);

  const ChainItem* findBlock(const std::string& blockId) const;
  // Every tone block across both lanes, left lane first.
  std::vector<const ChainItem*> toneBlocks() const;
};

// Payload of `getMeterLevels` (dB, -60 floor).
struct MeterLevels {
  float input[2] = {-60, -60};
  float output[2] = {-60, -60};
  struct Block {
    std::string id;
    float in = -60, out = -60;
  };
  std::vector<Block> blocks;
  float cpu = 0;
  float correlation = 1;

  static MeterLevels parse(const juce::var& v);
};

struct TunerReading {
  double frequency = 0;
  double confidence = 0;
  double level = -60;

  static TunerReading parse(const juce::var& v);
};

// pollAutoBalance / pollAutoOffset.
struct AutoMeasureResult {
  enum class State { idle, listening, done, timeout };
  State state = State::idle;
  std::optional<double> matchedDb;
  std::optional<double> matchedMs;
  bool polarityFlipped = false;
  double progress = 0;

  static AutoMeasureResult parse(const juce::var& v);
};

std::vector<PresetInfo> parsePresetList(const juce::var& v);
/** The user's category names, alphabetical, from the same getPresetList payload. */
std::vector<juce::String> parsePresetCategories(const juce::var& v);

}  // namespace t3k::ui
