# libfprint-goodix-gm168

Experimental libfprint image driver for the Goodix GM168SEC fingerprint reader
used in the Redmi Book Pro 15 2022.

## Status

**Community developer preview — not upstream-ready.**

## AI-assisted development and “as is” notice

Most of the reverse engineering, implementation, debugging, code review, and
documentation in this repository was performed with AI coding agents under
human direction. The result was tested on one physical device, but it has not
received a complete independent human security or correctness audit.

The project is provided **“as is”, without warranty of any kind**. Hardware
validation does not guarantee correctness, security, compatibility with another
sensor, or protection against data loss. Review the source and understand the
per-device secret and biometric-data handling before using it. The applicable
license terms and warranty disclaimer are in `LICENSE`.

Validated on one physical `27c6:589a` device with firmware
`GF3206_GM168SEC_APP_10008`:

- USB initialization and finger detection;
- per-device TLS-PSK handshake;
- image capture and Windows-compatible calibration formula;
- libfprint enrollment and verification;
- fprintd enrollment and `verify-match`;
- PAM use through sudo, polkit, and the Omarchy 4 lock screen.

The successful verification score on the reference device was `14/10`.
Different-finger checks produced no match in the exercised samples. This is not
a population-scale false-accept/false-reject evaluation. Two later same-finger
checks with the post-review lifecycle build produced `5/10` and `3/10`, so
false rejects and threshold/placement sensitivity remain unresolved.

## Important limitation

The sensor requires **per-device** data:

- `psk.bin` — 32-byte TLS PSK;
- `calib_windows.dat` — 140464-byte sensor calibration block;
- `cal_scale.bin` — 10240-byte per-pixel scale plane.

A safe, general provisioning workflow for obtaining both the PSK and calibration
is not ready yet. The reference device only produced reliable NBIS minutiae
after its own Windows calibration was loaded. Consequently this repository is
useful for development and validation, but is not yet a plug-and-play installer
for another laptop.

Never reuse another device's files. Never publish PSKs, calibration data,
fingerprint images, libfprint templates, TLS captures, or session logs.

## Source layout

- `driver/` — libfprint driver sources;
- `patches/` — build-system integration for libfprint v1.94.100;
- `tools/apply-overlay.sh` — applies this source overlay to a clean libfprint
  v1.94.100 checkout;
- `docs/` — protocol, provisioning, security, and release notes.

The generated `gm168_cal.h` from the research tree is intentionally excluded: it
contained calibration measurements from one physical sensor and is not used by
the final driver.

## Apply to libfprint v1.94.100

```bash
git clone --branch v1.94.100 --depth 1 \
  https://gitlab.freedesktop.org/libfprint/libfprint.git
./tools/apply-overlay.sh ./libfprint
```

The overlay tool refuses a dirty checkout or a base other than commit
`80a4b5ec612892c5c056c48dddaa561452cf37ec`.

Build an isolated test copy rather than replacing the system library directly:

```bash
meson setup libfprint/build libfprint \
  --prefix="$PWD/prefix" \
  -Ddrivers=goodix_gm168 \
  -Dgtk-examples=false \
  -Ddoc=false \
  -Dinstalled-tests=false
meson compile -C libfprint/build
meson install -C libfprint/build
```

Distribution integration should use a real package that conflicts with and
provides the distribution's libfprint package. Do not copy a shared library over
`/usr/lib` behind the package manager.

## Per-device provisioning

If `psk.bin` is absent, the driver can read the sensor's 324-byte DPAPI-sealed
object and save it as `sealed.bin` with mode `0600`. Decrypting that object
requires the Windows security identity that originally provisioned the sensor.
This repository intentionally does not publish the model-wide WhiteBox
derivation path or an unreviewed Windows privilege-transfer script.

After independently obtaining the matching PSK and calibration, point an
isolated driver run at the private state directory:

```bash
export GOODIX_GM168_DIR="$PWD/device-state"
```

The directory must also contain the two matching calibration files described
above. Production fprintd deployments should use `/etc/goodix-gm168` owned by
root with directory mode `0700` and file mode `0600`.

## Diagnostics

Raw protocol logging is disabled by default. To enable it explicitly for a
short debugging session:

```bash
export GM168_LOG_DIR="$PWD/private-logs"
```

The driver creates new log directories/files with modes `0700` and `0600` and
refuses an insecure existing directory.
Treat these logs as sensitive and do not attach them to public issues without
manual redaction.

## Upstream status

Before a libfprint merge request this driver still needs:

- tests using libfprint's umockdev driver-test framework;
- validation on additional independent `27c6:589a` devices;
- a reproducible per-device calibration workflow;
- cleanup of experimental image-processing and diagnostic paths;
- NEWS and contributor metadata changes in the upstream tree;
- maintainer review of the reverse-engineered constants and protocol document.

See `docs/RELEASE-READINESS.md`.

## License and provenance

LGPL-2.1-or-later. See `LICENSE` and `NOTICE`.

No Windows driver binary, firmware, VM image, per-device secret, biometric
sample, calibration dump, or packet capture is included.
