# Synthetic regression checks

These checks use generated data and a virtual device. They never open the
physical reader and contain no fingerprint images, device calibration, or PSK.
They require Python 3, a C compiler, pkg-config, GLib/GIO, and libgusb development
files.

Apply the overlay to the pinned libfprint v1.94.100 checkout and build it as
described in the repository README. Then run from this repository:

```bash
python3 tests/test_sensor_params.py
python3 tests/test_capture_clock.py
python3 tests/test_action_guard.py /path/to/libfprint /path/to/libfprint/build
meson test -C /path/to/libfprint/build --print-errorlogs
```

The configuration check exercises the actual loader using a generated
non-biometric register table. It tests valid input, absence, corruption,
length/header errors, permissions, symlinks, and a FIFO with a bounded timeout.

The clock check exercises the driver's elapsed-time calculation at capture
budget boundaries after several representative uptimes. It also reproduces
the loss of precision with the previous floating-point timestamp.

The action check runs actual public libfprint verify transactions with a
USB-free virtual device. It checks normal dispatch, cancellation, deactivation,
and idle handling, and reproduces the previous missing-VERIFY failure. It links
the pinned build's private static archive in the same way as libfprint's own
internal unit tests; it does not require Ubuntu's TOD extension.

These checks do not establish biometric accuracy or cross-device compatibility.
