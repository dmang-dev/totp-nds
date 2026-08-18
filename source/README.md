# source/

C sources for `totp-nds`. ARM946E-S, devkitARM + libnds + libfat.

## Crypto core (byte-identical with sibling `totp-*` repos)

| File | Purpose |
|---|---|
| `sha1.c` | FIPS 180-4 SHA-1. Pure C, no NDS includes. |
| `hmac.c` | HMAC-SHA1 per RFC 2104. |
| `base32.c` | Base32 decode (RFC 4648). |
| `totp.c` | TOTP per RFC 6238. |
| `datetime.c` | epoch <-> Y/M/D. |

Edits to these MUST be mirrored across the family, or the ports silently
diverge. Note the shape is not uniform: `sha1.c`, `hmac.c`, `base32.c` and
`totp.c` are in all five repos, but `datetime.c` is **not present in
`totp-gb`** — don't copy it there. `totp-gb` also keeps its sources in
`src/` rather than `source/`.

Compare blob hashes (`git -C <repo> rev-parse HEAD:source/sha1.c`), not
files on disk — these repos check out with different line endings, so
`diff` reports divergence that isn't real.

## Platform glue (NDS-specific)

| File | Purpose |
|---|---|
| `main.c` | Boot, self-test (printed on bottom screen), dispatch loop. |
| `ui.c` | Dual-screen PrintConsole UI (top = codes, bottom = menus + key hints). |
| `rtc.c` | NDS hardware RTC anchor via `time()` + user override on time-set screen. |
| `storage.c` | libfat-backed save at `fat:/totp-nds.dat`. Volatile-mode fallback if libfat init fails. |
| `ntp.c` | SNTPv3 client over DSi WiFi. Compiled in only when `-DDSI_BUILD` is set. |
