#pragma once

// POD configuration and result types for the USB audio driver core. This
// header (and everything under plugin/usb) is pure C++: no JUCE, no JNI, no
// OS headers, so the whole core builds and tests on the host. The Android
// shell (probe app, then the JUCE device) only moves these structs across
// the boundary.

#include <cstddef>
#include <cstdint>

namespace t3k::usb {

enum class UacVersion : uint8_t { uac1, uac2 };

/** Isochronous transfer speed class of the pipe we schedule against. Sets the
    packet cadence: 8 packets per ms at high speed (125 us microframes), 1 per
    ms at full speed. */
enum class UsbSpeed : uint8_t { full, high };

/**
 * Everything needed to run one audio stream direction. Populated from the
 * descriptor parser (UacDescriptors) and the probe's device dump, then
 * consumed by UsbAudioStream and the control path.
 */
struct StreamConfig {
  UacVersion uac = UacVersion::uac2;
  UsbSpeed speed = UsbSpeed::high;
  uint32_t sampleRate = 48000;
  uint8_t channels = 2;
  /** Bytes per sample slot on the wire: 3 = packed 24-bit LE, 4 = 32-bit slot
      with bitResolution significant bits. Read from bSubslotSize; never
      assume (the plan's pitfall 3). */
  uint8_t subslotBytes = 3;
  /** bBitResolution from the Type I format descriptor (e.g. 24). */
  uint8_t bitResolution = 24;
  /** Data endpoint addresses including the direction bit (0 = absent, so a
      capture-only or playback-only device is expressible). */
  uint8_t playbackEndpoint = 0x02;
  uint8_t captureEndpoint = 0x81;
  /** Explicit feedback endpoint address, or 0 for implicit feedback. */
  uint8_t feedbackEndpoint = 0;
  /** The playback data endpoint is isochronous with sync type Synchronous
      (bmAttributes bits 3:2 = 11): the device clock is locked to the USB
      frame clock, so every packet carries exactly the nominal frame count
      and no pacing trim applies (the Scarlett Solo, 2026-10-28). */
  bool playbackSynchronous = false;
  /** Explicit feedback format: UAC2 uses 16.16 in 4 bytes, UAC1 uses 10.14
      in 3 bytes (plan section 6.1/6.4). Only meaningful when
      feedbackEndpoint != 0. */
  bool feedback16_16 = true;
  uint8_t feedbackBytes = 4;
  /** Active altsetting indices (0 = zero bandwidth) and their interfaces.
      The AMS-22 has one AudioStreaming interface per direction, each with
      its own altsetting and terminal link. */
  uint8_t playbackAltsetting = 1;
  uint8_t captureAltsetting = 1;
  uint8_t playbackInterface = 1;
  uint8_t captureInterface = 2;
  /** UAC2 clock source entity id (wIndex high byte of the rate control).
      Unused for UAC1, which addresses the endpoint instead. */
  uint8_t clockSourceId = 1;
  /** AudioControl interface number (wIndex low byte of class requests). */
  uint8_t controlInterface = 0;

  int bytesPerFrame() const { return channels * subslotBytes; }
  /** Frames per packet at the nominal rate and cadence. */
  int nominalFramesPerPacket(const UsbSpeed speed) const {
    const int packetsPerSecond = (speed == UsbSpeed::high) ? 8000 : 1000;
    return static_cast<int>(sampleRate) / packetsPerSecond;
  }
};

/** Per-direction bookkeeping surfaced to the diagnostics readout and the
    measurement CSV (plan section 4.1). */
struct StreamStats {
  uint64_t urbsSubmitted = 0;
  uint64_t urbsReaped = 0;
  uint64_t packets = 0;
  /** Packets whose actual_length disagreed with the request, carried a
      non-zero status, or whose reap arrived after its deadline. */
  uint64_t xruns = 0;
  uint64_t bytesTransferred = 0;
  /** Clock drift of the device relative to nominal, parts per million
      (filled by ClockModel; positive = device runs fast). */
  double driftPpm = 0.0;
  /** Diagnostics: last URB's start_frame and the observed step between
      consecutive URBs (HCD-defined units; see UsbAudioStream). */
  int64_t lastStartFrame = 0;
  int startFrameStep = 0;
  /** Explicit feedback endpoint bookkeeping (only filled for the feedback
      stats; the value is frames per packet cadence, 0 = none yet). */
  uint64_t feedbackPackets = 0;
  uint64_t feedbackErrors = 0;
  double feedbackValue = 0.0;
  /** Ring policy counters (plan M2.2): output underruns where the last frame
      was repeated, and input overruns where the oldest frames were dropped.
      Cumulative for the stream. */
  uint64_t ringUnderruns = 0;
  uint64_t ringOverruns = 0;
  /** Recovery counters (plan M2.3): rejected submit() attempts, and the
      subset that was retried in place. A transient failure is retried before
      the machine gives up on that URB; the URB then waits in the deferred
      queue instead of shortening the pool permanently. */
  uint64_t submitFailures = 0;
  uint64_t submitRetries = 0;
};

}  // namespace t3k::usb
