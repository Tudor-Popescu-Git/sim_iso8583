# Makefile for sim_iso8583 (ISO 8583 simulator): server skeleton.
# Needs GNU make and clang (default) or gcc, on Linux (on Windows: inside WSL 2).
#
#   make                 build the debug version   -> build/debug/sim_iso8583d
#   make debug           same as above
#   make release         build the release version -> build/release/sim_iso8583d
#   make run             build debug and start the server on port 8583
#   make clean           delete the build directory
#
# Options (put them after the command):
#   make CC=gcc          use gcc instead of clang
#   make SAN=1           add AddressSanitizer and UndefinedBehaviorSanitizer
#
# Changing CC or SAN rebuilds everything automatically (see the .cc stamp below).

CC     = clang
BIN    := sim_iso8583d
SRC    := $(wildcard server/src/*.c)
SAN    ?= 0

# -D_POSIX_C_SOURCE is needed because -std=c99 hides POSIX functions
# (sigaction, fork, sockets) unless the program asks for them.
CFLAGS_BASE := -std=c99 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L -Iserver/include

BUILD ?= debug
ifeq ($(BUILD),release)
  CFLAGS_EXTRA := -O2 -DNDEBUG
else
  CFLAGS_EXTRA := -g -O0 -DDEBUG
endif

ifeq ($(SAN),1)
  CFLAGS_EXTRA += -fsanitize=address,undefined -fno-omit-frame-pointer
endif

OBJDIR := build/$(BUILD)
OBJ    := $(patsubst server/src/%.c,$(OBJDIR)/%.o,$(SRC))
DEP    := $(OBJ:.o=.d)
TARGET := $(OBJDIR)/$(BIN)
STAMP  := $(OBJDIR)/.cc

.PHONY: all debug release run clean FORCE

all: debug

debug:
	@$(MAKE) --no-print-directory BUILD=debug build/debug/$(BIN)

release:
	@$(MAKE) --no-print-directory BUILD=release build/release/$(BIN)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS_BASE) $(CFLAGS_EXTRA) -o $@ $(OBJ)

# The stamp file holds the compiler and flags used for the last build.
# It is rewritten only when they change. Every object file depends on it,
# so switching compiler or flags never mixes old and new object files.
$(STAMP): FORCE
	@mkdir -p $(OBJDIR)
	@echo '$(CC) $(CFLAGS_BASE) $(CFLAGS_EXTRA)' | cmp -s - $@ || echo '$(CC) $(CFLAGS_BASE) $(CFLAGS_EXTRA)' > $@

$(OBJDIR)/%.o: server/src/%.c $(STAMP)
	$(CC) $(CFLAGS_BASE) $(CFLAGS_EXTRA) -MMD -MP -c -o $@ $<

FORCE:

-include $(DEP)

run: debug
	./build/debug/$(BIN)

clean:
	rm -rf build
