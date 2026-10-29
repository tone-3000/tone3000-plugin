// Probe native half (plan M1.2-M1.6): the raw usbfs transport
// (USBDEVFS_SUBMITURB / REAPURB / DISCARDURB / CONTROL / SETINTERFACE /
// GETDRIVER on the fd from UsbDeviceConnection.getFileDescriptor) plus the
// probe modes (dump / stream / rtl / sweep) driving plugin/usb.
//
// The transport maps t3k::usb::Urb onto the kernel's usbdevfs_urb (with the
// iso_frame_desc flexible array worked around by a fixed-size tail array;
// the UAPI struct is a stable ABI). Host-testable pieces live in plugin/usb;
// everything here is glue.
//
// Logging: one-line structured records under the T3KUSB tag so the iteration
// loop can be `adb logcat -s T3KUSB` (plan section 4, L1).

#include <jni.h>

#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ClockModel.h"
#include "PcmConvert.h"
#include "RtlMeasurement.h"
#include "UacControl.h"
#include "UacDescriptors.h"
#include "UsbAudioStream.h"
#include "UsbQuirks.h"

#ifdef __ANDROID__
#include <android/log.h>
#define PROBE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "T3KUSB", __VA_ARGS__)
#else
#define PROBE_LOG(...) do { std::printf("T3KUSB "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;

using t3k::usb::CtrlRequest;
using t3k::usb::StreamConfig;
using t3k::usb::Urb;
using t3k::usb::UrbTransport;
using t3k::usb::UsbAudioStream;

// ---------------------------------------------------------------------------
// usbfs transport

constexpr int kMaxIsoPackets = 64;

// usbdevfs_urb ends in the flexible array iso_frame_desc[]; C++ refuses to
// embed such a struct mid-struct, so allocate the packet descriptors in the
// same block right after it (the libusb idiom; the UAPI layout is a stable
// ABI, so `urb + 1` is the first packet descriptor).
struct KernelUrb {
  explicit KernelUrb(size_t packets) {
    const size_t bytes =
        sizeof(usbdevfs_urb) + packets * sizeof(usbdevfs_iso_packet_desc);
    storage = ::operator new(bytes);
    std::memset(storage, 0, bytes);
    urb = static_cast<usbdevfs_urb*>(storage);
    urb->number_of_packets = static_cast<int>(packets);
  }
  ~KernelUrb() { ::operator delete(storage); }
  usbdevfs_iso_packet_desc* packets() {
    return reinterpret_cast<usbdevfs_iso_packet_desc*>(urb + 1);
  }
  void* storage;
  usbdevfs_urb* urb;
};

class UsbfsTransport : public UrbTransport {
 public:
  explicit UsbfsTransport(int fd) : fd_(fd) {}

  bool submit(Urb* urb) override {
    KernelUrb* k = kernelFor(urb, true);
    if (k == nullptr) return false;
    // Map our packet view onto the kernel's.
    k->urb->type = USBDEVFS_URB_TYPE_ISO;
    k->urb->endpoint = urb->endpoint;
    k->urb->buffer = urb->buffer.data();
    k->urb->buffer_length = static_cast<int>(urb->buffer.size());
    k->urb->number_of_packets = static_cast<int>(urb->packetLengths.size());
    k->urb->usercontext = urb;
    k->urb->status = 0;
    k->urb->actual_length = 0;
    usbdevfs_iso_packet_desc* packets = k->packets();
    for (size_t p = 0; p < urb->packetLengths.size() && p < kMaxIsoPackets; ++p) {
      packets[p].length = urb->packetLengths[p];
      packets[p].actual_length = 0;
      packets[p].status = 0;
    }
    if (::ioctl(fd_, USBDEVFS_SUBMITURB, k->urb) < 0) {
      PROBE_LOG("err: SUBMITURB ep=0x%02x errno=%d", urb->endpoint, errno);
      return false;
    }
    return true;
  }

