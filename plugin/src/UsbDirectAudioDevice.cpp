#include "UsbDirectAudioDevice.h"

#if JUCE_ANDROID

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "UacControl.h"
#include "UacDescriptors.h"
#include "UsbAudioStream.h"
#include "UsbQuirks.h"
#include "usb/android/UsbfsTransport.h"

extern "C" JNIEXPORT jboolean JNICALL
Java_com_TONE3000_TONE3000_UsbDirectBridge_nativeQuirkIgnoresInterface(
    JNIEnv*, jclass, jint, jint, jint);

namespace t3k {
namespace {

using usb::StreamConfig;
using usb::UsbAudioStream;
using usb::android::UsbfsTransport;

constexpr int kUrbsInFlight = 4;    // 3 misses the OUT slot ~2% of the time
constexpr int kDefaultWireChannels = 2;
constexpr int kMaxWireChannels = 32;  // UAC2 stream channel ceiling we accept
constexpr const char* kTypeName = "USB Direct";

// ---------------------------------------------------------------------------
// Java bridge (android/app .../UsbDirectBridge.java). The activity owns the
// permission prompt; this class only opens the already-permitted device and
// hands over the fd and descriptors.

class UsbDirectBridgeClass : public juce::JNIClassBase {
 public:
  UsbDirectBridgeClass()
      : JNIClassBase("com/TONE3000/TONE3000/UsbDirectBridge", 0, nullptr, 0) {}

  void initialiseFields(JNIEnv* env) override {
    if (getJclass() == nullptr) {
      juce::Logger::writeToLog("USB Direct: Java bridge class not found");
      return;
    }
    openDevice = resolveStaticMethod(env, "openDevice", "()I");
    closeDevice = resolveStaticMethod(env, "closeDevice", "()V");
    isAvailable = resolveStaticMethod(env, "isAvailable", "()Z");
    deviceName = resolveStaticMethod(env, "deviceName", "()Ljava/lang/String;");
    rawDescriptors = resolveStaticMethod(env, "getRawDescriptors", "()[B");
    kernelDriverBound = resolveStaticMethod(env, "kernelDriverBound", "()Z");
  }

