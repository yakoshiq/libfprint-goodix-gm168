#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check the driver's guard using real libfprint verify API transactions.

The virtual device performs no hardware I/O and has no biometric samples.
"""
from pathlib import Path
import shlex
import subprocess
import tempfile

import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("libfprint_source", type=Path)
parser.add_argument("libfprint_build", type=Path)
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "driver/goodix_gm168.c").read_text()
start = source.index("static gboolean\ngm168_action_active")
end = source.index("static gboolean\ngm168_ssm_is_current", start)
guard = source[start:end]
program = r"""
#include <fprint.h>
#include "fpi-device.h"
#include <stdio.h>
typedef struct {
  FpDevice parent;
  gboolean deactivating;
  gboolean cancel_inside;
  gboolean allowed;
  guint calls;
  GCancellable *request;
} ActionGateDevice;
typedef struct { FpDeviceClass parent; } ActionGateDeviceClass;
typedef ActionGateDevice FpDeviceGoodixGm168;
G_DEFINE_TYPE (ActionGateDevice, action_gate_device, FP_TYPE_DEVICE)
""" + guard + r"""
static void virtual_open (FpDevice *device) {
  fpi_device_open_complete (device, NULL);
}
static void virtual_close (FpDevice *device) {
  fpi_device_close_complete (device, NULL);
}
static void virtual_verify (FpDevice *device) {
  ActionGateDevice *self = (ActionGateDevice *) device;
  self->calls++;
  if (self->cancel_inside)
    g_cancellable_cancel (self->request);
  self->allowed = gm168_action_active (self);
  if (self->deactivating || self->cancel_inside) {
    fpi_device_verify_complete (device, g_error_new_literal (
        G_IO_ERROR, G_IO_ERROR_CANCELLED, "synthetic cancellation"));
    return;
  }
  fpi_device_verify_report (device, FPI_MATCH_FAIL, NULL, NULL);
  fpi_device_verify_complete (device, NULL);
}
static void action_gate_device_class_init (ActionGateDeviceClass *klass) {
  FpDeviceClass *device = FP_DEVICE_CLASS (klass);
  device->id = "virtual_action_gate";
  device->full_name = "Synthetic action lifecycle";
  device->type = FP_DEVICE_TYPE_VIRTUAL;
  device->features = FP_DEVICE_FEATURE_VERIFY;
  device->temp_hot_seconds = -1;
  device->open = virtual_open;
  device->close = virtual_close;
  device->verify = virtual_verify;
}
static void action_gate_device_init (ActionGateDevice *self) {
  (void) self;
}
int main (void) {
  for (int mode = 0; mode < 3; mode++) {
    g_autoptr(FpDevice) device =
      g_object_new (action_gate_device_get_type (), NULL);
    ActionGateDevice *self = (ActionGateDevice *) device;
    g_autoptr(GCancellable) request = g_cancellable_new ();
    g_autoptr(GError) error = NULL;
    g_autoptr(FpPrint) template =
      g_object_ref_sink (fp_print_new (device));
    if (!fp_device_open_sync (device, NULL, &error))
      return 2;
    if (gm168_action_active (self))
      return 3;
    self->deactivating = mode == 1;
    self->cancel_inside = mode == 2;
    self->request = request;
    gboolean matched = FALSE;
    gboolean ok = fp_device_verify_sync (
        device, template, request, NULL, NULL, &matched, NULL, &error);
    if (self->calls != 1 || self->allowed != (mode == 0) ||
        ok != (mode == 0) || matched)
      return 4;
    if (mode && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return 5;
    g_clear_error (&error);
    self->deactivating = FALSE;
    if (gm168_action_active (self) ||
        !fp_device_close_sync (device, NULL, &error))
      return 6;
  }
  puts ("PASS real verify dispatch; shutdown and cancellation suppress I/O");
  return 0;
}
"""
includes = [args.libfprint_source.resolve() / "libfprint",
            args.libfprint_build.resolve(),
            args.libfprint_build.resolve() / "libfprint"]
library = args.libfprint_build.resolve() / "libfprint"
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "glib-2.0", "gio-2.0",
     "gobject-2.0", "gmodule-2.0", "gusb", "openssl"], text=True))
with tempfile.TemporaryDirectory(prefix="gm168-action-test-") as temporary:
    directory = Path(temporary)
    for name, text, expected_success in (
        ("fixed", program, True),
        ("previous", program.replace(
            "           action == FPI_DEVICE_ACTION_VERIFY ||\n", ""), False),
    ):
        code, binary = directory / (name + ".c"), directory / name
        code.write_text(text)
        subprocess.run([
            "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            *("-I" + str(p) for p in includes),
            str(code), "-o", str(binary), "-L" + str(library),
            "-Wl,-rpath," + str(library), "-Wl,--export-dynamic",
            "-Wl,--start-group", str(library / "libfprint-private.a"),
            str(library / "libnbis.a"), "-l:libfprint-2.so.2", *flags,
            "-lm", "-Wl,--end-group",
        ], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                timeout=5)
        if (result.returncode == 0) != expected_success:
            raise RuntimeError(f"{name} lifecycle regression check failed: "
                               f"{result.stderr}")
        print(result.stdout.strip() if expected_success
              else "PASS previous guard reproduces verify-dispatch failure")
