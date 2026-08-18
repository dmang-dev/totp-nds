# CLAUDE.md — totp-nds

RFC 6238 TOTP authenticator for the Nintendo DS / DS Lite (and DSi / 3DS via
DS-mode). ARM946E-S native with devkitARM + libnds + libfat.

## Build

```
make            # totp-nds.nds (universal — DS / DS Lite / DSi-DS-mode / 3DS-DS-mode)
make dsi        # totp-nds.dsi (DSi-mode with WiFi NTP auto time-sync)
make release    # both, in the right order (used by CI)
make clean
```

Or the Windows convenience wrapper:

```
.\build.bat
.\build.bat dsi
.\build.bat release
.\build.bat clean
```

`make dsi` reuses the `build/` tree with `-DDSI_BUILD` defined, which
leaves `totp-nds.nds` containing the DSi-flavored binary. Use
`make release` if you want both artifacts in one shot — it handles the
clean / build / restore sequence.

## Toolchain

**devkitPro** with the `nds-dev` meta-package (devkitARM + libnds + libfat
+ Calico): <https://github.com/devkitPro/installer/releases>. Env vars
`DEVKITPRO=C:/devkitPro` and `DEVKITARM=$DEVKITPRO/devkitARM` (the wrapper
sets defaults).

## Layout

```
source/
  main.c                  boot + self-test + dispatch
  ui.c                    dual-screen PrintConsole UI (top = codes, bottom = menus)
  rtc.c                   NDS hardware RTC anchor + user override
  storage.c               libfat-backed save at fat:/totp-nds.dat
  ntp.c                   SNTPv3 client (DSi only via DSI_BUILD)
  sha1.c hmac.c base32.c  crypto core (shared verbatim with siblings)
  totp.c datetime.c
include/                  C headers
tests/                    Host-runnable KAT harness
.github/workflows/        CI
Makefile                  devkitARM + libnds Makefile
build.bat                 Windows convenience wrapper (MSYS2 hand-off)
totp-nds.nds              prebuilt universal ROM (committed)
totp-nds.dsi              prebuilt DSi-mode ROM (committed)
```

## Conventions

- **Crypto core (sha1 / hmac / base32 / totp / datetime) is byte-identical
  with sibling `totp-*` repos.** Any edit MUST be mirrored across the family
  (`totp-gb`, `totp-gba`, `totp-3ds`, `totp-psp`). If you touch these files
  here, copy them to the other four.
- **Boot self-test validates RFC 6238 vectors before UI starts.** Don't
  bypass it. On failure, the screen stops for 5 seconds with a visible
  error — a broken build can't quietly generate wrong codes. `make
  CPPFLAGS=-DSKIP_SELFTEST` only for crypto-unrelated UI work.
- **Built ROMs are committed at repo root** (`totp-nds.nds`,
  `totp-nds.dsi`). Both must be updated on a release.

## Common pitfalls

- **`make dsi` overwrites `totp-nds.nds` with the DSi binary.** The Make
  rule reuses the same output name. Use `make release` for the clean two-
  artifact build, not back-to-back `make` + `make dsi`.
- **libfat init can fail** on emulators without DLDI patching or on carts
  without an SD slot. `storage.c` falls back to volatile mode (changes
  lost on power-off) with a UI warning. Don't treat libfat failure as fatal.
- **DSi WiFi NTP requires a configured firmware WFC profile.** Without one,
  `ntp.c` times out and falls through to the manual time-set screen. The
  `.nds` build doesn't include `ntp.c` at all (gated by `-DDSI_BUILD`).
- **NDS hardware RTC is wall-clock-only** (no timezone, year 2000-2099).
  We read once at boot, treat as UTC by convention, anchor to the system
  tick. Same trade-off as `totp-gba`'s Seiko S-3511A.
