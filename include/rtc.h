#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* Initialize the software clock anchor. Reads the NDS hardware RTC
 * (year + month + day + hour + minute + second, 2000-2099) and treats
 * that as Unix-epoch-relative wall-clock UTC. Calling this once at
 * boot is enough; subsequent rtc_get_epoch() calls bring the count
 * forward by the elapsed tick-counter delta. */
void rtc_init_from_system(void);

/* Override the anchor to a specific Unix epoch (e.g. when the user
 * confirms the time-set screen). Saves immediately to storage. */
void rtc_set_epoch(uint32_t e);

/* Returns current Unix-epoch UTC. */
uint32_t rtc_get_epoch(void);

#endif
