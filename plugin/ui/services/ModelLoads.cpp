#include "ModelLoads.h"

namespace t3k::ui {

void ModelLoads::retry(const std::string& blockId) {
  if (!session_.authenticated()) {
    chain_.retryModelLoad(blockId);
    return;
  }
  // Session expired: the retry still runs and native falls back to whatever
  // token it holds.
  session_.ensureNativeAuth([this, blockId](const juce::String&) { chain_.retryModelLoad(blockId); });
}

void ModelLoads::switchModel(const std::string& blockId, const Model& model, std::function<void(bool)> done) {
  auto go = [this, blockId, model, done] {
    const bool ok = chain_.switchModel(blockId, model.id, model.raw);
    if (!ok) juce::Logger::writeToLog("Failed to switch model");
    if (done) done(ok);
  };
  if (model.modelUrl.startsWith("file:")) {
    go();
    return;
  }
  session_.ensureNativeAuth([go, done](const juce::String& error) {
    if (error.isNotEmpty()) {
      juce::Logger::writeToLog("Cannot switch model: TONE3000 session expired: " + error);
      if (done) done(false);
      return;
    }
    go();
  });
}

}  // namespace t3k::ui
