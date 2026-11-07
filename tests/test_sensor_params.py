#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise the exact configuration loader without accessing USB hardware."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "driver"
source = (DRIVER / "goodix_gm168.c").read_text()
start = source.index("static guint16\ngm168_read_le16")
end = source.index("/* Load the per-device Windows calibration", start)
functions = source[start:end]
protocol = (DRIVER / "goodix_proto.c").read_text()
start = protocol.index("const guint8 goodix_gm168_set_param[]")
end = protocol.index(";", start) + 1
table = protocol[start:end]
harness = """
#include <glib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
struct _FpDeviceGoodixGm168 { guint8 sensor_params[256]; };
static char *gm168_psk_dir(void) {
    return g_strdup(g_getenv("GOODIX_GM168_DIR"));
}
#define fp_dbg(...) ((void)0)
""" + table + """
static const gsize goodix_gm168_set_param_len = sizeof(goodix_gm168_set_param);
""" + functions + """
int main(void) {
    struct _FpDeviceGoodixGm168 self;
    GError *error = NULL;
    gboolean ok = gm168_load_sensor_params(&self, &error);
    if (!ok) {
        g_printerr("%s\\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        return 1;
    }
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="gm168-config-test-") as temporary:
    root = Path(temporary)
    cfile = root / "loader.c"
    binary = root / "loader"
    cfile.write_text(harness)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "glib-2.0"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    str(cfile), "-o", str(binary), *flags], check=True)
    private = root / "configuration"
    private.mkdir(mode=0o700)
    config = private / "sensor-register-map.bin"

    def check(name, expected):
        result = subprocess.run(
            [str(binary)], env={**os.environ, "GOODIX_GM168_DIR": str(private)},
            capture_output=True, text=True, timeout=2)
        assert (result.returncode == 0) == expected, (name, result.stderr)
        print("PASS", name)

    check("optional file absent", True)
    # A non-biometric fixture with the real section header and a valid checksum.
    fixture = bytearray(256)
    # Extract the protocol's header rather than relying on a fixture assumption.
    import re
    values = re.findall(r"0x([0-9a-fA-F]{2})\b", table)
    fixture[:16] = bytes(int(v, 16) for v in values[:16])
    struct.pack_into("<H", fixture, 254,
                     -(sum(struct.unpack("<127H", fixture[:254])) + 0xa5a5) & 0xffff)

    def write(content):
        config.write_bytes(content)
        config.chmod(0o600)

    write(fixture)
    check("valid private configuration", True)
    damaged = fixture.copy()
    damaged[200] ^= 1
    write(damaged)
    check("invalid checksum rejected", False)
    write(fixture[:-1])
    check("truncated configuration rejected", False)
    write(fixture + b"\0")
    check("oversized configuration rejected", False)
    damaged = fixture.copy()
    damaged[0] ^= 1
    write(damaged)
    check("unsupported mode header rejected", False)
    write(fixture)
    config.chmod(0o644)
    check("publicly readable configuration rejected", False)
    config.chmod(0o600)
    private.chmod(0o755)
    check("shared configuration directory rejected", False)
    private.chmod(0o700)
    target = root / "target.bin"
    target.write_bytes(fixture)
    target.chmod(0o600)
    config.unlink()
    config.symlink_to(target)
    check("file symlink rejected", False)
    config.unlink()
    os.mkfifo(config, mode=0o600)
    check("FIFO rejected without blocking", False)
    config.unlink()
    real = root / "real-directory"
    private.rename(real)
    private.symlink_to(real, target_is_directory=True)
    check("directory symlink rejected", False)
