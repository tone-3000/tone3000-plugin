#pragma once

// Android-side usbfs transport for the pure-C++ URB machine (plan D1/D2).
// Lives outside plugin/usb's core so the core stays host-buildable; only the
// Android shell compiles this. The probe carries its own copy of the same
// glue (it predates the shared lib); both issue the same ioctls.
//
// The fd comes from UsbDeviceConnection.getFileDescriptor() in the Java
// shell. Ownership of the fd stays with Java: closeDevice() there closes the
// connection after the stream has quiesced.

#include <linux/usbdevice_fs.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "UacControl.h"
#include "UsbAudioStream.h"

namespace t3k::usb::android {

class UsbfsTransport : public UrbTransport {
 public:
  explicit UsbfsTransport(int fd) : fd_(fd) {}

  bool submit(Urb* urb) override;
  Urb* reap(int timeoutMs) override;
  void discard(Urb* urb) override;
  bool deviceGone() const override { return deviceGone_; }

  /** A fresh control-path session can reuse the same fd after a transient
      ioctl error. */
  void clearDeviceGone() { deviceGone_ = false; }

  /** Class/standard control transfer (plan section 6.2). */
  bool control(const CtrlRequest& req, std::vector<uint8_t>& response);

  /** SET_INTERFACE via the dedicated ioctl (start/stop a stream). */
  bool setInterface(int interfaceNumber, int altSetting);

  /** USBDEVFS_GETDRIVER: which kernel driver held an interface. */
  std::string getDriver(int interfaceNumber);

 private:
  static constexpr int kMaxIsoPackets = 64;

  // usbdevfs_urb ends in a flexible array; allocate the packet descriptors
  // in the same block right after it (the libusb idiom).
  struct KernelUrb {
    explicit KernelUrb(size_t packets);
    ~KernelUrb();
    usbdevfs_iso_packet_desc* packets();
    void* storage = nullptr;
    usbdevfs_urb* urb = nullptr;
  };

  KernelUrb* kernelFor(Urb* urb, bool create);

  int fd_;
  bool deviceGone_ = false;
  std::map<Urb*, std::unique_ptr<KernelUrb>> map_;
};

}  // namespace t3k::usb::android
