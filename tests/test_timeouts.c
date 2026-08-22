/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include "gm168_timeouts.h"

_Static_assert (GM168_USB_RX_TIMEOUT_MS       == 2000, "RX default");
_Static_assert (GM168_USB_RX_SHORT_TIMEOUT_MS == 1000, "RX short");
_Static_assert (GM168_USB_RX_LONG_TIMEOUT_MS  == 3000, "RX long");
_Static_assert (GM168_USB_TX_TIMEOUT_MS       == 2000, "TX");
_Static_assert (GM168_WAKEUP_DELAY_MS         == 50,   "wakeup");
_Static_assert (GM168_TLS_PUMP_DELAY_MS       == 10,   "TLS pump");
_Static_assert (GM168_REARM_DELAY_MS          == 150,  "rearm");
_Static_assert (GM168_WAIT_LIFT_POLL_MS       == 300,  "lift poll");
_Static_assert (GM168_WAIT_LIFT_MAX_REARMS    == 8,    "lift retries");
_Static_assert (GM168_REARM_DEADLINE_MS       == 6000, "rearm deadline");
_Static_assert (GM168_USB_RX_RETRY_LIMIT      == 60,   "RX retries");
_Static_assert (GM168_TLS_RETRY_LIMIT         == 60,   "TLS retries");

int
main (void)
{
    puts ("gm168 timeout contract: OK");
    return 0;
}