  Urb* reap(int timeoutMs) override {
    // Non-blocking reap in a short sleep loop. poll() looked cheaper, but on
    // this usbfs fd POLLOUT is the "submit has room" hint and is always
    // ready, so poll() spins; POLLIN-only never wakes. The M1/M2 runs used
    // this loop and sustained 1 ms URBs; keep it, with a finer sleep.
    usbdevfs_urb* k = nullptr;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    for (;;) {
      k = nullptr;
      if (::ioctl(fd_, USBDEVFS_REAPURBNDELAY, &k) == 0 && k != nullptr) {
        Urb* urb = static_cast<Urb*>(k->usercontext);
        urb->status = k->status;
        urb->startFrame = k->start_frame;
        usbdevfs_iso_packet_desc* packets =
            reinterpret_cast<usbdevfs_iso_packet_desc*>(k + 1);
        for (int p = 0; p < k->number_of_packets && p < kMaxIsoPackets; ++p) {
          if (p < static_cast<int>(urb->packetActual.size())) {
            urb->packetActual[p] = static_cast<uint32_t>(packets[p].actual_length);
            urb->packetStatus[p] = packets[p].status;
          }
        }
        return urb;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // Fatal for the fd: the device is gone (unplug gives ENOTTY/ENODEV/
        // ESHUTDOWN depending on the kernel). Report it once and stop.
        if (!deviceGone_) {
          deviceGone_ = true;
          PROBE_LOG("stream: device gone (REAPURB errno=%d)", errno);
        }
        return nullptr;
      }
      if (std::chrono::steady_clock::now() >= deadline) return nullptr;
      struct timespec ts = {0, 100 * 1000};
      nanosleep(&ts, nullptr);
    }
  }

  void discard(Urb* urb) override {
    KernelUrb* k = kernelFor(urb, false);
    if (k == nullptr) return;
    if (::ioctl(fd_, USBDEVFS_DISCARDURB, k->urb) < 0 && errno != EINVAL) {
      PROBE_LOG("err: DISCARDURB errno=%d", errno);
    }
  }

  /** USBDEVFS_GETDRIVER: which kernel driver held an interface before we
      claimed it (plan M1.3). */
  std::string getDriver(int interfaceNumber) {
    usbdevfs_getdriver drv{};
    drv.interface = static_cast<unsigned int>(interfaceNumber);
    if (::ioctl(fd_, USBDEVFS_GETDRIVER, &drv) < 0) return "";
    return std::string(drv.driver);
  }

  /** Class/standard control transfer (plan section 6.2). */
  bool control(const CtrlRequest& req, std::vector<uint8_t>& response) {
    usbdevfs_ctrltransfer ctrl{};
    ctrl.bRequestType = req.bmRequestType;
    ctrl.bRequest = req.bRequest;
    ctrl.wValue = req.wValue;
    ctrl.wIndex = req.wIndex;
    ctrl.wLength = req.wLength;
    ctrl.timeout = 1000;
    uint8_t buf[64] = {};
    if ((req.bmRequestType & 0x80) != 0) {
      ctrl.data = buf;
    } else {
      std::memcpy(buf, req.data, sizeof(buf));
      ctrl.data = buf;
    }
    const int n = ::ioctl(fd_, USBDEVFS_CONTROL, &ctrl);
    if (n < 0) {
      PROBE_LOG("err: CONTROL type=0x%02x req=0x%02x errno=%d", req.bmRequestType,
                req.bRequest, errno);
      return false;
    }
    response.assign(buf, buf + n);
    return true;
  }

  /** A fresh control-path session can reuse the same fd after the previous
      stream quiesced on a transient ioctl error. */
  void clearDeviceGone() { deviceGone_ = false; }

  /** SET_INTERFACE via the dedicated ioctl (start/stop a stream). */
  bool setInterface(int interfaceNumber, int altSetting) {
    usbdevfs_setinterface set{};
    set.interface = static_cast<unsigned int>(interfaceNumber);
    set.altsetting = static_cast<unsigned int>(altSetting);
    if (::ioctl(fd_, USBDEVFS_SETINTERFACE, &set) < 0) {
      PROBE_LOG("err: SETINTERFACE if=%d alt=%d errno=%d", interfaceNumber, altSetting,
                errno);
      return false;
    }
    return true;
  }

 private:
  KernelUrb* kernelFor(Urb* urb, bool create) {
    auto it = map_.find(urb);
    if (it != map_.end()) return it->second.get();
    if (!create) return nullptr;
    auto k = std::make_unique<KernelUrb>(std::max<size_t>(urb->packetLengths.size(), 1));
    KernelUrb* raw = k.get();
    map_[urb] = std::move(k);
    return raw;
  }

  bool deviceGone() const override { return deviceGone_; }

