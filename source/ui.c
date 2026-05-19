/*
 * Console-mode UI for totp-nds. Top screen = live code / account
 * display; bottom screen = menus, input, and key hints. Both screens
 * are libnds PrintConsole text grids (32x24 chars at 8x8). Splitting
 * across two screens lets us always show "what controls do what"
 * without crowding the data display.
 *
 * Screens implemented:
 *   - ui_main()       live account list + 6-digit code + countdown bar
 *   - view modal      expanded single-account view with delete prompt
 *   - add modal       4-way char picker for name + secret entry
 *   - ui_timeset()    Y/M/D h:m:s editor
 *
 * Keep this file the only one that touches PrintConsole — main.c uses
 * raw iprintf only during boot self-test before any console split.
 */
#include "ui.h"
#include "storage.h"
#include "rtc.h"
#include "totp.h"
#include "datetime.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

static PrintConsole topConsole;
static PrintConsole bottomConsole;

#define COLS 32
#define ROWS 24

/* Wall-clock timestamp display starts at row 2 of the top screen. */

static void ui_clear_screen(PrintConsole *c) {
    consoleSelect(c);
    iprintf("\x1b[2J");      /* ANSI clear */
}

static void ui_at(PrintConsole *c, int row, int col) {
    consoleSelect(c);
    iprintf("\x1b[%d;%dH", row + 1, col + 1);  /* 1-indexed cursor */
}

/* ---- character picker (used by ui_add) ----------------------------------
 *
 * Cursor sits on one character position; UP/DOWN cycles the alphabet at
 * that position; LEFT/RIGHT moves the cursor; X confirms field; B cancels.
 * Alphabet covers what users actually need for account names and base32
 * secrets — uppercase letters first (most common in secret strings),
 * digits, then lowercase, then a few punctuation chars and space.
 */
static const char CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567abcdefghijklmnopqrstuvwxyz0189 -_.";
#define CHARS_LEN (sizeof(CHARS) - 1u)

static uint8_t char_idx(char c) {
    for (uint8_t i = 0u; i < CHARS_LEN; i++) {
        if (CHARS[i] == c) return i;
    }
    return 0u;
}

/* Read a string from the user. `out` is null-terminated on return.
 * `prompt` is shown on the top screen. `max_len` includes the null. */
static int ui_read_string(const char *prompt, char *out, uint8_t max_len) {
    ui_clear_screen(&topConsole);
    ui_clear_screen(&bottomConsole);

    consoleSelect(&topConsole);
    iprintf("totp-nds  -  add account\n\n%s\n", prompt);

    char buf[64];
    memset(buf, ' ', sizeof(buf));
    /* Pre-fill with however much of `out` was passed in (if anything). */
    uint8_t prefix = 0u;
    if (out[0]) {
        prefix = (uint8_t)strlen(out);
        if (prefix > max_len - 1u) prefix = max_len - 1u;
        memcpy(buf, out, prefix);
    }
    buf[max_len - 1u] = '\0';

    uint8_t cursor = prefix;

    for (;;) {
        ui_at(&topConsole, 5, 0);
        iprintf("  [%-*s]", (int)(max_len - 1u), buf);

        /* Underline cursor position on the next line. */
        ui_at(&topConsole, 6, 0);
        iprintf("   ");
        for (uint8_t i = 0u; i < (uint8_t)(max_len - 1u); i++) {
            iprintf("%c", i == cursor ? '^' : ' ');
        }

        ui_at(&bottomConsole, 0, 0);
        iprintf("LEFT/RIGHT  move cursor\n"
                "UP/DOWN     cycle character\n"
                "L / R       jump A/Z\n"
                "X / START   confirm\n"
                "B           cancel\n");

        swiWaitForVBlank();
        scanKeys();
        u32 keys = keysDown();

        if (keys & KEY_B)                        return 0;
        if (keys & (KEY_X | KEY_START))          break;

        if (keys & KEY_LEFT  && cursor > 0u)              cursor--;
        if (keys & KEY_RIGHT && cursor + 1u < (uint8_t)(max_len - 1u)) cursor++;

        char c = buf[cursor];
        uint8_t idx = char_idx(c == ' ' ? ' ' : c);
        if (keys & KEY_UP)   idx = (uint8_t)((idx + 1u) % CHARS_LEN);
        if (keys & KEY_DOWN) idx = (uint8_t)((idx == 0u) ? (CHARS_LEN - 1u) : (idx - 1u));
        if (keys & KEY_L)    idx = 0u;
        if (keys & KEY_R)    idx = (uint8_t)(CHARS_LEN - 1u);
        if (keys & (KEY_UP | KEY_DOWN | KEY_L | KEY_R)) {
            buf[cursor] = CHARS[idx];
        }
    }

    /* Trim trailing spaces; copy to caller. */
    int end = max_len - 2;
    while (end >= 0 && buf[end] == ' ') buf[end--] = '\0';
    strncpy(out, buf, max_len - 1u);
    out[max_len - 1u] = '\0';
    return 1;
}

