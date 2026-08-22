# Release readiness

## Verdict

The working research directory must never be published as a repository or
archive. It contains per-device secrets, biometric material, Windows media,
proprietary binaries, packet captures, VM state, absolute machine paths, and
one-off upload/debug tooling.

This curated directory is suitable for publication as a **community developer
preview**, not as a supported end-user release and not as an upstream-ready
libfprint merge request.

## Evidence obtained on the reference device

- exact USB device: `27c6:589a`;
- official Windows driver successfully enrolled and verified the finger;
- device PSK hash matched the recovered PSK;
- TLS handshake and encrypted image capture succeeded;
- 5120/5120 pixels matched the recovered Windows calibration formula;
- five-position libfprint enrollment completed;
- same-finger verification matched at Bozorth score `14/10`;
- two later post-review same-finger attempts scored `5/10` and `3/10` and
  false-rejected, confirming unresolved placement sensitivity;
- an exercised different finger produced no match;
- system fprintd returned `verify-match`;
- sudo PAM authentication completed successfully on Omarchy 4.

These observations prove operation on one unit. They do not establish false
accept/reject rates or cross-device compatibility.

## Deliberate exclusions

The public tree excludes:

- concrete `gm168_cal.h` tables from the developer sensor;
- all PSK, DPAPI, calibration, FDT, template, and biometric files;
- Windows DLL/EXE/SYS/CAT files and installation media;
- VM, TPM, firmware, screenshot, debugger, and packet-capture artifacts;
- raw-frame dump code and default protocol logging;
- the model-wide WhiteBox PSK derivation/extraction implementation;
- machine-specific package recipes and privileged installer scripts.

## Remaining blockers for an end-user beta

1. Build a safe, reproducible per-device provisioning workflow for both the PSK
   and the two calibration files.
2. Validate provisioning and capture on additional independent 27c6:589a units.
3. Exercise suspend/resume, reboot, fprintd restart, cancellation, rapid touch,
   USB removal, timeout, and failed TLS paths on real hardware.
4. Add deterministic non-biometric protocol/state-machine fixtures.
5. Decide how distribution packages track libfprint ABI upgrades without
   silently replacing or overwriting system libraries.
6. Obtain a legal review if distributing reverse-engineered register constants
   is a concern in the publisher's jurisdiction.

## Additional blockers for upstream libfprint

- provide a complete sanitized protocol specification;
- add a libfprint umockdev driver test and `tests/meson.build` registration;
- pass upstream uncrustify, strict-warning, ASan, valgrind, and CI jobs;
- remove or justify experimental environment-controlled processing paths;
- add NEWS and contributor attribution changes;
- present evidence from the hardware sample set requested by libfprint
  maintainers, or justify the laptop-specific exception;
- explain the per-device PSK and calibration lifecycle without proprietary code;
- split the work into reviewable commits rather than submitting the research
  history or a generated monolithic patch.

## Known provenance questions

The driver is LGPL-2.1-or-later and derives from LGPL-compatible libfprint work.
`goodix_proto.c` contains a sensor register map reconstructed from observed USB
traffic. It is protocol data rather than a shipped vendor binary, but its final
redistribution treatment should be reviewed before presenting the code as an
official product.
