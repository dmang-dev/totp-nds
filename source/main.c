/*
 * totp-nds — RFC 6238 TOTP authenticator for Nintendo DS / DS Lite,
 * with native runtime on DSi and 3DS via DS-mode.
 *
 * Sibling of totp-gb / totp-gba / totp-psp. The crypto core (sha1.c,
 * hmac.c, base32.c, totp.c, datetime.c) is byte-for-byte identical
 * across all four — if those KATs pass on GBA they pass here. Platform
 * glue is NDS-specific: rtc.c (hardware RTC via libnds + newlib time),
 * storage.c (libfat to SD or DLDI flashcart), ui.c (dual-screen
 * PrintConsole text mode).
 *
 * Boot sequence:
 *   1. videoSetMode + dual PrintConsole init (top = display, bottom = menu)
 *   2. Self-test: run RFC 6238 KAT vectors, print PASS/FAIL for ~1s
 *   3. storage_init: mount libfat, load or format /totp-nds.dat
 *   4. RTC seed: WiFi NTP first on the DSi build, else the saved epoch.
 *      The hardware RTC only pre-fills ui_timeset(), which the user
 *      confirms — see the priority-order note in main().
 *   5. ui_main() — never returns.
 */
#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <stdio.h>

#include "storage.h"
#include "rtc.h"
#include "totp.h"
#include "ui.h"
#include "ntp.h"

/* Forward decl — defined in ui.c, called only from here. */
void _ui_init_consoles(void);

/* ---- boot self-test (RFC 6238 KATs) --------------------------------- */
#define DEMO_SECRET "JBSWY3DPEHPK3PXP"

struct kat { uint32_t epoch; uint32_t expected; };
static const struct kat KATS[] = {
    {          0u, 282760u },
    { 1234567890u, 742275u },
    { 1778088090u, 283711u },
    { 1778088141u, 113232u },
};
#define NUM_KATS (sizeof(KATS) / sizeof(KATS[0]))

static int run_self_test(void) {
    consoleClear();
    iprintf("totp-nds self-test (RFC 6238)\n");
    iprintf("secret: %s (\"Hello!\")\n\n", DEMO_SECRET);
    unsigned pass = 0u;
    for (unsigned i = 0u; i < NUM_KATS; i++) {
        uint32_t got = totp_generate(DEMO_SECRET, KATS[i].epoch);
        int ok = (got == KATS[i].expected);
        iprintf("  epoch=%-10u got=%06u\n             expected=%06u  %s\n",
                (unsigned)KATS[i].epoch,
                (unsigned)got,
                (unsigned)KATS[i].expected,
                ok ? "PASS" : "FAIL");
        if (ok) pass++;
    }
    iprintf("\n  result: %u/%u PASS\n", pass, (unsigned)NUM_KATS);
    if (pass != NUM_KATS) {
        iprintf("\n *** SELF-TEST FAILED ***\n  halting 5s.\n");
        for (int i = 0; i < 5 * 60; i++) swiWaitForVBlank();
        return 0;
    }
    /* ~1s pause so a human watching boot can read the result. */
    for (int i = 0; i < 60; i++) swiWaitForVBlank();
    return 1;
}

#ifdef DSI_BUILD
/* ---- NTP status line -------------------------------------------------
 *
 * Map an ntp_sync() result to something the user can act on. The top
 * screen is 32 columns and ui_show_ntp_progress() indents only the
 * first line, so continuation lines carry their own two spaces; keep
 * every line under 30 characters.
 *
 * NTP_NO_PROFILE is the one worth spelling out. It's the only failure
 * the user can actually fix, and a generic "sync failed" gives them no
 * hint that the fix lives in System Settings. */
