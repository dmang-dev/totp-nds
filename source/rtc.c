/*
 * Software RTC anchored to the NDS hardware RTC (Toshiba TC8521 +
 * a low-power coin-cell on the mainboard). libnds exposes the
 * current RTC reading as a struct via the IPC FIFO; newlib also
 * hooks time(NULL) to that reading and adds elapsed ticks since
 * boot, which is exactly what we want.
 *
 * Caveat: the NDS RTC stores year as 00-99 (2000-2099) and has no
 * timezone field — what comes back is wall-clock UTC by convention
 * (firmware setup screen labels it that way), but in practice users
 * have it set to local time. We treat whatever it reports as UTC; the
 * user can fix it through ui_timeset() if codes come out wrong.
 */
#include "rtc.h"
#include "datetime.h"
#include "storage.h"

#include <time.h>

/* If the user overrides via timeset, we record (epoch, system_time) at
 * the moment of override and then report epoch + elapsed since. */
static uint32_t s_anchor_epoch = 0u;
static time_t   s_anchor_time  = 0;
static uint8_t  s_overridden   = 0u;

void rtc_init_from_system(void) {
    /* time(NULL) returns Unix seconds derived from the NDS hardware
     * RTC (year limited to 2000-2099 — fine for all 2025-2099 use). */
    time_t now = time(NULL);
    if (now < 0) now = 0;
    s_anchor_epoch = (uint32_t)now;
    s_anchor_time  = now;
    s_overridden   = 0u;
}

void rtc_set_epoch(uint32_t e) {
    s_anchor_epoch = e;
    s_anchor_time  = time(NULL);
    s_overridden   = 1u;
}

uint32_t rtc_get_epoch(void) {
    if (!s_overridden) {
        /* Not overridden — trust the hardware RTC directly. */
        time_t now = time(NULL);
        return (uint32_t)(now < 0 ? 0 : now);
    }
    /* Overridden — extend from the anchor by the elapsed system time. */
    time_t now = time(NULL);
    if (now < s_anchor_time) return s_anchor_epoch;
    return s_anchor_epoch + (uint32_t)(now - s_anchor_time);
}
