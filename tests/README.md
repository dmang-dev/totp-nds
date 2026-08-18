# tests/

Host-runnable known-answer-vector harness for the totp-nds crypto core.

The crypto sources (`sha1.c`, `hmac.c`, `base32.c`, `totp.c` under
`../source/`) are pure ISO C with no NDS includes — they're the same
files that get linked into `totp-nds.nds`, just driven by a different
`main()`. If KATs pass here, they pass on-device.

## Run

Requires a host C compiler with stdlib headers (any normal `gcc`,
`clang`, or MSVC). The devkitARM cross-compiler does **not** work —
it targets ARM and ships without host libc headers.

```bash
cd tests
make check        # builds run_kats and runs it; exit 1 on any failure
```

On Windows without MinGW installed, this is exercised in CI instead
(see `.github/workflows/build.yml`).

## What it checks

Four vectors against the RFC 6238 test secret `JBSWY3DPEHPK3PXP`
(base32-encoded `"Hello!"`). These match the vectors used by every
sibling — `totp-gb/tests/`, `totp-gba/tests/`, `totp-3ds/tests/` and
`totp-psp/tests/` — so a single break in the shared crypto core fails CI
across all five repos. (`totp-3ds` and `totp-psp` use the same
`tests/run_kats.c` host harness as this repo; `totp-gb` and `totp-gba`
drive a headless emulator instead.)
