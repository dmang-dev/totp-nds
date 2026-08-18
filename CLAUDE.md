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

- **Crypto core is byte-identical across the `totp-*` family.** Any edit
  MUST be mirrored, or the five ports silently diverge. The shape is not
  uniform, so check before copying:
  - `sha1`, `hmac`, `base32`, `totp` — both `.c` and `.h` — are in **all
    five** repos: here plus `totp-gb`, `totp-gba`, `totp-3ds`, `totp-psp`.
  - `datetime.c` / `datetime.h` are in **four**. `totp-gb` does not have
    them and does not need them. Don't "fix" that by copying them in.
  - `totp-gb` keeps sources *and* headers in `src/`. The other four split
    `source/` + `include/`.
- **Verify the invariant with blob hashes, never with files on disk.**
  These repos check out with different line endings, so `diff` and
  `sha256sum` over working-tree copies report divergence that isn't real.
  Compare what git actually stores:

  ```
  git -C <repo> rev-parse HEAD:source/sha1.c   # src/sha1.c in totp-gb
  ```

  All five agreed as of v1.1.0. For the same reason, don't trust `grep`
  or `awk` to detect CR bytes in this MSYS shell — both have returned
  confidently wrong answers here. Use `git cat-file -s` / `git ls-files
  --eol`.
- **Boot self-test validates RFC 6238 vectors before UI starts.** Don't
  bypass it. On failure, the screen stops for 5 seconds with a visible
  error — a broken build can't quietly generate wrong codes. `make
  CPPFLAGS=-DSKIP_SELFTEST` only for crypto-unrelated UI work — and note
  that `make dsi` already passes `CPPFLAGS=-DDSI_BUILD` on the command
  line, so combining the two needs
  `make dsi CPPFLAGS="-DDSI_BUILD -DSKIP_SELFTEST"` (a bare
  `CPPFLAGS=-DSKIP_SELFTEST` would silently drop `DSI_BUILD` and build
  the stub NTP path into the `.dsi`).
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
  `ntp.c` times out and falls through to the manual time-set screen.
  `ntp.c` is compiled into *both* builds — the Makefile globs
  `source/*.c` — but only its body is gated on `-DDSI_BUILD`; the `.nds`
  build gets a stub `ntp_sync()` that always returns `NTP_WIFI_FAIL`.
- **Never accept the hardware RTC reading unconfirmed.** The NDS RTC has
  no timezone field and users overwhelmingly set it to local time, while
  we interpret the reading as UTC. Auto-trusting it (e.g. "the year looks
  sane, ship it") silently produces codes off by the user's UTC offset
  with nothing on screen to explain why. On a boot with no saved epoch it
  may only pre-fill `ui_timeset()`. This regressed once on `wip/ntp` —
  don't reintroduce it.
- **NDS hardware RTC is wall-clock-only** (no timezone, year 2000-2099).
  We read once at boot, treat as UTC by convention, anchor to the system
  tick. Same trade-off as `totp-gba`'s Seiko S-3511A.
