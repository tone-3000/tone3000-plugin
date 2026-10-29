#pragma once

// "USB Direct": a juce::AudioIODeviceType backed by our own isochronous
// transport instead of the Android audio stack (plan M3, decisions D3/D4).
// Android-only: the implementation lives in src/UsbDirectAudioDevice.cpp and
// is added to the plugin target only for T3K_ANDROID builds.
//
// Registration happens in StandaloneAudioSettings' constructor, where the
// standalone holder's AudioDeviceManager exists; Oboe stays the default and
// the fallback.

#include <juce_audio_devices/juce_audio_devices.h>

namespace t3k {

// Optional diagnostics that a custom device can expose to the settings UI and
// the log without widening juce::AudioIODevice (the plan's M3.5 honest-latency
// readout). Cast the current device to this interface; generic backends do not
// implement it.
class AudioDeviceDiagnostics {
 public:
  virtual ~AudioDeviceDiagnostics() = default;
  // Measured clock drift in ppm, or 0 when the backend does not estimate it.
  virtual double getDriftPpm() const = 0;
};

// True when snd-usb-audio is still bound to the AudioControl interface (a
// physical replug has rebound it). USB Direct detaches it, and Oboe cannot
// open the device again until it is replugged; the settings guard uses this
// to refuse a transition that would block the audio HAL.
bool usbDirectKernelDriverBound();

class UsbDirectAudioIODeviceType : public juce::AudioIODeviceType {
 public:
  UsbDirectAudioIODeviceType();
  ~UsbDirectAudioIODeviceType() override = default;

  void scanForDevices() override;
  juce::StringArray getDeviceNames(bool wantInputNames = false) const override;
  int getDefaultDeviceIndex(bool forInput) const override;
  int getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const override;
  bool hasSeparateInputsAndOutputs() const override { return false; }
  juce::AudioIODevice* createDevice(const juce::String& outputDeviceName,
                                    const juce::String& inputDeviceName) override;

 private:
  bool available_ = false;
  juce::String deviceName_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UsbDirectAudioIODeviceType)
};

}  // namespace t3k
