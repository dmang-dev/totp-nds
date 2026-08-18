# include/

C headers paired with `source/`. One header per implementation file.

| Header | Pairs with |
|---|---|
| `sha1.h` `hmac.h` `base32.h` `totp.h` `datetime.h` | Shared crypto core, byte-identical with sibling `totp-*` repos. |
| `rtc.h` | Hardware RTC anchor + user override. |
| `storage.h` | libfat save format + Account CRUD. |
| `ntp.h` | DSi-only SNTPv3 client. |
| `ui.h` | Dual-screen PrintConsole UI. |

Edits to the crypto-core headers MUST be mirrored across the family.
