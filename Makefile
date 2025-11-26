# Makefile for 'Jutils' utilities
# Author: Jerry Richardson
# Copyright 2025

# ---- Configuration ----
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11
PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin

# ---- C Programs ----
C_PROGS := clipit kernmem swapout

# ---- Shell scripts ----
SH_SCRIPTS := toolchain-env.sh kernel_cleanup.sh

# ---- All programs ----
PROGS := $(C_PROGS) $(SH_SCRIPTS)

# ---- Default rule ----
all: $(C_PROGS)

# ---- C program build rules ----
clipit: clipit.c
	$(CC) $(CFLAGS) -o $@ $<

kernmem: kernmem.c
	$(CC) $(CFLAGS) -o $@ $<

swapout: swapout.c
	$(CC) $(CFLAGS) -o $@ $<

# ---- Installation ----
install: $(PROGS)
	install -d $(BINDIR)
	install -m 0755 $(C_PROGS) $(BINDIR)
	install -m 0755 $(SH_SCRIPTS) $(BINDIR)

# ---- Uninstall ----
uninstall:
	cd $(BINDIR) && rm -f $(PROGS)

# ---- Cleaning ----
clean:
	rm -f $(C_PROGS) *.o

# ---- Dry run ----
dry-run:
	@echo "C compiler: $(CC)"
	@echo "C flags:    $(CFLAGS)"
	@echo "C programs that would be built:"
	@for p in $(C_PROGS); do \
		echo "  - $$p (from $$p.c)"; \
	done
	@echo
	@echo "Shell scripts included:"
	@for s in $(SH_SCRIPTS); do \
		echo "  - $$s"; \
	done
	@echo
	@echo "No files were built (dry-run only)."

.PHONY: all clean install uninstall dry-run