/* ---- add account ---------------------------------------------------- */
static void ui_add(void) {
    Account a;
    memset(&a, 0, sizeof(a));

    if (!ui_read_string("name:",   a.name,   ACCOUNT_NAME_LEN)) return;
    if (!ui_read_string("secret (base32):", a.secret, SECRET_B32_LEN)) return;

    if (storage_count() >= MAX_ACCOUNTS) {
        ui_clear_screen(&topConsole);
        consoleSelect(&topConsole);
        iprintf("\n  Account list full (%d).\n  Delete one first.\n",
                MAX_ACCOUNTS);
        ui_clear_screen(&bottomConsole);
        consoleSelect(&bottomConsole);
        iprintf("\n  Press any key.\n");
        do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
        return;
    }
    storage_add(&a);
}

/* ---- view modal (expanded single-account view + delete prompt) ------ */
static void ui_view(uint8_t idx) {
    for (;;) {
        Account a;
        storage_get(idx, &a);

        uint32_t now  = rtc_get_epoch();
        uint32_t code = totp_generate(a.secret, now);
        uint8_t rem   = totp_seconds_remaining(now);

        ui_clear_screen(&topConsole);
        consoleSelect(&topConsole);
        iprintf("totp-nds  -  view\n"
                "\n"
                "  %s\n"
                "\n"
                "  code:    %06u\n"
                "  secret:  %s\n"
                "  remaining: %2u s\n",
                a.name,
                (unsigned)(code == 0xFFFFFFFFu ? 0u : code),
                a.secret, (unsigned)rem);

        /* Crude visual countdown bar — 30 chars wide. */
        ui_at(&topConsole, 11, 2);
        iprintf("[");
        for (uint8_t i = 0u; i < 30u; i++) iprintf(i < rem ? "#" : "-");
        iprintf("]");

        ui_clear_screen(&bottomConsole);
        consoleSelect(&bottomConsole);
        iprintf("B       back to list\n"
                "X       delete this account\n");

        swiWaitForVBlank();
        scanKeys();
        u32 keys = keysDown();
        if (keys & KEY_B) return;
        if (keys & KEY_X) {
            /* Confirm delete on bottom screen. */
            ui_clear_screen(&bottomConsole);
            iprintf("\n Delete \"%s\"?\n"
                    "\n A   yes, delete\n"
                    " B   no, keep\n", a.name);
            for (;;) {
                swiWaitForVBlank();
                scanKeys();
                u32 k = keysDown();
                if (k & KEY_A) { storage_remove(idx); return; }
                if (k & KEY_B) break;
            }
        }
    }
}

/* ---- main account list ----------------------------------------------- */
void ui_main(void) {
    uint8_t  sel       = 0u;
    uint32_t last_now  = 0xFFFFFFFFu;
    uint8_t  last_sel  = 0xFFu;
    uint8_t  last_cnt  = 0xFFu;

    for (;;) {
        uint8_t  cnt = storage_count();
        if (sel >= cnt && cnt > 0u) sel = (uint8_t)(cnt - 1u);
        uint32_t now = rtc_get_epoch();

        if (now != last_now || sel != last_sel || cnt != last_cnt) {
            ui_clear_screen(&topConsole);
            consoleSelect(&topConsole);
            iprintf("totp-nds  -  RFC 6238\n");
            iprintf("epoch %lu                \n\n", (unsigned long)now);

            if (cnt == 0u) {
                iprintf("\n  (no accounts yet)\n"
                        "  press START to add one.\n");
            } else {
                for (uint8_t i = 0u; i < cnt; i++) {
                    Account a;
                    storage_get(i, &a);
                    uint32_t code = totp_generate(a.secret, now);
                    iprintf(" %c %-16s %06u\n",
                            (i == sel) ? '>' : ' ',
                            a.name,
                            (unsigned)(code == 0xFFFFFFFFu ? 0u : code));
                }
                /* Countdown bar shared by all accounts (same 30-s window). */
                uint8_t rem = totp_seconds_remaining(now);
                ui_at(&topConsole, ROWS - 3, 0);
                iprintf("  next refresh in %2us\n  [", (unsigned)rem);
                for (uint8_t i = 0u; i < 28u; i++) iprintf(i < rem ? "#" : "-");
                iprintf("]");
            }

            ui_clear_screen(&bottomConsole);
            consoleSelect(&bottomConsole);
            iprintf("UP/DOWN  move selection\n"
                    "A        view + delete\n"
                    "START    add account\n"
                    "SELECT   set time\n");
            if (!storage_persistent()) {
                iprintf("\n WARN: no SD - changes\n"
                        "       will not persist.\n");
            }

            last_now = now;
            last_sel = sel;
            last_cnt = cnt;
        }

        swiWaitForVBlank();
        scanKeys();
        u32 keys = keysDown();
        if (keys & KEY_UP   && sel > 0u)        { sel--; last_sel = 0xFFu; }
        if (keys & KEY_DOWN && sel + 1u < cnt)  { sel++; last_sel = 0xFFu; }
        if (keys & KEY_A    && cnt > 0u)        { ui_view(sel); last_cnt = 0xFFu; }
        if (keys & KEY_START)                   { ui_add();     last_cnt = 0xFFu; }
        if (keys & KEY_SELECT) {
            uint32_t e = ui_timeset(rtc_get_epoch());
            rtc_set_epoch(e);
            storage_set_epoch(e);
            last_cnt = 0xFFu;
        }
    }
}

