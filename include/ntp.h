#ifndef NTP_H
#define NTP_H

#include <stdint.h>

/*
 * SNTP client for the DSi-mode build.
 *
 * Brings up dswifi using one of the firmware-stored WFC profiles
 * (DSi gives users six configurable profile slots versus DS's three),
 * resolves an NTP server, sends an SNTPv3 query, and converts the
 * server's transmit timestamp to a Unix epoch.
 *
 * Result codes are negative on failure so a single int8 return covers
 * "success" plus enough detail for the on-screen status line.
 *
 *   NTP_OK            (0)  epoch written; clock should be trusted
 *   NTP_WIFI_FAIL    (-1)  Wifi_InitDefault returned false
 *   NTP_NOT_ASSOC    (-2)  association never reached ASSOCIATED in budget
 *   NTP_DNS_FAIL     (-3)  gethostbyname() returned NULL
 *   NTP_SOCK_FAIL    (-4)  socket()/sendto() returned negative
 *   NTP_TIMEOUT      (-5)  no reply within receive budget
 *   NTP_BAD_REPLY    (-6)  short reply or stratum 0 / kiss-of-death
 *   NTP_CANCELLED    (-7)  user pressed B during the sync attempt
 *
 * On the non-DSi (.nds) build this header is still safe to include —
 * ntp_sync() is compiled out and replaced with a stub that always
 * returns NTP_WIFI_FAIL so callers can fall through to the manual
 * time-set path.
 */

#define NTP_OK         0
#define NTP_WIFI_FAIL -1
#define NTP_NOT_ASSOC -2
#define NTP_DNS_FAIL  -3
#define NTP_SOCK_FAIL -4
#define NTP_TIMEOUT   -5
#define NTP_BAD_REPLY -6
#define NTP_CANCELLED -7

int ntp_sync(uint32_t *out_epoch);

#endif
