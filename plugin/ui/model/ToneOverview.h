#pragma once

#include "Tone.h"
#include "CaptureDescriptions.h"
#include "core/Labels.h"

namespace t3k::ui {

// Extract explicit creator statements locally. No network summarizer and no
// guessed settings. Pack-level settings are not necessarily the selected model's.
inline juce::String toneOverview(const Tone& tone, const juce::String& modelName = {}) {
  const auto curated = captureDescription(tone.id, {});
  juce::String result;
  if (curated.isNotEmpty()) {
    result = curated;
  } else {
    juce::String use, settings, includes;
    juce::StringArray lines;
    lines.addLines(tone.description.replace(". ", ".\n"));
    bool inSettings = false;
    const auto append = [](juce::String& target, const juce::String& line) {
      // Bound summaries at complete source lines; full copy stays under More.
      if (line.isEmpty() || line.length() > 240 || target.length() + line.length() > 360) return;
      if (target.isNotEmpty()) target += "; ";
      target += line;
    };
    for (const auto& source : lines) {
      const auto line = source.trim();
      const auto lower = line.toLowerCase();
      if (line.isEmpty()) { inSettings = false; continue; }
      if (lower.contains("http") || lower.contains("www.") || lower.contains("epoch")) continue;
      const auto label = lower.upToFirstOccurrenceOf(":", false, false).trim();
      if (label == "use" || label == "used for") {
        append(use, line.fromFirstOccurrenceOf(":", false, false).trim());
        inSettings = false;
      } else if (label == "includes" || label == "included") {
        append(includes, line.fromFirstOccurrenceOf(":", false, false).trim());
        inSettings = false;
      } else if (label == "settings" || label == "amp settings" || label == "capture settings") {
        inSettings = true;
        append(settings, line.fromFirstOccurrenceOf(":", false, false).trim());
      } else {
        const bool numeric = line.containsAnyOf("0123456789");
        const bool setting = lower.startsWith("gain:") || lower.startsWith("bass:") ||
            lower.startsWith("mid:") || lower.startsWith("treble:") || lower.startsWith("presence:") ||
            lower.startsWith("volume:") || lower.startsWith("eq:") || lower.startsWith("compression:") ||
            lower.startsWith("ratio:") || lower.startsWith("attack:") || lower.startsWith("release:");
        if ((inSettings && numeric) || setting || lower.contains("control at ") ||
            lower.contains("set to ") || lower.contains("settings:")) append(settings, line);
        else {
          inSettings = false;
          if (use.isEmpty() && (lower.startsWith("great for ") || lower.startsWith("perfect for ") ||
                               lower.startsWith("use "))) append(use, line);
          if (includes.isEmpty() && (lower.contains("preamp-only") || lower.contains("preamp only") ||
                                    lower.contains("no cabinet") || lower.contains("no cab") ||
                                    lower.startsWith("includes "))) append(includes, line);
        }
      }
    }
    if (use.isEmpty()) {
      if (tone.gear == "amp") use = "Amp tone; follow with a cabinet IR.";
      else if (tone.gear == "amp-cab" || tone.gear == "full-rig") use = "Complete amp and cabinet tone.";
      else if (tone.gear == "cab") use = "Cabinet sound after an amp.";
      else if (tone.gear == "pedal") use = "Pedal effect in your signal chain.";
      else if (tone.gear == "outboard") use = "Studio hardware processing.";
      else if (tone.gear == "space") use = "Room ambience or reverb.";
      if (use.isEmpty()) use = "Not supplied.";
    }
    if (includes.isEmpty()) {
      const auto gear = labels::gear(tone.gear);
      includes = gear.isEmpty() ? "Not supplied." : gear + " capture; chain details not supplied.";
    }
    if (settings.isEmpty()) settings = "Not supplied.";
    result = "Use: " + use + "\nSettings: " + settings + "\nIncludes: " + includes;
  }
  if (modelName.isNotEmpty()) result += "\nSelected model: " + modelName;
  result += "\nPack settings may vary by model.";
  result += "\nCapture settings are baked in; plugin EQ adds further shaping.";
  return result;
}

}  // namespace t3k::ui