/* ---- time-set screen ------------------------------------------------- */
uint32_t ui_timeset(uint32_t current) {
    uint16_t yr; uint8_t mo, dy, hr, mn, sc;
    epoch_to_ymd(current, &yr, &mo, &dy, &hr, &mn, &sc);

    uint8_t field = 0u;   /* 0=Y 1=M 2=D 3=h 4=m 5=s */
    const char *labels[6] = {"year", "month", "day", "hour", "min", "sec"};

    for (;;) {
        ui_clear_screen(&topConsole);
        consoleSelect(&topConsole);
        iprintf("totp-nds  -  set time (UTC)\n\n");
        iprintf("  %c%04u-%c%02u-%c%02u %c%02u:%c%02u:%c%02u\n",
                field == 0 ? '>' : ' ', (unsigned)yr,
                field == 1 ? '>' : ' ', (unsigned)mo,
                field == 2 ? '>' : ' ', (unsigned)dy,
                field == 3 ? '>' : ' ', (unsigned)hr,
                field == 4 ? '>' : ' ', (unsigned)mn,
                field == 5 ? '>' : ' ', (unsigned)sc);
        iprintf("\n  editing: %s\n", labels[field]);

        ui_clear_screen(&bottomConsole);
        consoleSelect(&bottomConsole);
        iprintf("LEFT/RIGHT   move field\n"
                "UP/DOWN      adjust +-1\n"
                "L / R        adjust +-10\n"
                "A / START    confirm\n"
                "B            cancel\n");

        swiWaitForVBlank();
        scanKeys();
        u32 keys = keysDown();

        if (keys & KEY_B)               return current;
        if (keys & (KEY_A | KEY_START)) return ymd_to_epoch(yr, mo, dy, hr, mn, sc);

        if (keys & KEY_LEFT  && field > 0u)  field--;
        if (keys & KEY_RIGHT && field < 5u)  field++;

        int delta = 0;
        if (keys & KEY_UP)   delta = +1;
        if (keys & KEY_DOWN) delta = -1;
        if (keys & KEY_R)    delta = +10;
        if (keys & KEY_L)    delta = -10;
        if (delta) {
            switch (field) {
                case 0: { int v = (int)yr + delta; if (v < 1970) v = 1970; if (v > 9999) v = 9999; yr = (uint16_t)v; break; }
                case 1: { int v = (int)mo + delta; if (v < 1)    v = 1;    if (v > 12)   v = 12;   mo = (uint8_t)v;  break; }
                case 2: { int v = (int)dy + delta; if (v < 1)    v = 1;    if (v > 31)   v = 31;   dy = (uint8_t)v;  break; }
                case 3: { int v = (int)hr + delta; if (v < 0)    v = 0;    if (v > 23)   v = 23;   hr = (uint8_t)v;  break; }
                case 4: { int v = (int)mn + delta; if (v < 0)    v = 0;    if (v > 59)   v = 59;   mn = (uint8_t)v;  break; }
                case 5: { int v = (int)sc + delta; if (v < 0)    v = 0;    if (v > 59)   v = 59;   sc = (uint8_t)v;  break; }
            }
        }
    }
}

/* Internal init — called from main.c after both screens are video-on. */
void _ui_init_consoles(void);
void _ui_init_consoles(void) {
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topConsole,    3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true,  true);
    consoleInit(&bottomConsole, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
}
