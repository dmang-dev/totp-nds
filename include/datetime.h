#ifndef DATETIME_H
#define DATETIME_H

#include <stdint.h>

/* Unix epoch (seconds since 1970-01-01 UTC) <-> Gregorian Y/M/D H:M:S.
 * Same algorithms as the totp-gb sibling project, ported verbatim — pure
 * integer math, ARM7-friendly. */

uint32_t ymd_to_epoch(uint16_t yr, uint8_t mo, uint8_t dy,
                      uint8_t hr, uint8_t mn, uint8_t sc);

void epoch_to_ymd(uint32_t epoch,
                  uint16_t *yr, uint8_t *mo, uint8_t *dy,
                  uint8_t *hr, uint8_t *mn, uint8_t *sc);

uint8_t days_in_month(uint8_t m, uint16_t y);

#endif
