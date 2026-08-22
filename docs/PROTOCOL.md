# GM168 protocol notes

This document describes behavior implemented by the driver. It was derived from
USB traffic and behavioral analysis of the Windows stack. It contains no PSK,
calibration table, biometric sample, decrypted image, or proprietary binary.

## USB interface

| Item | Value |
|---|---:|
| VID:PID | `27c6:589a` |
| Interface | `0` |
| Bulk OUT | `0x01` |
| Bulk IN | `0x83` |
| OUT chunk | 64 bytes |
| Maximum driver receive buffer | 16 KiB |

The implementation recognizes three outer packet types:

- `A0` — command, ACK, and finger-detection traffic;
- `B0` — TLS records transported over USB;
- `B2` — encrypted image records.

`A0` contains a little-endian length, header checksum, command/payload section,
and body checksum. Exact validation and encoding are in `goodix_proto.c`.

## Session outline

The high-level activation sequence is:

1. wake and session reset;
2. firmware/version query;
3. TLS-PSK handshake over `B0` packets;
4. sensor parameter and power/IRQ setup;
5. finger-detection threshold calibration;
6. five no-finger background frames;
7. finger-detection polling;
8. image trigger and `B2` receive;
9. sensor re-arm and explicit finger-lift confirmation.

Every transport phase is asynchronous through libfprint `FpiSsm` state
machines. Deadlines, retry caps, cancellation, removal handling, and recovery
paths are defined in `gm168_timeouts.h`, `gm168_usb_errors.h`, and
`goodix_gm168.c`.

## TLS

The sensor behaves as a legacy TLS-PSK client while the host driver supplies the
server side. The implemented cipher is:

```text
TLS-PSK-WITH-AES-128-CBC-SHA256
```

The device-specific 32-byte PSK is loaded at runtime; it is never compiled into
the source. The OpenSSL adapter uses a local socket pair so the blocking
handshake runs outside the libfprint main loop while USB transport remains
asynchronous.

The sensor exposes read-only specification objects used during provisioning and
validation. Relevant identifiers are kept as internal driver constants. Secret object
contents and model-wide derivation material are intentionally not documented or
distributed.

## Image wire format

One decrypted frame is 10564 bytes:

- 80 wire rows;
- 132 bytes per wire row;
- first 96 bytes contain 64 packed 12-bit pixels;
- remaining 36 bytes are row padding;
- final 4 bytes are a trailer.

Unpacking and transposition produce an 80×64 logical image. The final libfprint
image is resized to 240×192 before NBIS minutiae extraction because the native
sensor area is unusually small.

## Calibration and processing

When matching per-device calibration is available, the core per-pixel operation
is:

```text
corrected[i] = round((bright_reference[i] - raw[i]) * 8192 / scale[i])
```

The implementation validates dimensions, 12-bit ranges, nonzero divisors,
plane ordering, and zero-filled padding before accepting calibration data.
Output then passes through contrast normalization and the driver-selected image
pipeline before NBIS extraction.

The formula was verified byte-for-byte against 5120 pixels from the reference
Windows processing state. The calibration values themselves are per-device and
are not part of this repository.

## Enrollment behavior

The reference sensor required:

- a short settle delay after finger detection;
- two image triggers per touch, selecting the higher-quality result;
- reliable finger-lift confirmation so one hold cannot count as several stages;
- five distinct enrollment placements;
- a Bozorth threshold of 10 for the small active area.

These values are validated on one device only and remain candidates for broader
hardware testing.
