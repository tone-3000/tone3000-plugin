#include "RoutingPlan.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>

namespace t3k::routing {
const char* sourceName(Source s) {
  switch (s) {
    case Source::off: return "Off";
    case Source::di: return "DI (raw digital input)";
    case Source::stack: return "Stack";
    case Source::modLeft: return "Mod Left";
    case Source::modRight: return "Mod Right";
    case Source::modMono: return "Mod Mono";
    case Source::modStereo: return "Mod Stereo";
    case Source::masterLeft: return "Master Left";
    case Source::masterRight: return "Master Right";
    case Source::masterMono: return "Master Mono";
    case Source::masterStereo: return "Master Stereo";
    case Source::wetFx: return "DLY/REV Wet";
    case Source::diStack: return "DI / Stack";
    case Source::diModLeft: return "DI / Mod Left";
    case Source::diModMono: return "DI / Mod Mono";
    case Source::diMasterLeft: return "DI / Master Left";
    case Source::diMasterMono: return "DI / Master Mono";
    case Source::returns: return "Return Inputs";
  }
  return "Invalid source";
}
const char* portName(Port p) {
  constexpr const char* names[]{"Main", "Monitor", "Direct", "Phones", "S/PDIF", "USB"};
  return names[static_cast<size_t>(p)];
}
const char* inputSourceName(InputSource s) {
  constexpr const char* names[]{"Instrument Input", "Alternative Input", "Return Input Reamp", "Digital Input Reamp"};
  return names[static_cast<size_t>(s)];
}
const std::vector<Source>& sources() {
  static const std::vector<Source> values{Source::off, Source::di, Source::stack,
    Source::modLeft, Source::modRight, Source::modMono, Source::modStereo,
    Source::masterLeft, Source::masterRight, Source::masterMono, Source::masterStereo,
    Source::wetFx, Source::diStack, Source::diModLeft, Source::diModMono,
    Source::diMasterLeft, Source::diMasterMono, Source::returns};
  return values;
}
bool stereoSource(Source s) {
  return s == Source::modStereo || s == Source::masterStereo || s == Source::wetFx ||
         s >= Source::diStack;
}
Config::Config() {
  outputs[0].source = Source::masterStereo;
  outputs[0].linkMaster = true;
  outputs[2].stereo = false;
}
namespace {
juce::var ints(const std::array<int, 2>& a) { return juce::Array<juce::var>{a[0], a[1]}; }
juce::var ids(const std::array<std::string, 2>& a) {
  return juce::Array<juce::var>{juce::String(a[0]), juce::String(a[1])};
}
}
juce::var toVar(const Config& c) {
  auto* v = new juce::DynamicObject;
  v->setProperty("schema", 1);
  v->setProperty("enabled", c.enabled);
  v->setProperty("inputDevice", c.inputDevice);
  v->setProperty("outputDevice", c.outputDevice);
  v->setProperty("inputSource", static_cast<int>(c.inputSource));
  juce::Array<juce::var> in;
  for (int ch : c.inputChannels) in.add(ch);
  v->setProperty("inputChannels", in);
  v->setProperty("stereoInputRight", c.stereoInputRight);
  v->setProperty("diInput", c.diInput);
  v->setProperty("returnInputs", ints(c.returnInputs));
  v->setProperty("auxInputs", ints(c.auxInputs));
  v->setProperty("inputGainDb", c.inputGainDb);
  v->setProperty("reampSensitivityDb", c.reampSensitivityDb);
  v->setProperty("masterVolumeDb", c.masterVolumeDb);
  juce::Array<juce::var> aux;
  for (float x : c.auxMix) aux.add(x);
  v->setProperty("auxMix", aux);
  v->setProperty("stackEnd", ids(c.stackEnd));
  v->setProperty("modEnd", ids(c.modEnd));
  v->setProperty("wetStart", ids(c.wetStart));
  v->setProperty("cabinet", ids(c.cabinet));
  juce::Array<juce::var> out;
  for (const auto& o : c.outputs) {
    auto* p = new juce::DynamicObject;
    p->setProperty("source", static_cast<int>(o.source));
    p->setProperty("left", o.left); p->setProperty("right", o.right);
    p->setProperty("volumeDb", o.volumeDb);
    p->setProperty("linkMaster", o.linkMaster); p->setProperty("pad12", o.pad12);
    p->setProperty("stereo", o.stereo); p->setProperty("cabinetOff", o.cabinetOff);
    p->setProperty("bassDb", o.bassDb); p->setProperty("middleDb", o.middleDb);
    p->setProperty("trebleDb", o.trebleDb); p->setProperty("presenceDb", o.presenceDb);
    p->setProperty("lowCutHz", o.lowCutHz); p->setProperty("highCutHz", o.highCutHz);
    out.add(juce::var(p));
  }
  v->setProperty("outputs", out);
  return juce::var(v);
}
juce::Result fromVar(const juce::var& v, Config& result) {
  const auto fail = [] { return juce::Result::fail("The saved routing configuration is invalid or from an unsupported version."); };
  const auto number = [](const juce::var& x) {
    return (x.isInt() || x.isInt64() || x.isDouble()) && std::isfinite(static_cast<double>(x));
  };
  const auto integer = [&](const juce::var& x) {
    if (!number(x)) return false;
    const double d = x;
    return std::floor(d) == d && d >= std::numeric_limits<int>::min() && d <= std::numeric_limits<int>::max();
  };
  if (!v.isObject() || !integer(v["schema"]) || static_cast<int>(v["schema"]) != 1 ||
      !v["enabled"].isBool() || !v["inputDevice"].isString() || !v["outputDevice"].isString()) return fail();
  Config c;
  c.enabled = v["enabled"]; c.inputDevice = v["inputDevice"].toString(); c.outputDevice = v["outputDevice"].toString();
  if (!integer(v["inputSource"])) return fail();
  const int src = v["inputSource"];
  if (src < 0 || src > 3) return fail();
  c.inputSource = static_cast<InputSource>(src);
  auto read = [&](const char* key, auto& a) {
    auto* values = v[key].getArray();
    if (!values || values->size() != static_cast<int>(a.size())) return false;
    using Value = typename std::decay_t<decltype(a)>::value_type;
    for (size_t i = 0; i < a.size(); ++i) {
      const auto& value = (*values)[static_cast<int>(i)];
      if constexpr (std::is_integral_v<Value>) { if (!integer(value)) return false; }
      else { if (!number(value)) return false; }
      a[i] = static_cast<Value>(value);
    }
    return true;
  };
  if (!read("inputChannels", c.inputChannels) || !read("returnInputs", c.returnInputs) ||
      !read("auxInputs", c.auxInputs) || !read("auxMix", c.auxMix)) return fail();
  auto readIds = [&](const char* key, auto& a) {
    auto* values = v[key].getArray();
    if (!values || values->size() != 2) return false;
    for (int i = 0; i < 2; ++i) {
      if (!(*values)[i].isString() || (*values)[i].toString().length() > 128) return false;
      a[static_cast<size_t>(i)] = (*values)[i].toString().toStdString();
    }
    return true;
  };
  if (!readIds("stackEnd", c.stackEnd) || !readIds("modEnd", c.modEnd) ||
      !readIds("wetStart", c.wetStart) || !readIds("cabinet", c.cabinet)) return fail();
  if (!integer(v["stereoInputRight"]) || !integer(v["diInput"])) return fail();
  for (const char* key : {"inputGainDb", "reampSensitivityDb", "masterVolumeDb"}) if (!number(v[key])) return fail();
  c.stereoInputRight = v["stereoInputRight"]; c.diInput = v["diInput"];
  c.inputGainDb = static_cast<float>(v["inputGainDb"]);
  c.reampSensitivityDb = static_cast<float>(v["reampSensitivityDb"]);
  c.masterVolumeDb = static_cast<float>(v["masterVolumeDb"]);
  auto* out = v["outputs"].getArray();
  if (!out || out->size() != static_cast<int>(portCount)) return fail();
  for (size_t i = 0; i < portCount; ++i) {
    const auto& p = (*out)[static_cast<int>(i)]; auto& o = c.outputs[i];
    if (!integer(p["source"]) || !integer(p["left"]) || !integer(p["right"])) return fail();
    for (const char* key : {"volumeDb", "bassDb", "middleDb", "trebleDb", "presenceDb", "lowCutHz", "highCutHz"}) if (!number(p[key])) return fail();
    for (const char* key : {"linkMaster", "pad12", "stereo", "cabinetOff"}) if (!p[key].isBool()) return fail();
    int source = p["source"];
    if (!p.isObject() || source < 0 || source > static_cast<int>(Source::returns)) return fail();
    o.source = static_cast<Source>(source); o.left = p["left"]; o.right = p["right"];
    o.volumeDb = static_cast<float>(p["volumeDb"]); o.linkMaster = p["linkMaster"]; o.pad12 = p["pad12"];
    o.stereo = p["stereo"]; o.cabinetOff = p["cabinetOff"];
    o.bassDb = static_cast<float>(p["bassDb"]); o.middleDb = static_cast<float>(p["middleDb"]);
    o.trebleDb = static_cast<float>(p["trebleDb"]); o.presenceDb = static_cast<float>(p["presenceDb"]);
    o.lowCutHz = static_cast<float>(p["lowCutHz"]); o.highCutHz = static_cast<float>(p["highCutHz"]);
  }
  if (!validate(c, 128, 128).isEmpty()) return fail();
  result = std::move(c);
  return juce::Result::ok();
}
juce::StringArray validate(const Config& c, int inputs, int outputs) {
  juce::StringArray errors;
  auto range = [&](float x, float lo, float hi, const char* name) {
    if (!std::isfinite(x) || x < lo || x > hi) errors.add(juce::String(name) + " is outside its supported range.");
  };
  auto input = [&](int n, bool required, const char* name) {
    if (n < (required ? 0 : -1) || n >= inputs) errors.add(juce::String(name) + " channel is unavailable.");
  };
  const int src = static_cast<int>(c.inputSource);
  if (src < 0 || src > 3) errors.add("Invalid input source.");
  else input(c.inputChannels[static_cast<size_t>(src)], true, "Selected input");
  for (int ch : c.inputChannels) input(ch, false, "Input alias");
  input(c.diInput, true, "Raw DI"); input(c.stereoInputRight, false, "Stereo input right");
  for (int ch : c.returnInputs) input(ch, false, "Return input");
  for (int ch : c.auxInputs) input(ch, false, "Auxiliary input");
  range(c.inputGainDb, -24, 24, "Input gain"); range(c.reampSensitivityDb, -24, 24, "Reamp sensitivity");
  range(c.masterVolumeDb, -60, 12, "Master volume");
  for (float x : c.auxMix) range(x, 0, 1, "Auxiliary mix");
  std::set<int> used;
  for (size_t i = 0; i < portCount; ++i) {
    const auto& o = c.outputs[i]; const juce::String name = portName(static_cast<Port>(i));
    if (static_cast<int>(o.source) < 0 || o.source > Source::returns) errors.add(name + ": invalid source.");
    range(o.volumeDb, -60, 12, "Output volume");
    for (float db : {o.bassDb, o.middleDb, o.trebleDb, o.presenceDb}) range(db, -12, 12, "Output EQ");
    for (float hz : {o.lowCutHz, o.highCutHz}) {
      if (hz != 0) range(hz, 20, 20000, "Output filter");
    }
    if (o.lowCutHz > 0 && o.highCutHz > 0 && o.lowCutHz >= o.highCutHz) errors.add(name + ": low cut must be below high cut.");
    if (o.cabinetOff && i != static_cast<size_t>(Port::monitor)) errors.add("Cabinet Off belongs to Monitor.");
    if (o.source == Source::off) continue;
    const bool pair = (stereoSource(o.source) && o.stereo) || (o.source >= Source::diStack && o.source <= Source::diMasterMono);
    for (int ch : {o.left, pair ? o.right : -1}) {
      if (ch == -1 && !pair && ch != o.left) continue;
      if (ch < 0 || ch >= outputs) errors.add(name + ": select an available output " + (ch == o.left ? "left/mono" : "right") + " channel.");
      else if (!used.insert(ch).second) errors.add(name + ": output channel " + juce::String(ch + 1) + " is already assigned to another route.");
    }
  }
  return errors;
}
bool sameDeviceTopology(const Config& a, const Config& b) {
  if (a.inputDevice != b.inputDevice || a.outputDevice != b.outputDevice || a.inputSource != b.inputSource ||
      a.inputChannels != b.inputChannels || a.stereoInputRight != b.stereoInputRight || a.diInput != b.diInput ||
      a.returnInputs != b.returnInputs || a.auxInputs != b.auxInputs || a.stackEnd != b.stackEnd ||
      a.modEnd != b.modEnd || a.wetStart != b.wetStart || a.cabinet != b.cabinet) return false;
  for (size_t i = 0; i < portCount; ++i)
    if (a.outputs[i].left != b.outputs[i].left || a.outputs[i].right != b.outputs[i].right ||
        a.outputs[i].cabinetOff != b.outputs[i].cabinetOff) return false;
  return true;
}
std::vector<std::string> effectivePath(const juce::var& state, int renderedLane) {
  if (renderedLane < 0 || renderedLane > 1) return {};
  const bool stereo = state["stereoEnabled"];
  const int lane = stereo ? renderedLane : 0;
  std::vector<std::string> result;
  auto append = [&](int side, const juce::String& end) {
    if (const auto* blocks = state[side == 0 ? "chain" : "chainRight"].getArray())
      for (const auto& b : *blocks) {
        if (b["kind"].toString() != "insert") result.push_back(b["blockId"].toString().toStdString());
        if (end.isNotEmpty() && b["blockId"].toString() == end) break;
      }
  };
  const auto branch = state["branch"];
  if (stereo && branch.isObject()) {
    const int trunk = branch["side"].toString() == "right" ? 1 : 0;
    if (lane != trunk) {
      const auto anchor = branch["afterBlockId"].toString();
      const auto* trunkBlocks = state[trunk == 0 ? "chain" : "chainRight"].getArray();
      if (anchor.isNotEmpty() && trunkBlocks && std::any_of(trunkBlocks->begin(), trunkBlocks->end(),
            [&](const auto& b) { return b["blockId"].toString() == anchor; })) append(trunk, anchor);
    }
  }
  append(lane, {});
  return result;
}
} // namespace t3k::routing
