.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# ----------------------------------------------------------------------
# Noms & chemins
# ----------------------------------------------------------------------
TARGET      := mioz-arena-switch
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include
ROMFS       := romfs

# ----------------------------------------------------------------------
# Architecture
# ----------------------------------------------------------------------
ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# ----------------------------------------------------------------------
# Flags de compilation (optimisés pour la 3D sur Switch)
# ----------------------------------------------------------------------
CFLAGS      := -g -Wall -Wextra -O3 -ffunction-sections -fdata-sections \
               $(ARCH) $(DEFINES)
CFLAGS      += -D__SWITCH__ -D__SDL2__ \
               $(INCLUDE) `sdl2-config --cflags`

# Note : -ffast-math est OK ici car on ne fait pas de physique critique.
# -fno-rtti/-fno-exceptions allègent le binaire (SDL n'en a pas besoin).
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions \
               -fno-threadsafe-statics

# LTO : active l'optimisation inter-modules (gain notable)
ifneq ($(strip $(NOLTO)),)
CXXFLAGS    += -fno-lto
LDFLAGS_LTO :=
else
CXXFLAGS    += -flto
LDFLAGS_LTO := -flto
endif

ASFLAGS     := -g $(ARCH)

# ----------------------------------------------------------------------
# Flags de linkage
# ----------------------------------------------------------------------
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
              $(LDFLAGS_LTO) \
              -Wl,-Map,$(notdir $*.map) \
              -Wl,--gc-sections -Wl,-s

LIBS        := `sdl2-config --libs` -lnx
LIBDIRS     := $(PORTLIBS) $(LIBNX)

# ----------------------------------------------------------------------
# Règles pour le sous-make
# ----------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

# Choix du linker
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

# Objets
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)

# Header de dépendances
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Romfs (si présent)
export ROMFS  := $(CURDIR)/$(ROMFS)
export NROFLAGS := --romfsdir=$(ROMFS)

.PHONY: all clean $(BUILD) nro

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD) \
	        $(TARGET).nro $(TARGET).nacp $(TARGET).elf \
	        $(TARGET).nsp $(TARGET).nso $(TWARGET).mapelse

# ======================================================================
# Sous-make : compilation réelle
# ======================================================================
.PHONY: all clean

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

# ----------------------------------------------------------------------
# NRO : dépend de l'ELF + NACP (métadonnées)
# ----------------------------------------------------------------------
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS) --nacp=$(OUTPUT).nacp
	@echo "==> Built $(notdir $@)"

# ----------------------------------------------------------------------
# NACP : métadonnées (titre, auteur, version)
# ----------------------------------------------------------------------
$(OUTPUT).nacp: $(OUTPUT).elf
	@nacptool --create "Mioz Zombie World" \
	          "Mioz" "1.0.0" $@
	@echo "==> Built $(notdir $@)"

# ----------------------------------------------------------------------
# ELF : linkage final
# ----------------------------------------------------------------------
$(OUTPUT).elf: $(OFILES)
	@echo "==> Linking $(notdir $@)"
	$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

# ----------------------------------------------------------------------
# Règle générique .cpp -> .o
# ----------------------------------------------------------------------
%.o: %.cpp
	@echo "==> Compiling $<"
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(DEPSDIR)/$*.d -c $< -o $@

# ----------------------------------------------------------------------
# Règle générique .c -> .o
# ----------------------------------------------------------------------
%.o: %.c
	@echo "==> Compiling $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPSDIR)/$*.d -c $< -o $@

# ----------------------------------------------------------------------
# Règle générique .s -> .o
# ----------------------------------------------------------------------
%.o: %.s
	@echo "==> Assembling $<"
	$(CC) $(ASFLAGS) -c $< -o $@

# ----------------------------------------------------------------------
# Binaires (données brutes)
# ----------------------------------------------------------------------
%.o: %
	@echo "==> Converting $<"
	bin2s $< | $(CC) $(ARCH) -x assembler-with-cpp -c -o $@ -

# ----------------------------------------------------------------------
# Dépendances
# ----------------------------------------------------------------------
-include $(DEPENDS)

endif