  int fd_;
  bool deviceGone_ = false;
  std::map<Urb*, std::unique_ptr<KernelUrb>> map_;
};

// ---------------------------------------------------------------------------
// Session: one stream at a time on one fd.

struct Session {
  std::unique_ptr<UsbfsTransport> transport;
  std::unique_ptr<UsbAudioStream> stream;
  std::thread pump;
  std::atomic<bool> stopRequested{false};
  std::mutex mutex;
  StreamConfig config;
};

Session g_session;

// Capture-ring size in frames. The pacer holds half of it as its target
// fill, so this is a direct latency term: 512 frames is 10.7 ms of ring,
// 5.3 ms of which the pacer deliberately keeps. M2.4 tunes it; the probe
// can change it per run (nativeConfigureRing).
int g_ringFrames = 512;

// The stream callback: a sine for stream mode, and (rtl mode) the probe burst
// once per trial with the input captured into a log for correlation.
struct ProbeCallback : t3k::usb::UsbAudioStream::Callback {
  std::atomic<int> mode{0};  // 0 = sine, 1 = rtl burst
  double phase = 0.0;
  double rate = 48000.0;
  std::vector<float> burst;
  size_t burstPos = 0;
  bool burstActive = false;
  std::vector<float> captureLog;
  std::mutex logMutex;
  // Log the input while playing the steady sine (AMS loopback ON), so the
  // playback can be inspected digitally, before the DAC.
  std::atomic<bool> logCapture{false};

  void process(const float* in, float* out, int frames, int channels) override {
    if (mode.load() == 1 || logCapture.load()) {
      std::lock_guard<std::mutex> lock(logMutex);
      for (int f = 0; f < frames; ++f) {
        captureLog.push_back(in[static_cast<size_t>(f) * channels]);
      }
    }
    for (int f = 0; f < frames; ++f) {
      float sample = 0.0f;
      if (mode.load() == 0) {
        sample = 0.25f * static_cast<float>(std::sin(phase));
        phase += 2.0 * kPi * 440.0 / rate;
        if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
      } else if (burstActive && burstPos < burst.size()) {
        sample = burst[burstPos++];
        if (burstPos >= burst.size()) burstActive = false;
      }
      for (int c = 0; c < channels; ++c) out[static_cast<size_t>(f) * channels + c] = sample;
    }
  }
};

ProbeCallback g_callback;

// Extract the descriptor bytes from a Java array (empty = caller has none).
std::vector<uint8_t> bytesFrom(JNIEnv* env, jbyteArray raw) {
  if (raw == nullptr) return {};
  jsize len = env->GetArrayLength(raw);
  std::vector<uint8_t> out(static_cast<size_t>(len));
  env->GetByteArrayRegion(raw, 0, len, reinterpret_cast<jbyte*>(out.data()));
  return out;
}

StreamConfig configFrom(int rate, int channels, int subslot, const uint8_t* raw,
                        size_t rawLen, int vid, int pid) {
  StreamConfig cfg;
  cfg.sampleRate = static_cast<uint32_t>(rate);
  cfg.channels = static_cast<uint8_t>(channels);
  cfg.subslotBytes = static_cast<uint8_t>(subslot);
  cfg.bitResolution = 24;
  t3k::usb::ConfigTree tree;
  if (raw != nullptr && t3k::usb::parseConfig(raw, rawLen, tree)) {
    cfg.uac = tree.uacVersion();
    for (const auto& alt : tree.altSettings) {
      if (alt.isAudioControl()) {
        cfg.controlInterface = alt.interfaceNumber;
        break;
      }
    }
    const t3k::usb::AltSetting* play = tree.findAlt(true, cfg.sampleRate, cfg.channels);
    const t3k::usb::AltSetting* cap = tree.findAlt(false, cfg.sampleRate, cfg.channels);
    if (play != nullptr) {
      cfg.playbackEndpoint = play->dataEndpoint()->address;
      cfg.playbackInterface = play->interfaceNumber;
      cfg.playbackAltsetting = play->altSetting;
      if (const auto* fb = play->feedbackEndpoint()) cfg.feedbackEndpoint = fb->address;
      if (play->format.subslotBytes != 0) cfg.subslotBytes = play->format.subslotBytes;
    }
    if (cap != nullptr) {
      cfg.captureEndpoint = cap->dataEndpoint()->address;
      cfg.captureInterface = cap->interfaceNumber;
      cfg.captureAltsetting = cap->altSetting;
    }
    if (const t3k::usb::Entity* clock = tree.rateClockSource()) {
      cfg.clockSourceId = clock->id;
    }
  }
  // UAC2 explicit feedback is 16.16 in 4 bytes; UAC1's is 10.14 in 3.
  cfg.feedback16_16 = cfg.uac == t3k::usb::UacVersion::uac2;
  cfg.feedbackBytes = cfg.feedback16_16 ? 4 : 3;
  const t3k::usb::DeviceQuirk* quirk =
      t3k::usb::UsbQuirks::instance().find(static_cast<uint16_t>(vid),
                                           static_cast<uint16_t>(pid));
  if (quirk != nullptr && quirk->forceSubslot4) cfg.subslotBytes = 4;
  return cfg;
}

