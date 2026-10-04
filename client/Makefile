.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=/opt/devkitpro/devkitARM")
endif

# Banner shown by the DS menu and loaders. Set before ds_rules reads them; the
# icon path is fixed in the first pass and inherited by the one inside build/.
VERSION         := 1.1.0
GAME_TITLE      := DS Shop
GAME_SUBTITLE1  := Game downloader
GAME_SUBTITLE2  := v$(VERSION)
export GAME_ICON ?= $(CURDIR)/icon.bmp

include $(DEVKITARM)/ds_rules

TARGET      := ds-shop
BUILD       := build
SOURCES     := source
INCLUDES    := include

ARCH        := -march=armv5te -mtune=arm946e-s -mthumb

CFLAGS      := -g -Wall -O2 -ffunction-sections -fdata-sections \
               $(ARCH) \
               $(INCLUDE) \
               -DARM9 -DDS_SHOP_VERSION=\"$(VERSION)\"

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS      = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -ldswifi9 -lfat -lnds9
LIBDIRS     := $(LIBNDS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.bin)))

ifeq ($(strip $(CPPFILES)),)
export LD   := $(CC)
else
export LD   := $(CXX)
endif

export OFILES   := $(BINFILES:.bin=.o) \
                   $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile DEFINES="$(DEFINES)"

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS     := $(OFILES:.o=.d)

$(OUTPUT).nds   : $(OUTPUT).elf
$(OUTPUT).elf   : $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
