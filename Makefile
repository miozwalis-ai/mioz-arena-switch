# MIOZ ZOMBIE WORLD
# Nintendo Switch - devkitA64 / libnx / SDL2

.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO n'est pas defini dans l'environnement")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

# Nom du programme
TARGET := MIOZ_ZOMBIE_WORLD_SWITCH

# Dossiers du projet
BUILD := build
SOURCES := source
DATA :=
INCLUDES := include

# Architecture Nintendo Switch
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# Compilation C/C++
CFLAGS := -g -Wall -O2 -ffunction-sections \
          $(ARCH) \
          -D__SWITCH__ \
          $(INCLUDE) \
          $(shell sdl2-config --cflags)

CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions

ASFLAGS := -g $(ARCH)

# Edition de liens
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs \
           -g $(ARCH) \
           -Wl,-Map,$(notdir $*).map

LIBS := $(shell sdl2-config --libs)

# Dossiers des bibliotheques
LIBDIRS := $(PORTLIBS) $(LIBNX)

# Informations de l'application
APP_TITLE := MIOZ ZOMBIE WORLD
APP_AUTHOR := MIOZ
APP_VERSION := 1.0.0

# Compilation finale
