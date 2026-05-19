/*
 * Host-runnable known-answer-vector harness for the totp-nds crypto core.
 *
 * Same driver as totp-psp/tests/run_kats.c — included by directly
 * compiling the same `source/sha1.c`, `hmac.c`, `base32.c`, `totp.c`
 * files that get linked into `totp-nds.nds`. Crypto code is pure ISO C
 * with no NDS includes, so if KATs pass here they pass on hardware.
 *
 * Build: cd tests && make    (or: gcc -I../include -o run_kats run_kats.c)
 * Run:   ./run_kats           (exit 0 = all pass, 1 = any fail)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/sha1.c"
#include "../source/hmac.c"
#include "../source/base32.c"
#include "../source/totp.c"

#define SECRET "JBSWY3DPEHPK3PXP"

struct kat { uint32_t epoch; uint32_t expected; };
static const struct kat KATS[] = {
    {          0u, 282760u },
    { 1234567890u, 742275u },
    { 1778088090u, 283711u },
    { 1778088141u, 113232u },
};
#define NUM_KATS (sizeof(KATS) / sizeof(KATS[0]))

int main(void) {
    unsigned i, pass = 0u;
    printf("totp-nds KAT runner (RFC 6238)\n");
    printf("secret: %s (\"Hello!\")\n\n", SECRET);
    for (i = 0u; i < NUM_KATS; i++) {
        uint32_t got = totp_generate(SECRET, KATS[i].epoch);
        int ok = (got == KATS[i].expected);
        printf("  epoch=%-10u  got=%06u  expected=%06u  %s\n",
               (unsigned)KATS[i].epoch,
               (unsigned)got,
               (unsigned)KATS[i].expected,
               ok ? "PASS" : "FAIL");
        if (ok) pass++;
    }
    printf("\nresult: %u/%u PASS\n", pass, (unsigned)NUM_KATS);
    return (pass == NUM_KATS) ? 0 : 1;
}
