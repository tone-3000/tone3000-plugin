# Descriptor fixtures

Captured raw descriptor blobs (the bytes of
`UsbDeviceConnection.getRawDescriptors()`) from real devices, saved by the
probe app's `dump` mode (plan M1.3). The parser tests in
`uac_descriptors_tests.cpp` run against hand-built synthetic blobs shaped
like the Zoom AMS-22 until the real captures land here as
`ams22-highspeed.bin` / `ams22-fullspeed.bin`; wire the tests to the real
files when they arrive and keep the synthetic blobs as regression cases for
layouts the AMS-22 does not have (UAC1, per-altsetting rate lists).