static const char *ntp_status_text(int rc) {
    switch (rc) {
    case NTP_OK:         return "WiFi time-sync OK.";
    case NTP_NO_PROFILE: return "No WiFi profile found.\n"
                                "  Add one in System Settings\n"
                                "  to sync automatically.";
    case NTP_WIFI_FAIL:  return "WiFi unavailable.";
    case NTP_NOT_ASSOC:  return "Couldn't join the network.\n"
                                "  Check the AP is in range.";
    case NTP_DNS_FAIL:   return "Joined, but DNS lookup\n  failed.";
    case NTP_SOCK_FAIL:  return "Network socket error.";
    case NTP_TIMEOUT:    return "No reply from time server.";
    case NTP_BAD_REPLY:  return "Bad reply from time server.";
    case NTP_CANCELLED:  return "Sync cancelled.";
    default:             return "WiFi sync failed.";
    }
}
#endif

int main(void) {
    /* Bring up a bare bottom-screen console early so the self-test has
     * somewhere to print. _ui_init_consoles() re-does this later with
     * the dual-screen layout for the main UI. */
    consoleDemoInit();

#ifndef SKIP_SELFTEST
    (void)run_self_test();
#endif

    /* Real dual-screen layout. */
    _ui_init_consoles();

    /* Load (or format) the savedata file. */
    uint8_t loaded = storage_init();

    /* Seed software RTC. Priority order:
     *   1. (DSi-mode only) WiFi NTP sync. If it succeeds we override
     *      whatever the saved/hardware clocks say — the network is
     *      authoritative.
     *   2. Saved epoch from storage — preserves time across power
     *      cycles even if WiFi is offline at this boot.
     *   3. NDS hardware RTC — pre-fills the time-set screen only. Never
     *      trusted unconfirmed (no timezone field; usually local time).
     *   4. Manual time-set screen, confirmed by the user. Reached on
     *      first boot, or when NTP failed and nothing was saved.
     */
    uint32_t saved = storage_get_epoch();
    uint8_t  time_known = 0u;

#ifdef DSI_BUILD
    {
        /* Tell the user what we're doing. ntp_sync() budgets up to ~20s
         * for association plus ~5s for the round-trip, and samples B on
         * every frame throughout, so the cancel offer here is real. */
        ui_show_ntp_progress("Syncing time over WiFi...\n  B cancels.");
        uint32_t e_ntp = 0u;
        int rc = ntp_sync(&e_ntp);
        if (rc == NTP_OK) {
            rtc_set_epoch(e_ntp);
            storage_set_epoch(e_ntp);
            ui_show_ntp_progress(ntp_status_text(rc));
            time_known = 1u;
        } else {
            /* Non-fatal — fall through to the other clock sources. The
             * screen that follows (account list, or time-set) already
             * shows what we fell back to, so this line only has to say
             * why the sync didn't happen. */
            ui_show_ntp_progress(ntp_status_text(rc));
            /* Several of these lines carry an instruction, and
             * ui_show_ntp_progress only builds in ~0.5s. Cancelling is
             * deliberate though — don't stall someone who asked to skip. */
            if (rc != NTP_CANCELLED) {
                for (int i = 0; i < 2 * 60; i++) swiWaitForVBlank();
            }
        }
    }
#endif

    if (!time_known) {
        if (loaded && saved != 0u) {
            rtc_set_epoch(saved);
            time_known = 1u;
        } else {
            /* No saved epoch — seed the anchor from the hardware RTC so
             * the time-set screen opens pre-filled with a close guess.
             * We deliberately do NOT accept it unconfirmed: the NDS RTC
             * has no timezone field and users overwhelmingly set it to
             * local time, while we interpret the reading as UTC. Taking
             * it silently would generate codes off by the user's UTC
             * offset with nothing on screen to explain why. Confirming
             * through ui_timeset() is what tells us it really is UTC. */
            rtc_init_from_system();
        }
    }

    if (!time_known) {
        uint32_t e = ui_timeset(rtc_get_epoch());
        rtc_set_epoch(e);
        storage_set_epoch(e);
    }

    ui_main();   /* never returns */
    return 0;
}
