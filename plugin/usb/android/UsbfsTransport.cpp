#include "UsbfsTransport.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef __ANDROID__
#include <android/log.h>
#define USB_LOG(...) __android_log_print(ANDROID_LOG_INFO, "T3KUSB", __VA_ARGS__)
#else
#define USB_LOG(...)                        \
  do {                                      \
    std::printf("T3KUSB ");                 \
    std::printf(__VA_ARGS__);               \
    std::printf("\n");                      \
  } while (0)
#endif

namespace t3k::usb::android {

UsbfsTransport::KernelUrb::KernelUrb(size_t packets) {
  const size_t bytes = sizeof(usbdevfs_urb) + packets * sizeof(usbdevfs_iso_packet_desc);
  storage = ::operator new(bytes);
  std::memset(storage, 0, bytes);
  urb = static_cast<usbdevfs_urb*>(storage);
  urb->number_of_packets = static_cast<int>(packets);
}

UsbfsTransport::KernelUrb::~KernelUrb() { ::operator delete(storage); }

usbdevfs_iso_packet_desc* UsbfsTransport::KernelUrb::packets() {
  return reinterpret_cast<usbdevfs_iso_packet_desc*>(urb + 1);
}

UsbfsTransport::KernelUrb* UsbfsTransport::kernelFor(Urb* urb, bool create) {
  auto it = map_.find(urb);
  if (it != map_.end()) return it->second.get();
  if (!create) return nullptr;
  auto k = std::make_unique<KernelUrb>(
      std::max<size_t>(urb->packetLengths.size(), 1));
  KernelUrb* raw = k.get();
  map_[urb] = std::move(k);
  return raw;
}

bool UsbfsTransport::submit(Urb* urb) {
  KernelUrb* k = kernelFor(urb, true);
  if (k == nullptr) return false;
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
    USB_LOG("err: SUBMITURB ep=0x%02x errno=%d", urb->endpoint, errno);
    return false;
  }
  return true;
}

Urb* UsbfsTransport::reap(int timeoutMs) {
  // Non-blocking reap in a short sleep loop. poll() is not usable on this
  // fd: POLLOUT is the "submit has room" hint and is always ready, and
  // POLLIN never wakes (measured on the Tab S10 FE, kernel 6.6). The loop
  // sustains 1 ms URBs; see results-m2-soak.txt.
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
      if (!deviceGone_) {
        deviceGone_ = true;
        USB_LOG("stream: device gone (REAPURB errno=%d)", errno);
      }
      return nullptr;
    }
    if (std::chrono::steady_clock::now() >= deadline) return nullptr;
    struct timespec ts = {0, 100 * 1000};
    nanosleep(&ts, nullptr);
  }
}

void UsbfsTransport::discard(Urb* urb) {
  KernelUrb* k = kernelFor(urb, false);
  if (k == nullptr) return;
  if (::ioctl(fd_, USBDEVFS_DISCARDURB, k->urb) < 0 && errno != EINVAL) {
    USB_LOG("err: DISCARDURB errno=%d", errno);
  }
}

bool UsbfsTransport::control(const CtrlRequest& req, std::vector<uint8_t>& response) {
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
    USB_LOG("err: CONTROL type=0x%02x req=0x%02x errno=%d", req.bmRequestType,
            req.bRequest, errno);
    return false;
  }
  response.assign(buf, buf + n);
  return true;
}

bool UsbfsTransport::setInterface(int interfaceNumber, int altSetting) {
  usbdevfs_setinterface set{};
  set.interface = static_cast<unsigned int>(interfaceNumber);
  set.altsetting = static_cast<unsigned int>(altSetting);
  if (::ioctl(fd_, USBDEVFS_SETINTERFACE, &set) < 0) {
    USB_LOG("err: SETINTERFACE if=%d alt=%d errno=%d", interfaceNumber, altSetting,
            errno);
    return false;
  }
  return true;
}

std::string UsbfsTransport::getDriver(int interfaceNumber) {
  usbdevfs_getdriver drv{};
  drv.interface = static_cast<unsigned int>(interfaceNumber);
  if (::ioctl(fd_, USBDEVFS_GETDRIVER, &drv) < 0) return "";
  return std::string(drv.driver);
}

}  // namespace t3k::usb::android
