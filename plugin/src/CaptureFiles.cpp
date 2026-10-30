#include "StandaloneCapture.h"
#include "CaptureDsp.h"
#include <cmath>

namespace t3k::capture {
namespace {
juce::Result fail(const juce::String& message) {
  return juce::Result::fail(message);
}
float peak(const std::vector<float>& samples) {
  float value = 0;
  for (float sample : samples)
    value = juce::jmax(value, std::abs(sample));
  return value;
}
}  // namespace
juce::Result StandaloneCapture::prepareRecording(Recording& recording) {
  if (recording.convertedTo48k)
    return juce::Result::ok();
  if (std::abs(recording.deviceSampleRate - sampleRate) > 0.01) {
    auto wet = resample(recording.wet, recording.deviceSampleRate, sampleRate);
    auto dry = recording.dry.empty()
                   ? std::vector<float>()
                   : resample(recording.dry, recording.deviceSampleRate, sampleRate);
    if (wet.empty() || (!recording.dry.empty() && dry.empty()))
      return fail("The recorded audio could not be converted to48 kHz.");
    recording.wet = std::move(wet);
    recording.dry = std::move(dry);
  }
  recording.convertedTo48k = true;
  return juce::Result::ok();
}
juce::Result StandaloneCapture::readSignal(const juce::File& file,
                                           std::vector<float>& samples,
                                           juce::MemoryBlock& original) {
  if (!file.existsAsFile() || file.getSize() > 128 * 1024 * 1024)
    return fail("Choose a mono 48 kHz WAV of ten minutes or less.");
  if (!file.loadFileAsData(original))
    return fail("The test WAV could not be read.");
  juce::WavAudioFormat format;
  std::unique_ptr<juce::AudioFormatReader> reader(
      format.createReaderFor(new juce::MemoryInputStream(original, false), true));
  if (!reader || reader->numChannels != 1 || std::abs(reader->sampleRate - sampleRate) > 0.01)
    return fail(
        "The test signal must be a mono 48 kHz WAV. Use the current original file supplied by "
        "TONE3000.");
  if (reader->lengthInSamples < 64 || reader->lengthInSamples > sampleRate * 600)
    return fail("The test WAV must be between 64 samples and ten minutes long.");
  juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
  if (!reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false))
    return fail("The test WAV is incomplete or damaged.");
  samples.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
  for (float value : samples)
    if (!std::isfinite(value) || std::abs(value) > 1.0f)
      return fail("The test signal contains invalid or out-of-range samples.");
  if (peak(samples) < 0.000001f)
    return fail("The test signal is silent.");
  return juce::Result::ok();
}
juce::Result StandaloneCapture::writeWav(const juce::File& file,
                                         const std::vector<float>& samples) {
  if (samples.empty())
    return fail("Cannot write empty audio.");
  for (float value : samples)
    if (!std::isfinite(value) || std::abs(value) > 1.0f)
      return fail(
          "Audio exceeds full scale or contains invalid samples. Lower the recording level and "
          "recapture.");
  std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
  if (!stream)
    return fail("Cannot create " + file.getFileName());
  juce::WavAudioFormat format;
  const auto options = juce::AudioFormatWriterOptions()
                           .withSampleRate(sampleRate)
                           .withNumChannels(1)
                           .withBitsPerSample(24);
  auto writer = format.createWriterFor(stream, options);
  if (!writer)
    return fail("Cannot create a 24-bit WAV writer.");
  const float* channels[] = {samples.data()};
  if (!writer->writeFromFloatArrays(channels, 1, static_cast<int>(samples.size())))
    return fail("WAV write failed. Check free disk space.");
  if (!writer->flush())
    return fail("WAV could not be flushed to disk.");
  return juce::Result::ok();
}
juce::Result StandaloneCapture::exportRecording(const Recording& r,
                                                const juce::File& parent,
                                                int offset,
                                                const std::vector<float>& ir,
                                                juce::File& folder) {
  if (std::abs(r.deviceSampleRate - sampleRate) > 0.01 && !r.convertedTo48k)
    return fail("Prepare the recording at 48 kHz before exporting.");
  if (r.wet.size() < sampleRate || peak(r.wet) < 0.00001f)
    return fail("Record at least one second of a non-silent Kemper return.");
  if (r.config.mode == Mode::dryWet && (r.dry.size() != r.wet.size() || peak(r.dry) < 0.00001f))
    return fail("Dry DI and wet return must both contain the same non-silent performance.");
  std::vector<float> trimmed;
  if (r.config.mode == Mode::namSignal) {
    if (offset < 0 || static_cast<size_t>(offset) + r.reference.size() > r.wet.size() ||
        r.sourceWav.isEmpty())
      return fail("The selected offset cannot contain the full original test signal.");
    trimmed.assign(r.wet.begin() + offset,
                   r.wet.begin() + offset + static_cast<std::ptrdiff_t>(r.reference.size()));
  }
  if (r.config.mode == Mode::impulseResponse && ir.empty())
    return fail("Extract the IR before exporting.");
  const auto created = parent.createDirectory();
  if (created.failed())
    return created;
  auto name = juce::File::createLegalFileName(r.config.name).trim().substring(0, 80);
  if (name.isEmpty())
    name = "Kemper capture";
  folder = parent.getNonexistentChildFile(
      name + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), "", false);
  if (!folder.createDirectory())
    return fail("Cannot create an export session folder.");
  auto save = [&](const char* filename, const std::vector<float>& audio) {
    return writeWav(folder.getChildFile(filename), audio);
  };
  juce::Result result = juce::Result::ok();
  if (r.config.mode == Mode::dryWet) {
    result = save("DRY.wav", r.dry);
    if (result.wasOk())
      result = save("WET.wav", r.wet);
  } else if (r.config.mode == Mode::namSignal) {
    result = save("WET.wav", trimmed);
    if (result.wasOk())
      result = save("RETURN-RAW.wav", r.wet);
    if (result.wasOk() && !folder.getChildFile("REFERENCE.wav")
                               .replaceWithData(r.sourceWav.getData(), r.sourceWav.getSize()))
      result = fail("Cannot save the original reference WAV.");
  } else {
    result = save("IR.wav", ir);
    if (result.wasOk())
      result = save("RETURN-RAW.wav", r.wet);
    if (result.wasOk())
      result = save("SWEEP.wav", r.reference);
  }
  auto* metadata = new juce::DynamicObject();
  juce::var metadataValue(metadata);
  metadata->setProperty("captureFormatVersion", 1);
  metadata->setProperty("gear", r.config.gear);
  metadata->setProperty("rig", r.config.rig);
  metadata->setProperty("notes", r.config.notes);
  metadata->setProperty("createdAt", r.createdAt);
  metadata->setProperty("nativeDeviceSampleRate", r.deviceSampleRate);
  metadata->setProperty("resampledForExport", std::abs(r.deviceSampleRate - sampleRate) > 0.01);
  metadata->setProperty("sampleRate", sampleRate);
  metadata->setProperty("bitsPerSample", 24);
  metadata->setProperty("mode", r.config.mode == Mode::dryWet      ? "dry-wet"
                                : r.config.mode == Mode::namSignal ? "nam-signal"
                                                                   : "ir");
  metadata->setProperty("inputDevice", r.inputDevice);
  metadata->setProperty("outputDevice", r.outputDevice);
  metadata->setProperty("dryInput", r.config.dryInput + 1);
  metadata->setProperty("wetInput", r.config.wetInput + 1);
  metadata->setProperty("sendOutput", r.config.sendOutput + 1);
  metadata->setProperty("outputTrimDb", r.config.outputDb);
  metadata->setProperty("offsetSamples", offset);
  metadata->setProperty("clipped", r.clipped);
  metadata->setProperty("irProcessing",
                        "IR only: arrival delay removed, peak normalized to 0.9; weak early "
                        "arrivals may be omitted.");
  if (result.wasOk() &&
      !folder.getChildFile("session.json").replaceWithText(juce::JSON::toString(metadataValue)))
    result = fail("Cannot save session notes.");
  const auto instructions =
      juce::String("TONE3000 capture\n\n") +
      (r.config.mode == Mode::dryWet ? "Upload DRY.wav and WET.wav as Dry/Wet Pair. Both are "
                                       "mono 24-bit 48kHz, exactly equal in length.\n"
       : r.config.mode == Mode::namSignal
           ? "Upload WET.wav with the matching official TONE3000 test-signal workflow. It has the "
             "exact test-file frame count. REFERENCE.wav is the original imported file; "
             "RETURN-RAW.wav preserves the full recording.\n"
           : "IR.wav is the finished impulse response. No NAM training is needed. It captures a "
             "linear response only, not amp distortion or moving/time-based effects.\n") +
      "\nNAM training happens at https://www.tone3000.com/capture . After training, use the same "
      "account in the TONE3000 app.\nNo upload/training has been performed by this "
      "app.\n\nTONE3000 public sharing rules exclude captures of current modelers such as Kemper. "
      "Check applicable terms before uploading or sharing.\n";
  if (result.wasOk() && !folder.getChildFile("READ-ME.txt").replaceWithText(instructions))
    result = fail("Cannot save capture instructions.");
  if (result.failed()) {
    folder.deleteRecursively();
    folder = juce::File();
  }
  return result;
}
}  // namespace t3k::capture
