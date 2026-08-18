#ifndef UI_H
#define UI_H

#include <stdint.h>

/* Time-set modal — runs the Y/M/D/h/m/s editor seeded at `current`.
 * Returns the chosen epoch (or `current` if the user cancels). */
uint32_t ui_timeset(uint32_t current);

/* Main account list. Never returns — the only way out is the HOME
 * button on a DSi/3DS or a hard power cycle on an original DS. */
void ui_main(void);

/* Top-screen status message used during the DSi-only NTP sync. Safe
 * to call before storage_init / rtc_init. Clears the top screen and
 * prints `msg` (which may contain '\n's). On the .nds build this is a
 * no-op so callers can use it unconditionally. */
void ui_show_ntp_progress(const char *msg);

#endif
