# Contributing

The highest-value contribution is validation on an independent `27c6:589a`
device without publishing private sensor state.

## Safe issue data

You may report:

- laptop make/model;
- `lsusb` line containing `27c6:589a`;
- firmware version string printed by the driver;
- distribution, kernel, libfprint, and fprintd versions;
- state-machine name and numeric state where a failure occurred;
- redacted error messages with home paths removed.

Do **not** attach PSKs, sealed objects, calibration files, FDT state, fingerprint
images/templates, raw frames, packet captures, protocol logs, Windows binaries,
or VM images. If a maintainer asks for any of those in a public issue, decline
and refer to `SECURITY.md`.

## Code changes

- Base driver changes on the files in `driver/`, not on a private research tree.
- Keep LGPL-2.1-or-later SPDX headers.
- Preserve attribution in `NOTICE`.
- Do not add model-wide keys, per-device constants, proprietary binaries, or
  generated calibration tables.
- Add deterministic synthetic tests for parser/state-machine changes.
- Never use real fingerprint images as public fixtures.
- Keep hardware access asynchronous and ensure cancellation/deactivation leaves
  no live transfer, timer, SSM, or worker callback.

## Before proposing upstream changes

Build against the pinned libfprint base, run the applicable libfprint test suite
and formatting checks, and read upstream `HACKING.md`. A community-repository
change is not automatically suitable for the upstream libfprint tree.
