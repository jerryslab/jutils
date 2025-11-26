# Makefile for 'Jutils' utilities
# Author: Jerry Richardson
# Copyright 2025

# ---- Configuration ----
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11
PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin

# ---- C Programs ----
CBINS := clipit kernmem swapmon

# ---- Script Programs (installed as-is) ----
SCRIPTS := toolchain-env.sh kernel_cleanup.sh

# ---- All programs ----
PROGS := $(CBINS) $(SCRIPTS)

all: $(CBINS)

# ---- Build C binaries ----
clipit: clipit.c
	$(CC) $(CFLAGS) -o $@ $<

kernmem: kernmem.c
	$(CC) $(CFLAGS) -o $@ $<

swapmon: swapmon.c
	$(CC) $(CFLAGS) -o $@ $<

# ---- Install ----
install: $(PROGS)
	install -d $(BINDIR)
	install -m 0755 $(CBINS) $(BINDIR)
	install -m 0755 $(SCRIPTS) $(BINDIR)

# ---- Clean ----
clean:
	rm -f $(CBINS) *.o

.PHONY: all clean install

