#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/*
 * File-backed persistence on whatever filesystem libfat mounts at boot
 * (DSi SD slot, R4-class flashcart's SD, etc.). Save file lives at
 * "fat:/totp-nds.dat".
 *
 * Byte layout is identical to totp-gba's SRAM and totp-psp's savedata
 * file, so a future import/export tool could shuttle accounts between
 * the family of authenticators:
 *   0x0000  magic[2]   = 0xAA 0x55
 *   0x0002  epoch[4]   little-endian uint32 (Unix epoch when saved)
 *   0x0006  count[1]   number of accounts stored (0..MAX_ACCOUNTS)
 *   0x0007  reserved   bits 0-6 = theme idx, bit 7 = sound mute (unused on NDS)
 *   0x0008+ accounts[] each 48 bytes: 16 name + 32 secret
 */

#define ACCOUNT_NAME_LEN  16
#define SECRET_B32_LEN    32
#define MAX_ACCOUNTS      8

typedef struct {
    char name[ACCOUNT_NAME_LEN];
    char secret[SECRET_B32_LEN];
} Account;

/* Returns 1 if a previously-saved store was loaded; 0 if formatted fresh
 * (first boot, missing/corrupt file, or libfat init failed — in the last
 * case storage runs in volatile mode and changes are not persisted). */
uint8_t storage_init(void);

/* True if libfat is mounted and we can actually persist to disk. */
uint8_t storage_persistent(void);

uint32_t storage_get_epoch(void);
void     storage_set_epoch(uint32_t epoch);

uint8_t storage_get_theme(void);
void    storage_set_theme(uint8_t idx);

uint8_t storage_count(void);
void    storage_get(uint8_t idx, Account *out);
uint8_t storage_add(const Account *acct);
void    storage_remove(uint8_t idx);

#endif