  jmethodID openDevice = nullptr;
  jmethodID closeDevice = nullptr;
  jmethodID isAvailable = nullptr;
  jmethodID deviceName = nullptr;
  jmethodID rawDescriptors = nullptr;
  jmethodID kernelDriverBound = nullptr;
};

UsbDirectBridgeClass gBridge;

bool bridgeReady() {
  return gBridge.getJclass() != nullptr && gBridge.openDevice != nullptr;
}

int openJavaDevice() {
  if (!bridgeReady()) return -1;
  auto* env = juce::getEnv();
  if (env == nullptr) return -1;
  const jint fd = env->CallStaticIntMethod(gBridge.getJclass(), gBridge.openDevice);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    return -1;
  }
  return fd;
}

void closeJavaDevice() {
  if (!bridgeReady()) return;
  auto* env = juce::getEnv();
  if (env == nullptr) return;
  env->CallStaticVoidMethod(gBridge.getJclass(), gBridge.closeDevice);
  if (env->ExceptionCheck()) env->ExceptionClear();
}

bool javaKernelDriverBound() {
  if (!bridgeReady() || gBridge.kernelDriverBound == nullptr) return false;
  auto* env = juce::getEnv();
  if (env == nullptr) return false;
  const jboolean bound = env->CallStaticBooleanMethod(gBridge.getJclass(),
                                                      gBridge.kernelDriverBound);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  return bound == JNI_TRUE;
}

bool javaDeviceAvailable() {
  if (!bridgeReady()) return false;
  auto* env = juce::getEnv();
  if (env == nullptr) return false;
  const jboolean yes = env->CallStaticBooleanMethod(gBridge.getJclass(), gBridge.isAvailable);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  return yes == JNI_TRUE;
}

juce::String javaDeviceName() {
  if (!bridgeReady()) return {};
  auto* env = juce::getEnv();
  if (env == nullptr) return {};
  auto name = static_cast<jstring>(
      env->CallStaticObjectMethod(gBridge.getJclass(), gBridge.deviceName));
  if (env->ExceptionCheck() || name == nullptr) {
    env->ExceptionClear();
    return {};
  }
  const char* utf = env->GetStringUTFChars(name, nullptr);
  juce::String result(utf != nullptr ? utf : "");
  if (utf != nullptr) env->ReleaseStringUTFChars(name, utf);
  env->DeleteLocalRef(name);
  return result;
}

std::vector<uint8_t> javaDescriptors() {
  if (!bridgeReady()) return {};
  auto* env = juce::getEnv();
  if (env == nullptr) return {};
  auto array = static_cast<jbyteArray>(
      env->CallStaticObjectMethod(gBridge.getJclass(), gBridge.rawDescriptors));
  if (env->ExceptionCheck() || array == nullptr) {
    env->ExceptionClear();
    return {};
  }
  const jsize len = env->GetArrayLength(array);
  std::vector<uint8_t> out(static_cast<size_t>(len));
  if (len > 0) env->GetByteArrayRegion(array, 0, len, reinterpret_cast<jbyte*>(out.data()));
  env->DeleteLocalRef(array);
  return out;
}

// ---------------------------------------------------------------------------
// Configuration

bool buildConfig(const std::vector<uint8_t>& raw, double sampleRate, StreamConfig& cfg,
                 juce::String& error) {
  usb::ConfigTree tree;
  if (!usb::parseConfig(raw.data(), raw.size(), tree)) {
    error = "could not parse the USB descriptors";
    return false;
  }
  cfg = StreamConfig{};
  cfg.uac = tree.uacVersion();
  cfg.speed = usb::UsbSpeed::high;  // the AMS-22 and this app's target are HS
  cfg.sampleRate = static_cast<uint32_t>(sampleRate);
  cfg.channels = 0;  // decided from the selected altsetting below
  cfg.bitResolution = 24;
  cfg.subslotBytes = 4;
  cfg.feedback16_16 = cfg.uac == usb::UacVersion::uac2;
  cfg.feedbackBytes = cfg.feedback16_16 ? 4 : 3;

  for (const auto& alt : tree.altSettings) {
    if (alt.isAudioControl()) {
      cfg.controlInterface = alt.interfaceNumber;
      break;
    }
  }

  // 0 channels means "any": UAC2 keeps the count on the terminal, which the
  // parser resolves. A stereo-only device still yields 2; the EVO8 yields 6.
  const usb::AltSetting* play = tree.findAlt(true, cfg.sampleRate, 0);
  const usb::AltSetting* cap = tree.findAlt(false, cfg.sampleRate, 0);
  if (play == nullptr || play->dataEndpoint() == nullptr) {
    error = "no playback altsetting for this sample rate";
    return false;
  }
  if (cap == nullptr || cap->dataEndpoint() == nullptr) {
    error = "no capture altsetting for this sample rate";
    return false;
  }
  const uint8_t playChannels = play->format.channels > 0 ? play->format.channels : 2;
  const uint8_t capChannels =
      cap->format.channels > 0 ? cap->format.channels : playChannels;
  if (playChannels != capChannels) {
    error = "asymmetric input/output channel counts are not supported yet";
    return false;
  }
  cfg.channels = playChannels;
  cfg.playbackEndpoint = play->dataEndpoint()->address;
  cfg.playbackInterface = play->interfaceNumber;
  cfg.playbackAltsetting = play->altSetting;
  cfg.playbackSynchronous = play->dataEndpoint()->isSynchronous();
  if (const auto* fb = play->feedbackEndpoint()) cfg.feedbackEndpoint = fb->address;
  if (play->format.subslotBytes != 0) cfg.subslotBytes = play->format.subslotBytes;
  if (play->format.bitResolution != 0) cfg.bitResolution = play->format.bitResolution;
  cfg.captureEndpoint = cap->dataEndpoint()->address;
  cfg.captureInterface = cap->interfaceNumber;
  cfg.captureAltsetting = cap->altSetting;
  if (const usb::Entity* clock = tree.rateClockSource()) cfg.clockSourceId = clock->id;
  return true;
}

// Rate first, then SET_INTERFACE (plan section 6.2). GET_CUR is optional: the
// AMS-22 STALLs it (EPIPE), so its absence is not an error.
bool applyControl(UsbfsTransport& transport, const StreamConfig& cfg, juce::String& error) {
  std::vector<uint8_t> response;
  if (cfg.uac == usb::UacVersion::uac2) {
    if (!transport.control(
            usb::uac::uac2SetSampleRate(cfg.controlInterface, cfg.clockSourceId,
                                        cfg.sampleRate),
            response)) {
      error = "SET_CUR sample rate failed";
      return false;
    }
  } else {
    if (!transport.control(usb::uac::uac1SetSampleRate(cfg.playbackEndpoint, cfg.sampleRate),
                           response)) {
      error = "SET_CUR sample rate failed";
      return false;
    }
  }
  if (!transport.setInterface(cfg.playbackInterface, cfg.playbackAltsetting)) {
    error = "SET_INTERFACE (playback) failed";
    return false;
  }
  if (!transport.setInterface(cfg.captureInterface, cfg.captureAltsetting)) {
    error = "SET_INTERFACE (capture) failed";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// The device

class UsbDirectAudioIODevice final : public juce::AudioIODevice,
                                     public UsbAudioStream::Callback,
                                     public AudioDeviceDiagnostics {
 public:
  explicit UsbDirectAudioIODevice(const juce::String& deviceName)
      : juce::AudioIODevice(deviceName, kTypeName) {}

  ~UsbDirectAudioIODevice() override { close(); }

  double getDriftPpm() const override {
    return stream_ != nullptr ? stream_->captureStats().driftPpm : 0.0;
  }

  juce::StringArray getOutputChannelNames() override {
    juce::StringArray names;
    for (int i = 0; i < wireChannels_; ++i) names.add("Output " + juce::String(i + 1));
    return names;
  }
  juce::StringArray getInputChannelNames() override {
    juce::StringArray names;
    for (int i = 0; i < wireChannels_; ++i) names.add("Input " + juce::String(i + 1));
    return names;
  }

  juce::Array<double> getAvailableSampleRates() override {
    // 44.1/88.2 need fractional microframe packet sizes; the app is pinned
    // at 48 kHz (plan D6), so expose only the integer-cadence rates here.
    return {48000.0, 96000.0};
  }

  juce::Array<int> getAvailableBufferSizes() override {
    // The callback period is one URB: 1, 2, 3 or 4 ms. At high speed a packet
    // carries sampleRate/8000 frames, so 8/16/24/32 packets give 48/96/144/192
    // frames at 48 kHz (96/192/288/384 at 96 kHz). Longer periods relax the
    // DSP deadline and add exactly that much latency.
    const int framesPerPacket =
        juce::jmax(1, static_cast<int>(config_.sampleRate) / 8000);
    juce::Array<int> sizes;
    for (int packets : {8, 16, 24, 32}) sizes.add(packets * framesPerPacket);
    return sizes;
  }

  int getDefaultBufferSize() override { return getAvailableBufferSizes()[0]; }

  juce::String open(const juce::BigInteger& inputChannels,
                    const juce::BigInteger& outputChannels, double sampleRate,
                    int bufferSizeSamples) override {
    close();
    lastError_.clear();

    if (!bridgeReady()) {
      lastError_ = "USB Direct bridge is not initialised";
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      return lastError_;
    }

    sampleRate_ = sampleRate;
    // The packet cadence and the SET_CUR rate both come from config_, so it
    // must track the requested rate before applyControl() and before the
    // frames-per-packet calculation (else 96 kHz streams 48 kHz packets).
    config_.sampleRate = static_cast<uint32_t>(sampleRate);
    const int fd = openJavaDevice();
    if (fd < 0) {
      lastError_ = "USB audio device unavailable (is it attached and permitted?)";
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      return lastError_;
    }
    fd_ = fd;

    const auto raw = javaDescriptors();
    if (raw.empty()) {
      lastError_ = "could not read the USB descriptors";
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      close();
      return lastError_;
    }
    if (!buildConfig(raw, sampleRate, config_, lastError_)) {
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      close();
      return lastError_;
    }

    juce::Logger::writeToLog(
        "USB Direct: config subslot=" + juce::String(static_cast<int>(config_.subslotBytes)) +
        " play=0x" + juce::String::toHexString(config_.playbackEndpoint) +
        " cap=0x" + juce::String::toHexString(config_.captureEndpoint) +
        " fb=0x" + juce::String::toHexString(config_.feedbackEndpoint) +
        " sync=" + juce::String(config_.playbackSynchronous ? 1 : 0) +
        " clock=" + juce::String(static_cast<int>(config_.clockSourceId)));
    transport_ = std::make_unique<UsbfsTransport>(fd_);
    if (!applyControl(*transport_, config_, lastError_)) {
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      close();
      return lastError_;
    }

    wireChannels_ = juce::jmax(1, static_cast<int>(config_.channels));
    inputMap_ = channelsFromMask(inputChannels);
    outputMap_ = channelsFromMask(outputChannels);
    const int framesPerPacket = config_.nominalFramesPerPacket(config_.speed);
    // Buffer size = one URB. Snap the request to the supported periods
    // (8/16/24/32 packets = 1/2/3/4 ms at high speed); 0 means the 1 ms
    // default. A remembered generic value like 128 lands on 2 ms rather than
    // a packet count the UI cannot express.
    const int requestedPackets =
        bufferSizeSamples > 0
            ? (bufferSizeSamples + framesPerPacket / 2) / framesPerPacket
            : 8;
    packetsPerUrb_ = 8;
    int bestDelta = std::abs(8 - requestedPackets);
    for (int candidate : {16, 24, 32}) {
      const int delta = std::abs(candidate - requestedPackets);
      if (delta < bestDelta) {
        bestDelta = delta;
        packetsPerUrb_ = candidate;
      }
    }
    bufferFrames_ = packetsPerUrb_ * framesPerPacket;
    maxFrames_ = bufferFrames_ + 2;
    inputBuffer_.setSize(wireChannels_, maxFrames_);
    outputBuffer_.setSize(wireChannels_, maxFrames_);
    inputBuffer_.clear();
    outputBuffer_.clear();

    UsbAudioStream::Options options;
    options.packetsPerUrb = packetsPerUrb_;
    options.urbsInFlight = kUrbsInFlight;
    // 128 frames at 48 kHz is 2.7 ms, of which the pacer targets half. This
    // was measured against 512 (8.9 ms RTL) and 96 (no further gain):
    // results-m2-soak.txt / the M2.4 report.
    options.ringFrames = static_cast<size_t>(std::max(128, 2 * bufferFrames_));
    options.nowNs = [] {
      return static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());
    };

    stream_ = std::make_unique<UsbAudioStream>(config_, options, *transport_, *this);
    if (!stream_->start()) {
      lastError_ = "URB submission failed";
      juce::Logger::writeToLog("USB Direct: open failed: " + lastError_);
      close();
      return lastError_;
    }
    // Priming the playback pool before the first capture URB is counted as
    // an underrun by the ring policy; it is not a user-visible xrun.
    baselineRingUnder_ = static_cast<int>(stream_->playbackStats().ringUnderruns);
    baselineRingOver_ = static_cast<int>(stream_->captureStats().ringOverruns);
    open_ = true;
    juce::Logger::writeToLog("USB Direct: open " + juce::String(sampleRate_, 0) +
                             " Hz, " + juce::String(bufferFrames_) + "-frame blocks, " +
                             juce::String(packetsPerUrb_) + "x" +
                             juce::String(kUrbsInFlight) + " URBs, ring " +
                             juce::String(static_cast<int>(options.ringFrames)));
    return {};
  }

  void close() override {
    stop();
    if (transport_ != nullptr) {
      transport_->setInterface(config_.playbackInterface, 0);
      transport_->setInterface(config_.captureInterface, 0);
    }
    stream_.reset();
    transport_.reset();
    if (fd_ >= 0) {
      closeJavaDevice();
      fd_ = -1;
    }
    open_ = false;
  }

  bool isOpen() override { return open_; }

  void start(juce::AudioIODeviceCallback* callback) override {
    if (!open_ || playing_) return;
    callback_.store(callback);
    if (callback != nullptr) callback->audioDeviceAboutToStart(this);
    if (stream_ != nullptr && !stream_->running()) {
      if (!stream_->start()) {
        lastError_ = "URB submission failed on restart";
        callback_.store(nullptr);
        return;
      }
    }
    stopRequested_.store(false);
    pump_ = std::thread([this] { pumpLoop(); });
    playing_ = true;
    juce::Logger::writeToLog("USB Direct: start");
  }

  void stop() override {
    if (!playing_ && !pump_.joinable()) {
      callback_.store(nullptr);
      return;
    }
    stopRequested_.store(true);
    if (pump_.joinable()) pump_.join();
    if (stream_ != nullptr) stream_->stop();
    playing_ = false;
    if (auto* callback = callback_.exchange(nullptr)) callback->audioDeviceStopped();
    juce::Logger::writeToLog("USB Direct: stop, xruns=" + juce::String(getXRunCount()));
  }

  bool isPlaying() override { return playing_; }

  juce::String getLastError() override { return lastError_; }

  int getCurrentBufferSizeSamples() override { return bufferFrames_; }
  double getCurrentSampleRate() override { return sampleRate_; }
  int getCurrentBitDepth() override { return 24; }

  juce::BigInteger getActiveOutputChannels() const override {
    return maskFromChannels(outputMap_);
  }
  juce::BigInteger getActiveInputChannels() const override {
    return maskFromChannels(inputMap_);
  }

  int getOutputLatencyInSamples() override {
    // URB depth + a 1 ms converter estimate (plan M3.1: honest accounting,
    // estimates marked as estimates).
    return kUrbsInFlight * bufferFrames_ + static_cast<int>(sampleRate_ / 1000.0);
  }

  int getInputLatencyInSamples() override {
    return getOutputLatencyInSamples() + static_cast<int>(ringTargetFrames());
  }

  int getXRunCount() const noexcept override {
    if (stream_ == nullptr) return 0;
    const int ringOver = static_cast<int>(stream_->captureStats().ringOverruns) -
                         baselineRingOver_;
    const int ringUnder = static_cast<int>(stream_->playbackStats().ringUnderruns) -
                          baselineRingUnder_;
    return static_cast<int>(stream_->captureStats().xruns +
                            stream_->playbackStats().xruns) +
           std::max(0, ringOver) + std::max(0, ringUnder);
  }

 private:
  std::vector<int> channelsFromMask(const juce::BigInteger& mask) const {
    std::vector<int> channels;
    for (int i = 0; i < wireChannels_; ++i)
      if (mask[i]) channels.push_back(i);
    if (channels.empty())
      for (int i = 0; i < wireChannels_; ++i) channels.push_back(i);
    return channels;
  }

  static juce::BigInteger maskFromChannels(const std::vector<int>& channels) {
    juce::BigInteger mask;
    for (int ch : channels) mask.setBit(ch);
    return mask;
  }

  int ringTargetFrames() const {
    const size_t ring = stream_ != nullptr ? static_cast<size_t>(std::max(128, 2 * bufferFrames_))
                                           : 128;
    return static_cast<int>(ring / 2);
  }

  void process(const float* in, float* out, int frames, int /*channels*/) override {
    const int inChannels = static_cast<int>(inputMap_.size());
    const int outChannels = static_cast<int>(outputMap_.size());
    for (int ch = 0; ch < inChannels; ++ch) {
      const int wire = inputMap_[static_cast<size_t>(ch)];
      auto* dst = inputBuffer_.getWritePointer(ch);
      for (int f = 0; f < frames; ++f) dst[f] = in[f * wireChannels_ + wire];
    }
    for (int ch = 0; ch < outChannels; ++ch) outputBuffer_.clear(ch, 0, frames);

    if (auto* callback = callback_.load()) {
      const auto callbackStart = std::chrono::steady_clock::now();
      std::array<const float*, kMaxWireChannels> ins{};
      std::array<float*, kMaxWireChannels> outs{};
      for (int ch = 0; ch < inChannels; ++ch) ins[static_cast<size_t>(ch)] =
          inputBuffer_.getReadPointer(ch);
      for (int ch = 0; ch < outChannels; ++ch) outs[static_cast<size_t>(ch)] =
          outputBuffer_.getWritePointer(ch);
      juce::AudioIODeviceCallbackContext context;
      callback->audioDeviceIOCallbackWithContext(
          ins.data(), inChannels, outs.data(), outChannels, frames, context);
      const double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - callbackStart)
                            .count();
      callbackMaxMs_ = std::max(callbackMaxMs_, ms);
    }
    callbackMinFrames_ = std::min(callbackMinFrames_, frames);
    callbackMaxFrames_ = std::max(callbackMaxFrames_, frames);

    for (int f = 0; f < frames; ++f) {
      for (int wire = 0; wire < wireChannels_; ++wire) {
        int source = -1;
        for (int ch = 0; ch < outChannels; ++ch)
          if (outputMap_[static_cast<size_t>(ch)] == wire) {
            source = ch;
            break;
          }
        out[f * wireChannels_ + wire] =
            source >= 0 ? outputBuffer_.getSample(source, f) : 0.0f;
      }
    }
  }

  // Per-log-window callback diagnostics, touched only on the pump thread
  // (process() runs inside pumpOnce): the frames the pacer handed the DSP and
  // the longest JUCE callback, against the block's real-time budget.
  void resetCallbackWindow() {
    callbackMinFrames_ = std::numeric_limits<int>::max();
    callbackMaxFrames_ = 0;
    callbackMaxMs_ = 0.0;
  }

  void pumpLoop() {
    // SCHED_FIFO is opt-in only (plan risk table); JUCE 9 has no portable
    // helper and the kernel may deny it, so the pump runs at normal priority
    // and the ring/URB depth absorb scheduling jitter.
    auto lastLog = std::chrono::steady_clock::now();
    resetCallbackWindow();
    while (!stopRequested_.load()) {
      bool progressed = false;
      for (int i = 0; i < 16 && !stopRequested_.load(); ++i) {
        if (!stream_->pumpOnce(0)) break;
        progressed = true;
      }
      if (!progressed) {
        if (!stream_->pumpOnce(50) && !stream_->running()) break;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now - lastLog > std::chrono::seconds(10)) {
        lastLog = now;
        juce::Logger::writeToLog(
            "USB Direct: xruns=" + juce::String(static_cast<int>(
                                       stream_->captureStats().xruns +
                                       stream_->playbackStats().xruns)) +
            " ringOver=" + juce::String(static_cast<int>(
                                 stream_->captureStats().ringOverruns)) +
            " ringUnder=" + juce::String(static_cast<int>(
                                  stream_->playbackStats().ringUnderruns)) +
            " drift=" + juce::String(stream_->captureStats().driftPpm, 1) + " ppm" +
            " fb=" + juce::String(stream_->feedbackStats().feedbackValue, 5) +
            " frames=" + juce::String(callbackMaxFrames_ > 0 ? callbackMinFrames_ : 0) + ".." +
            juce::String(callbackMaxFrames_) +
            " cbMax=" + juce::String(callbackMaxMs_, 2) + " ms (budget " +
            juce::String(1000.0 * bufferFrames_ / sampleRate_, 2) + " ms)");
        resetCallbackWindow();
      }
    }
  }

  StreamConfig config_;
  std::unique_ptr<UsbfsTransport> transport_;
  std::unique_ptr<UsbAudioStream> stream_;
  std::thread pump_;
  int callbackMinFrames_ = std::numeric_limits<int>::max();
  int callbackMaxFrames_ = 0;
  double callbackMaxMs_ = 0.0;
  std::atomic<bool> stopRequested_{false};
  std::atomic<juce::AudioIODeviceCallback*> callback_{nullptr};

  std::vector<int> inputMap_{0, 1};
  std::vector<int> outputMap_{0, 1};
  juce::AudioBuffer<float> inputBuffer_;
  juce::AudioBuffer<float> outputBuffer_;
  int wireChannels_ = kDefaultWireChannels;
  int bufferFrames_ = 48;
  int packetsPerUrb_ = 8;   // 1 ms at high speed; the selected URB period
  int maxFrames_ = 50;
  double sampleRate_ = 48000.0;
  int fd_ = -1;
  int baselineRingUnder_ = 0;
  int baselineRingOver_ = 0;
  bool open_ = false;
  bool playing_ = false;
  juce::String lastError_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Device type

UsbDirectAudioIODeviceType::UsbDirectAudioIODeviceType()
    : juce::AudioIODeviceType(kTypeName) {}

void UsbDirectAudioIODeviceType::scanForDevices() {
  available_ = javaDeviceAvailable();
  deviceName_ = available_ ? javaDeviceName() : juce::String();
  if (!available_) {
    // Keep a stable name so a remembered "USB Direct" setup still matches
    // when the device is replugged.
    deviceName_ = "USB audio device";
  }
}

juce::StringArray UsbDirectAudioIODeviceType::getDeviceNames(bool) const {
  return available_ ? juce::StringArray(deviceName_) : juce::StringArray();
}

int UsbDirectAudioIODeviceType::getDefaultDeviceIndex(bool) const { return 0; }

int UsbDirectAudioIODeviceType::getIndexOfDevice(juce::AudioIODevice* device, bool) const {
  return device != nullptr && device->getTypeName() == getTypeName() ? 0 : -1;
}

bool usbDirectKernelDriverBound() { return javaKernelDriverBound(); }

juce::AudioIODevice* UsbDirectAudioIODeviceType::createDevice(
    const juce::String& outputDeviceName, const juce::String& inputDeviceName) {
  if (!available_ || (outputDeviceName.isEmpty() && inputDeviceName.isEmpty()))
    return nullptr;
  return new UsbDirectAudioIODevice(deviceName_);
}

}  // namespace t3k

// ---------------------------------------------------------------------------
// JNI: the quirk registry lookup the Java shell uses before claiming.

extern "C" JNIEXPORT jboolean JNICALL
Java_com_TONE3000_TONE3000_UsbDirectBridge_nativeQuirkIgnoresInterface(
    JNIEnv*, jclass, jint vid, jint pid, jint iface) {
  const auto* quirk = t3k::usb::UsbQuirks::instance().find(static_cast<uint16_t>(vid),
                                                           static_cast<uint16_t>(pid));
  if (quirk == nullptr || iface < 0 || iface >= 32) return JNI_FALSE;
  return (quirk->ignoreInterfaces & (1u << iface)) != 0 ? JNI_TRUE : JNI_FALSE;
}

// JNI: is snd-usb-audio still bound to the AudioControl interface? The
// settings guard uses this to refuse an Oboe open that would otherwise block
// inside the audio HAL (ANR 2026-10-19) until the AMS-22 is replugged.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_TONE3000_TONE3000_UsbDirectBridge_nativeKernelDriverBound(JNIEnv*, jclass, jint fd) {
  if (fd < 0) return JNI_FALSE;
  t3k::usb::android::UsbfsTransport transport(fd);
  return transport.getDriver(0).empty() ? JNI_FALSE : JNI_TRUE;
}

#endif  // JUCE_ANDROID
