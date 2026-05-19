# Host-side native build of the crypto KAT runner. No devkitARM
# required — this is just gcc against the same C sources that go into
# totp-nds.nds.
#
#   make            # build run_kats
#   make check      # build and run; exit 1 on any KAT failure
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99 -pedantic
INCDIR  := ../include

run_kats: run_kats.c ../source/sha1.c ../source/hmac.c ../source/base32.c ../source/totp.c
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ run_kats.c

.PHONY: check clean
check: run_kats
	./run_kats

clean:
	rm -f run_kats run_kats.exe
