CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -Wall -Wextra -std=c99
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
ifneq ($(strip $(SDL2_LIBS)),)
CPPFLAGS += -Iinclude $(SDL2_CFLAGS) -DCHIP8_HAVE_SDL2
LDLIBS += $(SDL2_LIBS)
else
CPPFLAGS += -Iinclude
$(warning SDL2 development files not found; live sound is unavailable until SDL2 is installed)
endif

SOURCES = src/main.c src/chip8.c src/audio.c
TARGET = chip8

all: $(TARGET)

$(TARGET): $(SOURCES) include/chip8.h include/audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS) -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean
