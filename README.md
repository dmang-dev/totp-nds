# totp-nds

Native **Nintendo DS / DS Lite** TOTP (RFC 6238) authenticator. Same
protocol as Google Authenticator — add accounts, get rotating 6-digit
codes — but running on an ARM946E-S / ARM7TDMI dual-CPU console from
2004.

Sibling repo to [`totp-gb`](https://github.com/dmang-dev/totp-gb),
[`totp-gba`](https://github.com/dmang-dev/totp-gba), and
[`totp-psp`](https://github.com/dmang-dev/totp-psp). The crypto core
(SHA-1 / HMAC / Base32 / TOTP / date math) is shared byte-for-byte
across all four; this project adds NDS-specific platform glue:

- **Hardware RTC** — Toshiba TC8521 on the NDS mainboard, read via
  newlib's `time()` (libnds wires it up over the ARM7/ARM9 IPC FIFO).
  User can override via the time-set screen if codes come out wrong.
- **Storage** — file at `fat:/totp-nds.dat` on whatever filesystem
  libfat mounts (DSi SD slot, R4-class DLDI-patched flashcart, etc.).
  Same byte layout as `totp-gba`/`totp-psp` so the same offsets apply.
- **UI** — libnds `PrintConsole` text mode, 32×24 chars per screen,
  with the top screen showing live codes and the bottom screen
  showing menus + key hints.

[![ROM](https://img.shields.io/badge/ROM-prebuilt%20%26%20committed-success)](totp-nds.nds)
[![Built with devkitARM](https://img.shields.io/badge/built%20with-devkitARM%20%2B%20libnds-orange)](https://devkitpro.org)
[![Algorithm verified](https://img.shields.io/badge/HMAC--SHA1-RFC%206238-blue)](#testing)
[![Format](https://img.shields.io/badge/format-.nds-lightgrey)](https://www.nesdev.org/wiki/NDS)

---

## Try it

Two pre-built artifacts are checked in:

| File | Use when | Why |
|---|---|---|
| [`totp-nds.nds`](totp-nds.nds) | DS / DS Lite (DLDI flashcart), or DSi/3DS via DS-mode | Universal — runs everywhere via DS-mode |
| [`totp-nds.dsi`](totp-nds.dsi) | DSi / 2DS / 3DS exclusively | DSi-enhanced header (unit code 0x03, 0x4000-byte header) — Home Menu / TWiLight Menu++ recognizes it as a DSi app rather than a DS-mode ROM. Same binary semantics for now; v1.1+ adds DSi-only features (WiFi NTP) and v2.0+ adds DSi camera support |

### DSi / 3DS

Drop either file on the SD card. Use `.dsi` for cleanest Home Menu /
launcher integration; use `.nds` if your launcher only handles the
plain format.

### Original DS / DS Lite

Use `totp-nds.nds`. Requires a DLDI-patched flashcart (R4, EZ-Flash,
etc.). The flashcart loader handles DLDI patching transparently on
most modern cards.

### Emulators

DeSmuME, melonDS, no$gba all run `totp-nds.nds` directly. For the
`.dsi`, use melonDS in DSi-mode (DeSmuME doesn't emulate DSi). melonDS
is also recommended for the most accurate RTC behavior.

First boot lands on the time-set screen so the software RTC isn't off
by years. After that you're on the live account list.

---

## Controls

| Button | Action |
|---|---|
| **UP / DOWN** | move selection in the list |
| **A** | view selected account (expanded, with countdown bar) |
| **B** | back from any modal |
| **X** | (in view) delete selected account |
| **START** | add new account |
| **SELECT** | open the time-set screen |
| **L / R** | (in char picker) jump to A / Z in the alphabet |

### Add account char picker

Cursor over one character at a time, cycle through the alphabet at
that position with UP/DOWN; LEFT/RIGHT to move the cursor; L/R to
jump to the ends; X or START to confirm; B to cancel.

---

## Testing

Two layers, matching the GB / GBA / PSP siblings:

1. **Boot self-test on hardware.** Every boot runs the four RFC 6238
   known-answer vectors against `JBSWY3DPEHPK3PXP` and prints PASS/FAIL
   on the bottom screen for ~1 second before handing off to the live
   list. On failure, the screen stops with a visible error for 5
   seconds — a broken build can't quietly generate wrong codes. Skip
   with `make CPPFLAGS=-DSKIP_SELFTEST`.

   ```
   totp-nds self-test (RFC 6238)
   secret: JBSWY3DPEHPK3PXP ("Hello!")

     epoch=0          got=282760 expected=282760  PASS
     epoch=1234567890 got=742275 expected=742275  PASS
     epoch=1778088090 got=283711 expected=283711  PASS
     epoch=1778088141 got=113232 expected=113232  PASS

     result: 4/4 PASS
   ```

2. **Host KAT runner in [`tests/`](tests/).** Same vectors, compiled
   with the system's native gcc against the same `source/sha1.c`,
   `hmac.c`, `base32.c`, `totp.c` that go into `totp-nds.nds`. Returns
   exit 1 on any failure, so CI fails on a regression. Run locally
   with:

   ```bash
   cd tests && make check
   ```

CI runs both layers on every push — see
[`.github/workflows/build.yml`](.github/workflows/build.yml).

---

## Build from source

Requires **devkitPro** with the `nds-dev` meta-package (includes
devkitARM, libnds, libfat, Calico): <https://github.com/devkitPro/installer/releases>

```
make            # builds totp-nds.nds (universal)
make dsi        # builds totp-nds.dsi (DSi-enhanced header)
make clean
```

Or use the Windows convenience wrapper:

```
.\build.bat
.\build.bat dsi
.\build.bat clean
```

The wrapper sets `DEVKITPRO=C:/devkitPro` (the default install path) and
hands off to MSYS2's `make`.

---

## Layout

```
source/
  main.c                  boot + self-test + dispatch
  ui.c                    dual-screen PrintConsole UI
  rtc.c                   NDS hardware RTC anchor + override
  storage.c               libfat-backed save at fat:/totp-nds.dat
  sha1.c hmac.c base32.c  crypto core (shared with siblings)
  totp.c datetime.c
include/                  C headers
tests/                    host-runnable KAT harness
.github/workflows/        CI
Makefile                  devkitARM + libnds Makefile
build.bat                 Windows convenience wrapper
totp-nds.nds              prebuilt ROM (committed)
```

---

## Implementation notes

### Why a software RTC anchor

The NDS hardware RTC is wall-clock-only — no timezone field, year
limited to 2000-2099. We read it once at boot, treat it as UTC by
convention, and let the user override via the time-set screen. The
override is anchored to the system tick counter so the clock keeps
advancing even if you never re-confirm.

### Why libfat instead of NDS save SRAM

NDS cartridges have save chips, but accessing them at runtime requires
flashcart-specific tricks that don't generalize across cards. libfat
gives us a portable filesystem that works on every DLDI-patched cart,
the DSi SD slot, and every emulator that supports DLDI / SD
(DeSmuME, melonDS, no$gba). Cost: if libfat init fails, the app
shows a warning and runs in volatile mode (changes lost at power-off).

### Why two screens

NDS UX feels wrong with a single screen — the second display is too
obvious to leave dark. We use the bottom screen exclusively for menus
+ key hints (the user is always looking at it for context), and the
top screen for live code display (so codes don't visually collide with
the controls). Bonus: makes the app legible across DS / DS Lite / DSi
without scaling.

### Why not a touchscreen keyboard

It'd be nicer, but the char-picker pattern is shared with `totp-psp`
and `totp-gba` — typing a 32-char base32 secret once is rare enough
that the gain wasn't worth diverging the input flow. Touchscreen
keyboard tracked as a future enhancement.

---

## Acknowledgments

- [devkitPro / devkitARM + libnds](https://devkitpro.org/) —
  ARM toolchain and NDS SDK
- [libfat](https://github.com/devkitPro/libfat) — portable FAT
  filesystem driver
- [no$gba](https://problemkaputt.de/gba.htm), [DeSmuME](https://desmume.org/),
  [melonDS](https://melonds.kuribo64.net/) — emulators used for
  development and verification

---

## License

Released under the [MIT License](LICENSE).
