# Zoom AMS-22: descriptor and entity-graph notes (plan M1.3)

Device under test for the low-latency USB driver. Facts marked **confirmed**
come from `dumpsys usb` on the Galaxy Tab S10 FE (2026-10-17) and the probe
app's enumeration; facts marked **pending** come from the raw descriptor
dump (`am start -e mode dump`), which needs the tablet unlocked for the USB
permission dialog. When that lands, save the blob as
`plugin/usb/test/fixtures/ams22-descriptors.bin` and replace the pending
rows.

## Device identity (confirmed)

| Property | Value |
| --- | --- |
| VID:PID | `0x1686:0x066F` (5766:1647) |
| Strings | ZOOM Corporation / AMS-22 |
| Device class | 0xEF / 0x02 / 0x01 (Misc, IAD) |
| USB speed | USB 2.0 high speed |
| Config | value 1, bus-powered, bMaxPower 500 (1000 mA at 2 mA units) |

## Interfaces (confirmed, from `dumpsys usb` and the probe's enumeration)

| Iface | Subclass / protocol | Role | Endpoints |
| --- | --- | --- | --- |
| 0 | AudioControl 1/1/32 (UAC2) | entity graph | none |
| 1 | AudioStreaming 1/2/32 | **playback** | alt 0 zero-bandwidth; alt 1: `0x02` isoc OUT (async data, bmAttributes 0x05, 104 B/microframe, interval 1) + `0x82` isoc **explicit feedback** (bmAttributes 0x11, 4 B, interval 4) |
| 2 | AudioStreaming 1/2/32 | **capture** | alt 0 zero-bandwidth; alt 1: `0x81` isoc IN (async data, bmAttributes 0x05, 104 B/microframe, interval 1) |
| 3 | AudioControl 1/1/0 | second AC block (protocol 0) | none |
| 4 | MIDIStreaming 1/3/0 | MIDI (out of scope) | `0x03` / `0x84` bulk 512 B |

- **Feedback is explicit**: endpoint `0x82`, isochronous, usage type 01
  (feedback). UAC2 feedback values are 16.16 (verify the format on first
  readback; the plan's ClockModel::parseFeedback handles both).
- Interface 4 (MIDI) is bulk: leaving it bound to `snd-usb-audio` costs no
  isochronous bandwidth. Interface 3 is never claimed. Both are in the quirk
  registry (`plugin/usb/UsbQuirks.cpp`).
- `max_packet_size` 104 B/microframe covers 96 kHz / 2 ch / subslot 3
  (72 B needed) and 48 kHz with room to spare; 125 us packet cadence
  (`interval=1`) = 8 packets per ms at high speed.

## Raw descriptor dump (confirmed, probe `dump` mode)

336-byte blob saved as `plugin/usb/test/fixtures/ams22-highspeed.bin`
(device descriptor 18 B + configuration 318 B). Parsed by UacDescriptors
and pinned by the `RealAms22Fixture` unit test.

| Fact | Value |
| --- | --- |
| UAC2 clock source ID | **40 (0x28)**, programmable clock (bmAttributes 0x03), SAM_FREQ control present (bmControls 0x07) |
| Terminals' bCSourceID | 40 on all four terminals |
| Subslot size | **4 bytes** |
| Bit resolution | **24 bits** |
| Channels per stream | 2 (from the terminals' bNrChannels; UAC2 Type I carries none) |
| Rates | not in the format descriptors: query `GET_RANGE` on SAM_FREQ_CONTROL (clock source 40). Expect 44100/48000/88200/96000. |
| Altsettings | one active altsetting per direction (iface 1 alt 1, iface 2 alt 1); rate switching happens on the clock source, so 44.1/48/88.2/96 all use the same altsettings |

Entity graph:

```
playback:  USB streaming IT (2)  ->  Speaker OT (20)
capture:   Terminal IT (3)       ->  USB streaming OT (21)
both wired to clock source 40
```

No feature unit: the AMS-22's gain is hardware (its own knobs). The
UAC2 Type I format descriptors are the minimal 6-byte form
(`bFormatType, bSubslotSize, bBitResolution`), which is why the parser must
not assume the UAC1 field order.

## Kernel driver state before claiming (M1.3 checklist)

`USBDEVFS_GETDRIVER` per interface, before `claimInterface(force=true)`
(probe log, 2026-10-17):

| Iface | Kernel driver before claim | Action |
| --- | --- | --- |
| 0 | snd-usb-audio | claim (AudioControl) |
| 1 | snd-usb-audio | claim (playback AS) |
| 2 | snd-usb-audio | claim (capture AS) |
| 3 | snd-usb-audio | leave (quirk: extra AC block) |
| 4 | snd-usb-audio | leave (quirk: MIDI bulk) |

Claiming detaches `snd-usb-audio` from 0/1/2 as expected; the quirk
registry keeps 3/4 untouched.

## Streaming notes (M1.5)

- 3 URBs x 8 packets runs at exactly 1 ms/URB; capture packets land within
  the +/-1 frame tolerance (383-384 bytes/URB average at subslot 4).
- `start_frame` is reported as 0 on every URB by this kernel/HCD, so
  start_frame continuity is unobservable here; the machine still checks it
  (against the observed step) for controllers that populate it.
- Drift from reap timing reads ~-3000 ppm, which is scheduling noise, not
  the device clock: the real drift number comes from the explicit feedback
  endpoint 0x82 (ClockModel parseFeedback, wired up in M2).
