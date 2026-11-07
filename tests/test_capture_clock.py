#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check capture elapsed time across uptimes and budget boundaries, without USB."""
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

driver = Path(__file__).resolve().parents[1] / "driver/goodix_gm168.c"
source = driver.read_text()
match = re.search(
    r"(?P<type>\w+)\s+now_us\s*=\s*g_get_monotonic_time\s*\(\s*\)\s*;"
    r"\s*gint64\s+elapsed\s*=\s*[^;]+;",
    source,
)
assert match, "capture elapsed-time calculation not found"
calculation = match.group()
program = r"""
#include <glib.h>
typedef struct { gint64 capture_start_us; } Capture;
static gint64 fixture_time;
#define g_get_monotonic_time() fixture_time
int main(void) {
  const gint64 uptimes[] = {
    G_USEC_PER_SEC, G_GINT64_CONSTANT(3600) * G_USEC_PER_SEC,
    G_GINT64_CONSTANT(172800000000),
    G_GINT64_CONSTANT(1209600000000),
    G_GINT64_CONSTANT(5184000000000)
  };
  const gint64 delays[] = {0, 1, 299, 300, 301, 799, 800, 801, 3000};
  Capture capture;
  Capture *self = &capture;
  for (gsize i = 0; i < G_N_ELEMENTS(uptimes); i++) {
    for (gsize j = 0; j < G_N_ELEMENTS(delays); j++) {
      self->capture_start_us = uptimes[i];
      fixture_time = uptimes[i] + delays[j] * 1000;
      CALCULATION
      if (elapsed != delays[j])
        return 1;
    }
  }
  return 0;
}
""".replace("CALCULATION", calculation)
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "glib-2.0"], text=True))
with tempfile.TemporaryDirectory(prefix="gm168-clock-test-") as temporary:
    directory = Path(temporary)
    for name, code, succeeds in (
        ("fixed", program, True),
        ("previous", program.replace(calculation, calculation.replace(
            match["type"], "gfloat", 1)), False),
    ):
        cfile, binary = directory / (name + ".c"), directory / name
        cfile.write_text(code)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        str(cfile), "-o", str(binary), *flags], check=True)
        result = subprocess.run([str(binary)], timeout=5)
        assert (result.returncode == 0) == succeeds, name
        print("PASS", name, "capture budget across uptime boundaries")
