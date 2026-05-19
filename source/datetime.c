#include "datetime.h"

uint8_t days_in_month(uint8_t m, uint16_t y) {
    static const uint8_t dom[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2u && y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u)) return 29u;
    return dom[m - 1u];
}

uint32_t ymd_to_epoch(uint16_t yr, uint8_t mo, uint8_t dy,
                      uint8_t hr, uint8_t mn, uint8_t sc) {
    uint32_t days = 0;
    uint16_t y;
    uint8_t  m;
    for (y = 1970u; y < yr; y++)
        days += (y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u)) ? 366u : 365u;
    for (m = 1u; m < mo; m++) days += days_in_month(m, yr);
    days += (uint32_t)(dy - 1u);
    return days * 86400UL + (uint32_t)hr * 3600UL
         + (uint32_t)mn * 60UL + sc;
}

void epoch_to_ymd(uint32_t epoch,
                  uint16_t *yr, uint8_t *mo, uint8_t *dy,
                  uint8_t *hr, uint8_t *mn, uint8_t *sc) {
    uint32_t days = epoch / 86400UL;
    uint32_t tod  = epoch % 86400UL;
    uint16_t y = 1970u;
    uint8_t  m = 1u;
    uint16_t ydays;

    *hr = (uint8_t)(tod / 3600UL);
    *mn = (uint8_t)((tod % 3600UL) / 60UL);
    *sc = (uint8_t)(tod % 60UL);

    for (;;) {
        ydays = (y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u)) ? 366u : 365u;
        if (days < (uint32_t)ydays) break;
        days -= ydays;
        y++;
    }
    while (m <= 12u) {
        uint8_t dim = days_in_month(m, y);
        if (days < (uint32_t)dim) break;
        days -= dim;
        m++;
    }
    *yr = y;
    *mo = m;
    *dy = (uint8_t)(days + 1u);
}
