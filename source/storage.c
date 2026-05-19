/*
 * libfat-backed persistence for totp-nds.
 *
 * fatInitDefault() probes for the DSi SD slot first, then DLDI-patched
 * slot-1 / slot-2 flashcarts. If it succeeds we mount at "fat:/" and
 * persist to /totp-nds.dat. If it fails (no SD, no DLDI driver, etc.)
 * we run in volatile mode — the in-memory image works, but changes
 * vanish at power-off. The UI surfaces this via storage_persistent().
 */
#include "storage.h"

#include <fat.h>
#include <stdio.h>
#include <string.h>

#define OFF_MAGIC     0u
#define OFF_EPOCH     2u
#define OFF_COUNT     6u
#define OFF_RESERVED  7u
#define OFF_ACCOUNTS  8u
#define ACCOUNT_SIZE  (ACCOUNT_NAME_LEN + SECRET_B32_LEN)
#define STORE_BYTES   (OFF_ACCOUNTS + (uint32_t)ACCOUNT_SIZE * MAX_ACCOUNTS)

#define MAGIC_0    0xAAu
#define MAGIC_1    0x55u

#define STORE_PATH  "fat:/totp-nds.dat"

static uint8_t mem[STORE_BYTES];
static uint8_t s_persistent = 0u;

static uint32_t r32(uint16_t off) {
    return  (uint32_t)mem[off]
         | ((uint32_t)mem[off + 1u] <<  8)
         | ((uint32_t)mem[off + 2u] << 16)
         | ((uint32_t)mem[off + 3u] << 24);
}
static void w32(uint16_t off, uint32_t v) {
    mem[off]      = (uint8_t)( v        & 0xFFu);
    mem[off + 1u] = (uint8_t)((v >>  8) & 0xFFu);
    mem[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    mem[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

static void store_flush(void) {
    if (!s_persistent) return;
    FILE *f = fopen(STORE_PATH, "wb");
    if (!f) return;
    fwrite(mem, 1, STORE_BYTES, f);
    fclose(f);
}

static void store_format(void) {
    memset(mem, 0, sizeof(mem));
    mem[OFF_MAGIC]      = MAGIC_0;
    mem[OFF_MAGIC + 1u] = MAGIC_1;
    store_flush();
}

uint8_t storage_init(void) {
    s_persistent = fatInitDefault() ? 1u : 0u;

    if (!s_persistent) {
        /* Volatile mode — start with a clean in-memory image. */
        store_format();
        return 0u;
    }

    FILE *f = fopen(STORE_PATH, "rb");
    if (!f) {
        store_format();
        return 0u;
    }
    size_t n = fread(mem, 1, STORE_BYTES, f);
    fclose(f);
    if (n < STORE_BYTES
        || mem[OFF_MAGIC] != MAGIC_0
        || mem[OFF_MAGIC + 1u] != MAGIC_1) {
        store_format();
        return 0u;
    }
    return 1u;
}

uint8_t storage_persistent(void) { return s_persistent; }

uint32_t storage_get_epoch(void)        { return r32(OFF_EPOCH); }
void     storage_set_epoch(uint32_t e)  { w32(OFF_EPOCH, e); store_flush(); }

uint8_t  storage_get_theme(void)        { return mem[OFF_RESERVED] & 0x7Fu; }
void     storage_set_theme(uint8_t idx) {
    mem[OFF_RESERVED] = (mem[OFF_RESERVED] & 0x80u) | (idx & 0x7Fu);
    store_flush();
}

uint8_t storage_count(void) { return mem[OFF_COUNT]; }

static uint16_t acct_off(uint8_t idx) {
    return (uint16_t)(OFF_ACCOUNTS + (uint16_t)idx * ACCOUNT_SIZE);
}

void storage_get(uint8_t idx, Account *out) {
    if (idx >= mem[OFF_COUNT]) {
        memset(out, 0, sizeof(*out));
        return;
    }
    uint16_t o = acct_off(idx);
    memcpy(out->name,   &mem[o],                   ACCOUNT_NAME_LEN);
    memcpy(out->secret, &mem[o + ACCOUNT_NAME_LEN], SECRET_B32_LEN);
}

uint8_t storage_add(const Account *acct) {
    if (mem[OFF_COUNT] >= MAX_ACCOUNTS) return 0u;
    uint16_t o = acct_off(mem[OFF_COUNT]);
    memcpy(&mem[o],                   acct->name,   ACCOUNT_NAME_LEN);
    memcpy(&mem[o + ACCOUNT_NAME_LEN], acct->secret, SECRET_B32_LEN);
    mem[OFF_COUNT]++;
    store_flush();
    return 1u;
}

void storage_remove(uint8_t idx) {
    if (idx >= mem[OFF_COUNT]) return;
    uint8_t n = mem[OFF_COUNT];
    if (idx + 1u < n) {
        uint16_t src = acct_off(idx + 1u);
        uint16_t dst = acct_off(idx);
        memmove(&mem[dst], &mem[src],
                (size_t)ACCOUNT_SIZE * (n - idx - 1u));
    }
    mem[OFF_COUNT]--;
    memset(&mem[acct_off(mem[OFF_COUNT])], 0, ACCOUNT_SIZE);
    store_flush();
}