void pumpThreadMain() {
  auto lastLog = std::chrono::steady_clock::now();
  while (!g_session.stopRequested.load()) {
    // Drain every ready completion before sleeping. Reaping one URB per
    // 100 us tick let capture completions crowd out playback (measured
    // 991 vs 978 URBs/s screen-off), which slowly overran the small ring.
    bool progressed = false;
    for (int i = 0; i < 16 && !g_session.stopRequested.load(); ++i) {
      if (!g_session.stream->pumpOnce(0)) break;
      progressed = true;
    }
    if (!progressed) {
      if (!g_session.stream->pumpOnce(50) && !g_session.stream->running()) {
        break;  // device gone: exit cleanly
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - lastLog > std::chrono::seconds(5)) {
      lastLog = now;
      const auto& cap = g_session.stream->captureStats();
      const auto& play = g_session.stream->playbackStats();
      PROBE_LOG("stream: capUrb=%llu capPkts=%llu playUrb=%llu playPkts=%llu xruns=%llu "
                "bytes=%llu drift=%.1f ringOver=%llu ringUnder=%llu",
                (unsigned long long)cap.urbsReaped, (unsigned long long)cap.packets,
                (unsigned long long)play.urbsReaped, (unsigned long long)play.packets,
                (unsigned long long)(cap.xruns + play.xruns),
                (unsigned long long)cap.bytesTransferred, cap.driftPpm,
                (unsigned long long)cap.ringOverruns,
                (unsigned long long)play.ringUnderruns);
      // Packet-level detail for the xrun budget: how far actual lengths
      // stray from nominal and how big the URB frame steps are.
      PROBE_LOG("stream: capBytesPerUrb=%llu playBytesPerUrb=%llu sfCap=%lld stepCap=%d "
                "sfPlay=%lld stepPlay=%d",
                (unsigned long long)(cap.bytesTransferred /
                                     std::max<uint64_t>(cap.urbsReaped, 1)),
                (unsigned long long)(play.bytesTransferred /
                                     std::max<uint64_t>(play.urbsReaped, 1)),
                (long long)cap.lastStartFrame, cap.startFrameStep,
                (long long)play.lastStartFrame, play.startFrameStep);
      const auto& fb = g_session.stream->feedbackStats();
      if (g_session.config.feedbackEndpoint != 0) {
        PROBE_LOG("stream: fbUrb=%llu fbPkts=%llu fbErr=%llu fbVal=%.5f fbDrift=%.1f",
                  (unsigned long long)fb.urbsReaped,
                  (unsigned long long)fb.feedbackPackets,
                  (unsigned long long)fb.feedbackErrors, fb.feedbackValue,
                  fb.driftPpm);
      }
      PROBE_LOG("stream: submitFail=%llu submitRetry=%llu",
                (unsigned long long)(cap.submitFailures + play.submitFailures),
                (unsigned long long)(cap.submitRetries + play.submitRetries));
    }
  }
}

// Rate first, then SET_INTERFACE (plan pitfall 2), with a GET_CUR readback
// where the device supports it (M2.3 requires the readback).
std::string applyRateAndInterfaces(UsbfsTransport& transport, const StreamConfig& cfg,
                                   std::string& detail) {
  if (cfg.uac == t3k::usb::UacVersion::uac2) {
    std::vector<uint8_t> resp;
    if (!transport.control(t3k::usb::uac::uac2SetSampleRate(
                               cfg.controlInterface, cfg.clockSourceId, cfg.sampleRate),
                           resp)) {
      return "SET_CUR sample rate failed";
    }
    std::vector<uint8_t> cur;
    if (transport.control(t3k::usb::uac::uac2GetSampleRate(cfg.controlInterface,
                                                           cfg.clockSourceId),
                          cur) &&
        cur.size() >= 4) {
      const uint32_t got = static_cast<uint32_t>(cur[0]) |
                           (static_cast<uint32_t>(cur[1]) << 8) |
                           (static_cast<uint32_t>(cur[2]) << 16) |
                           (static_cast<uint32_t>(cur[3]) << 24);
      detail = "readback=" + std::to_string(got);
      if (got != cfg.sampleRate) return "rate readback mismatch";
    } else {
      detail = "readback=unavailable";
    }
  } else {
    std::vector<uint8_t> resp;
    transport.control(
        t3k::usb::uac::uac1SetSampleRate(cfg.playbackEndpoint, cfg.sampleRate), resp);
    detail = "readback=uac1-unverified";
  }
  if (!transport.setInterface(cfg.playbackInterface, cfg.playbackAltsetting)) {
    return "SET_INTERFACE playback failed";
  }
  if (cfg.captureEndpoint != 0 &&
      !transport.setInterface(cfg.captureInterface, cfg.captureAltsetting)) {
    return "SET_INTERFACE capture failed";
  }
  return "";
}

// Assumes g_session.mutex is held and the transport exists. Sets the rate,
// selects the altsettings, starts the URB machine and the pump thread.
std::string startStreamLocked(const StreamConfig& cfg, int packetsPerUrb,
                              int urbsInFlight, std::string& detail) {
  UsbfsTransport* transport = g_session.transport.get();
  const std::string err = applyRateAndInterfaces(*transport, cfg, detail);
  if (!err.empty()) return err;

  t3k::usb::UsbAudioStream::Options opts;
  opts.packetsPerUrb = packetsPerUrb;
  opts.urbsInFlight = urbsInFlight;
  opts.ringFrames = static_cast<size_t>(g_ringFrames);
  opts.nowNs = [] {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  };
  g_session.config = cfg;
  g_session.stream = std::make_unique<t3k::usb::UsbAudioStream>(
      cfg, opts, *transport, g_callback);
  if (!g_session.stream->start()) {
    g_session.stream.reset();
    return "URB submission failed";
  }
  g_session.stopRequested = false;
  g_session.pump = std::thread(pumpThreadMain);
  PROBE_LOG("stream: start rate=%u ch=%u subslot=%u playback=0x%02x capture=0x%02x fb=0x%02x",
            cfg.sampleRate, cfg.channels, cfg.subslotBytes, cfg.playbackEndpoint,
            cfg.captureEndpoint, cfg.feedbackEndpoint);
  return "";
}

std::string openStream(int fd, const StreamConfig& cfg, int packetsPerUrb,
                       int urbsInFlight) {
  std::lock_guard<std::mutex> lock(g_session.mutex);
  if (g_session.stream != nullptr) return "already running";
  g_session.transport = std::make_unique<UsbfsTransport>(fd);
  std::string detail;
  return startStreamLocked(cfg, packetsPerUrb, urbsInFlight, detail);
}

// Full stop/start across a rate change (plan M2.3): stop the stream, return
// both endpoints to altsetting 0, SET_CUR the new rate, verify GET_CUR,
// select the altsettings again and restart the pump.
std::string changeRate(int fd, const StreamConfig& requested, int rate, int packetsPerUrb,
                       int urbsInFlight) {
  std::lock_guard<std::mutex> lock(g_session.mutex);
  if (g_session.stream == nullptr) return "not running";
  g_session.stopRequested = true;
  if (g_session.pump.joinable()) g_session.pump.join();
  g_session.stream->stop();
  g_session.stream.reset();

  UsbfsTransport* transport = g_session.transport.get();
  transport->setInterface(g_session.config.playbackInterface, 0);
  transport->setInterface(g_session.config.captureInterface, 0);
  transport->clearDeviceGone();

  StreamConfig cfg = requested;
  cfg.sampleRate = static_cast<uint32_t>(rate);
  std::string detail;
  const std::string err = startStreamLocked(cfg, packetsPerUrb, urbsInFlight, detail);
  if (!err.empty()) {
    PROBE_LOG("rate: switch to %d failed: %s", rate, err.c_str());
    return err;
  }
  PROBE_LOG("rate: switch to %d ok (%s)", rate, detail.c_str());
  return "rate=" + std::to_string(rate) + " " + detail;
}

std::string closeStream() {
  std::lock_guard<std::mutex> lock(g_session.mutex);
  if (g_session.stream == nullptr) return "not running";
  g_session.stopRequested = true;
  if (g_session.pump.joinable()) g_session.pump.join();
  const t3k::usb::StreamStats cap = g_session.stream->captureStats();
  const t3k::usb::StreamStats play = g_session.stream->playbackStats();
  g_session.stream->stop();
  g_session.stream.reset();
  // Return to altsetting 0 or some devices stop responding (plan pitfall 2).
  UsbfsTransport* transport = g_session.transport.get();
  transport->setInterface(g_session.config.playbackInterface, 0);
  transport->setInterface(g_session.config.captureInterface, 0);
  PROBE_LOG("stream: stop xruns=%llu drift=%.1f ppm",
            (unsigned long long)(cap.xruns + play.xruns), cap.driftPpm);
  g_session.transport.reset();
  return "";
}

// One-line snapshot for the adb scenario driver (script/usb-scenarios.sh):
// counters only, no parsing of the 5 s log cadence.
std::string statusLine() {
  std::lock_guard<std::mutex> lock(g_session.mutex);
  if (g_session.stream == nullptr) {
    char idle[64];
    std::snprintf(idle, sizeof(idle), "status: running=0 rate=%u",
                  g_session.config.sampleRate);
    return idle;
  }
  const auto& cap = g_session.stream->captureStats();
  const auto& play = g_session.stream->playbackStats();
  const auto& fb = g_session.stream->feedbackStats();
  char line[512];
  std::snprintf(
      line, sizeof(line),
      "status: running=%d rate=%u capUrb=%llu capPkts=%llu playUrb=%llu playPkts=%llu "
      "xruns=%llu ringOver=%llu ringUnder=%llu submitFail=%llu submitRetry=%llu "
      "fbErr=%llu fbVal=%.5f drift=%.1f",
      g_session.stream->running() ? 1 : 0, g_session.config.sampleRate,
      (unsigned long long)cap.urbsReaped, (unsigned long long)cap.packets,
      (unsigned long long)play.urbsReaped, (unsigned long long)play.packets,
      (unsigned long long)(cap.xruns + play.xruns),
      (unsigned long long)cap.ringOverruns,
      (unsigned long long)play.ringUnderruns,
      (unsigned long long)(cap.submitFailures + play.submitFailures),
      (unsigned long long)(cap.submitRetries + play.submitRetries),
      (unsigned long long)fb.feedbackErrors, fb.feedbackValue, cap.driftPpm);
  return line;
}

}  // namespace

