# Makefile for 'Jutils' utilities
# Author: Jerry Richardson
# Copyright 2025

# ---- Configuration ----
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11
PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin

# ---- Programs ----
PROGS := clipit kernmem toolchain-env.sh kernel_cleanup.sh

all: $(PROGS)

clipit: clipit.c
	$(CC) $(CFLAGS) -o $@ $<

kernmem: kernmem.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(PROGS)
	install -d $(BINDIR)
	install -m 0755 $(PROGS) $(BINDIR)

clean:
	rm -f $(PROGS) *.o

.PHONY: all clean install

