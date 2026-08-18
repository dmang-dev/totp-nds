#---------------------------------------------------------------------------
# devkitPro NDS Makefile for totp-nds (ARM9-only, libnds + libfat).
# Adapted from $(DEVKITPRO)/examples/nds/templates/arm9.
#---------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

GAME_TITLE     := totp-nds
GAME_SUBTITLE1 := TOTP authenticator (RFC 6238)
GAME_SUBTITLE2 := built with devkitARM + libnds

include $(DEVKITARM)/ds_rules

TARGET   := totp-nds
BUILD    := build
SOURCES  := source
INCLUDES := include
DATA     :=
GRAPHICS :=

ARCH     := -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections \
            $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# libfat for SD/flashcart persistence, dswifi9 for the DSi-mode NTP
# sync (link is harmless on the .nds build — unused dswifi9 functions
# don't pull anything in unless DSI_BUILD code references them), and
# libnds9 for everything else.
LIBS     := -lfat -ldswifi9 -lnds9
LIBDIRS  := $(LIBNDS) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

.PHONY: dsi release
# Release build: produces both totp-nds.nds (universal, no DSi WiFi) and
# totp-nds.dsi (DSi-mode with WiFi NTP). The `dsi` target overwrites
# build/ with DSI_BUILD-defined object files, so we sequence: clean →
# .nds → save → clean → .dsi → restore. CI calls this single target.
release:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory
	@cp $(TARGET).nds $(TARGET).nds.tmp
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory dsi
	@mv $(TARGET).nds.tmp $(TARGET).nds
	@echo built ... $(TARGET).nds + $(TARGET).dsi

# DSi-enhanced build. Two differences from the universal .nds build:
#
#   1. Re-link the same source tree with CPPFLAGS=-DDSI_BUILD so the
#      WiFi/NTP code in ntp.c is compiled in. The .nds build builds
#      the same files but ntp_sync() is a no-op stub there.
#   2. Repackage via ndstool with the DSi header (unit code 0x03,
#      0x4000-byte header, DSi title-ID prefix). Home Menu / TWiLight
#      Menu++ recognize it as a DSi app instead of "DS-mode running
#      on DSi".
#
# The relinked ELF reuses build/ — `make dsi` after a fresh `make` will
# repickle the ELF only if DSI_BUILD changed any compilation. To force a
# clean DSi-only build use `make clean && make dsi`.
dsi:
	@$(MAKE) --no-print-directory CPPFLAGS=-DDSI_BUILD $(BUILD)
	@ndstool -c $(TARGET).dsi -9 $(TARGET).elf \
		-7 $(CALICO)/bin/ds7_maine.elf \
		-h 0x4000 -u 0x00030004 \
		-b $(CALICO)/share/nds-icon.bmp "$(GAME_TITLE) (DSi);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)"
	@echo built ... $(TARGET).dsi

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).dsi

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nds : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

endif
