#ifndef UI_H
#define UI_H

#include <stdint.h>

/* Time-set modal — runs the Y/M/D/h/m/s editor seeded at `current`.
 * Returns the chosen epoch (or `current` if the user cancels). */
uint32_t ui_timeset(uint32_t current);

/* Main account list. Never returns — the only way out is the HOME
 * button on a DSi/3DS or a hard power cycle on an original DS. */
void ui_main(void);

#endif