// ---------------------------------------------------------------------------
// JNI

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeGetDriver(JNIEnv* env, jclass,
                                                        jint fd, jint iface) {
  UsbfsTransport transport(fd);
  return env->NewStringUTF(transport.getDriver(iface).c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeQuirkIgnoresInterface(JNIEnv*, jclass,
                                                                    jint vid, jint pid,
                                                                    jint iface) {
  const t3k::usb::DeviceQuirk* q = t3k::usb::UsbQuirks::instance().find(
      static_cast<uint16_t>(vid), static_cast<uint16_t>(pid));
  if (q == nullptr || iface < 0 || iface >= 32) return JNI_FALSE;
  return (q->ignoreInterfaces & (1u << iface)) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeDump(JNIEnv* env, jclass,
                                                   jbyteArray rawDescriptors, jint vid,
                                                   jint pid) {
  (void)vid;
  (void)pid;
  jsize len = env->GetArrayLength(rawDescriptors);
  std::vector<uint8_t> raw(static_cast<size_t>(len));
  env->GetByteArrayRegion(rawDescriptors, 0, len,
                          reinterpret_cast<jbyte*>(raw.data()));
  t3k::usb::ConfigTree tree;
  std::string out;
  if (!t3k::usb::parseConfig(raw.data(), raw.size(), tree)) {
    out = "parse: failed (truncated or not a config descriptor)";
  } else {
    out = tree.describe();
  }
  PROBE_LOG("dump: %zu bytes, %zu altsettings, %zu entities", raw.size(),
            tree.altSettings.size(), tree.entities.size());
  return env->NewStringUTF(out.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeStart(JNIEnv* env, jclass, jint fd,
                                                    jbyteArray rawDescriptors, jint vid,
                                                    jint pid, jint rate, jint channels,
                                                    jint subslot, jint packetsPerUrb,
                                                    jint urbsInFlight) {
  const std::vector<uint8_t> raw = bytesFrom(env, rawDescriptors);
  StreamConfig cfg =
      configFrom(rate, channels, subslot, raw.data(), raw.size(), vid, pid);
  g_callback.mode = 0;
  g_callback.rate = rate;
  std::string err = openStream(fd, cfg, packetsPerUrb, urbsInFlight);
  return env->NewStringUTF(err.c_str());
}

JNIEXPORT jfloatArray JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeCaptureSine(
    JNIEnv* env, jclass, jint fd, jbyteArray rawDescriptors, jint vid, jint pid,
    jint rate, jint channels, jint subslot, jint packetsPerUrb, jint urbsInFlight,
    jint millis) {
  const std::vector<uint8_t> raw = bytesFrom(env, rawDescriptors);
  StreamConfig cfg =
      configFrom(rate, channels, subslot, raw.data(), raw.size(), vid, pid);
  g_callback.mode = 0;  // steady sine
  g_callback.rate = rate;
  g_callback.logCapture = true;
  {
    std::lock_guard<std::mutex> lock(g_callback.logMutex);
    g_callback.captureLog.clear();
  }
  const std::string err = openStream(fd, cfg, packetsPerUrb, urbsInFlight);
  if (!err.empty()) {
    g_callback.logCapture = false;
    return nullptr;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, millis)));
  std::vector<float> capture;
  {
    std::lock_guard<std::mutex> lock(g_callback.logMutex);
    capture = g_callback.captureLog;
  }
  closeStream();
  g_callback.logCapture = false;
  jfloatArray out = env->NewFloatArray(static_cast<jsize>(capture.size()));
  if (out != nullptr && !capture.empty())
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(capture.size()), capture.data());
  return out;
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeStop(JNIEnv* env, jclass) {
  std::string err = closeStream();
  return env->NewStringUTF(err.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeRtl(JNIEnv* env, jclass, jint fd,
                                                   jbyteArray rawDescriptors, jint vid,
                                                   jint pid, jint rate, jint channels,
                                                   jint subslot, jint packetsPerUrb,
                                                   jint urbsInFlight, jint trials) {
  const std::vector<uint8_t> raw = bytesFrom(env, rawDescriptors);
  StreamConfig cfg =
      configFrom(rate, channels, subslot, raw.data(), raw.size(), vid, pid);
  g_callback.mode = 1;
  g_callback.rate = rate;
  g_callback.burst = t3k::usb::rtl::makeSweep(rate, 2.0, -12.0f);
  std::string err = openStream(fd, cfg, packetsPerUrb, urbsInFlight);
  if (!err.empty()) return env->NewStringUTF(err.c_str());

  std::string rows;
  std::vector<double> rtls;
  double worstRatio = 1e9;
  for (int trial = 0; trial < trials; ++trial) {
    {
      std::lock_guard<std::mutex> lock(g_callback.logMutex);
      g_callback.captureLog.clear();
      g_callback.burstPos = 0;
      g_callback.burstActive = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::vector<float> capture;
    {
      std::lock_guard<std::mutex> lock(g_callback.logMutex);
      capture = g_callback.captureLog;
    }
    const t3k::usb::rtl::FindResult found = t3k::usb::rtl::findBurst(
        capture.data(), capture.size(), g_callback.burst.data(), g_callback.burst.size());
    const double rtlMs =
        found.valid ? static_cast<double>(found.lag) / rate * 1000.0 : -1.0;
    rtls.push_back(rtlMs);
    worstRatio = std::min(worstRatio, found.peakRatio);
    char line[256];
    std::snprintf(line, sizeof(line),
                  "rtl: trial=%d rtlMs=%.3f peakRatio=%.1f lag=%lld", trial + 1, rtlMs,
                  found.peakRatio, (long long)found.lag);
    PROBE_LOG("%s", line);
    rows += line;
    rows += "\n";
  }
  const auto& cap = g_session.stream->captureStats();
  char summary[256];
  std::snprintf(summary, sizeof(summary),
                "rtl: median=%.3f ms over %d trials worstRatio=%.1f xruns=%llu drift=%.1f",
                t3k::usb::rtl::median(rtls), trials, worstRatio,
                (unsigned long long)cap.xruns, cap.driftPpm);
  PROBE_LOG("%s", summary);
  rows += summary;
  closeStream();
  return env->NewStringUTF(rows.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeSweep(JNIEnv* env, jclass, jint fd,
                                                     jbyteArray rawDescriptors, jint vid,
                                                     jint pid, jint rate, jint channels,
                                                     jint subslot) {
  const std::vector<uint8_t> raw = bytesFrom(env, rawDescriptors);
  std::string rows = "sweep: packetsPerUrb,urbsInFlight,rtlMs,peakRatio,xruns,driftPpm\n";
  for (int packets : {1, 2, 4, 8}) {
    for (int urbs : {2, 3, 4}) {
      StreamConfig cfg =
          configFrom(rate, channels, subslot, raw.data(), raw.size(), vid, pid);
      g_callback.mode = 1;
      g_callback.rate = rate;
      g_callback.burst = t3k::usb::rtl::makeSweep(rate, 2.0, -12.0f);
      std::string err = openStream(fd, cfg, packets, urbs);
      if (!err.empty()) {
        rows += "sweep: " + std::to_string(packets) + "," + std::to_string(urbs) +
                ",error: " + err + "\n";
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(g_callback.logMutex);
        g_callback.captureLog.clear();
        g_callback.burstPos = 0;
        g_callback.burstActive = true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      std::vector<float> capture;
      {
        std::lock_guard<std::mutex> lock(g_callback.logMutex);
        capture = g_callback.captureLog;
      }
      const t3k::usb::rtl::FindResult found = t3k::usb::rtl::findBurst(
          capture.data(), capture.size(), g_callback.burst.data(), g_callback.burst.size());
      const double rtlMs =
          found.valid ? static_cast<double>(found.lag) / rate * 1000.0 : -1.0;
      const auto& cap = g_session.stream->captureStats();
      char line[256];
      std::snprintf(line, sizeof(line), "sweep: %d,%d,%.3f,%.1f,%llu,%.1f", packets, urbs,
                    rtlMs, found.peakRatio, (unsigned long long)cap.xruns,
                    cap.driftPpm);
      rows += line;
      rows += "\n";
      PROBE_LOG("%s", line);
      closeStream();
    }
  }
  return env->NewStringUTF(rows.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeSetRate(
    JNIEnv* env, jclass, jint fd, jbyteArray rawDescriptors, jint vid, jint pid, jint rate,
    jint channels, jint subslot, jint packetsPerUrb, jint urbsInFlight) {
  const std::vector<uint8_t> raw = bytesFrom(env, rawDescriptors);
  const StreamConfig requested =
      configFrom(rate, channels, subslot, raw.data(), raw.size(), vid, pid);
  const std::string result =
      changeRate(fd, requested, rate, packetsPerUrb, urbsInFlight);
  return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeConfigureRing(JNIEnv*, jclass, jint frames) {
  g_ringFrames = std::max(64, std::min(frames, 4096));
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeReset(JNIEnv* env, jclass, jint fd) {
  // USBDEVFS_RESET makes the HCD re-enumerate the device. This is how a
  // session recovers the kernel's snd-usb-audio binding after the probe
  // detached it (a physical replug does the same thing).
  if (::ioctl(fd, USBDEVFS_RESET, 0) < 0) {
    const std::string err = "reset failed: errno=" + std::to_string(errno);
    PROBE_LOG("%s", err.c_str());
    return env->NewStringUTF(err.c_str());
  }
  PROBE_LOG("reset: device re-enumerating");
  return env->NewStringUTF("");
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeStatus(JNIEnv* env, jclass) {
  return env->NewStringUTF(statusLine().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_TONE3000_USBCHECK_NativeBridge_nativeDiscardOne(JNIEnv* env, jclass) {
  std::lock_guard<std::mutex> lock(g_session.mutex);
  const int n = g_session.stream != nullptr ? g_session.stream->discardOneInFlight() : 0;
  return env->NewStringUTF(("discard=" + std::to_string(n)).c_str());
}

}  // extern "C"
