# Makefile for org2mid -- Cave Story Organya -> Standard MIDI converter.
# Linux host tool. C99, no dependencies beyond libc.
#
# Build:    make
# Test:     make selftest         (converts data/org/quiet.org to /tmp/quiet.mid)
# Clean:    make clean
#
# Design: docs/internal/WAVE-41-CONVERTER-DESIGN.md
# License: GPL-3.0-only

# Host-only build. We FORCE CC to a host gcc/cc rather than `?= gcc` because
# the parent doskutsu Makefile sets CC := i586-pc-msdosdjgpp-gcc (DJGPP cross-
# compiler) and a recursive sub-make would inherit that. The DJGPP `as` then
# rejects host-x86_64 instruction flags ("--64") -- builds fail. Hard override
# here. Operator can still set HOST_CC=/path/to/special-gcc to point at a
# specific host compiler if needed.
HOST_CC ?= cc
CC      := $(HOST_CC)
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wpedantic
LDFLAGS ?=

BIN     := org2mid
SRC     := org2mid.c

# Self-test path relative to this Makefile (assumes invocation from
# tools/org2mid/). data/org/ lives at the repo root.
REPO_ROOT := ../..
SAMPLE_IN := $(REPO_ROOT)/data/org/quiet.org
SAMPLE_OUT := /tmp/quiet.mid

.PHONY: all selftest clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# Self-test: round-trips a known .org file. Validates that the binary
# builds + reads a real corpus file + writes a valid SMF. Does NOT
# validate the SMF is playable -- that's task #4's listening session.
selftest: $(BIN)
	@if [ ! -f $(SAMPLE_IN) ]; then \
		echo "selftest: missing $(SAMPLE_IN); skipping" >&2; \
		exit 0; \
	fi
	./$(BIN) --force $(SAMPLE_IN) $(SAMPLE_OUT)
	@if [ ! -f $(SAMPLE_OUT) ]; then \
		echo "selftest: FAIL -- $(SAMPLE_OUT) not produced" >&2; \
		exit 1; \
	fi
	@if [ ! -s $(SAMPLE_OUT) ]; then \
		echo "selftest: FAIL -- $(SAMPLE_OUT) is empty" >&2; \
		exit 1; \
	fi
	@HDR=`head -c 14 $(SAMPLE_OUT) | od -An -tx1 | tr -d ' \n'`; \
	EXP="4d54686400000006000100"; \
	if ! echo "$$HDR" | grep -q "^$$EXP"; then \
		echo "selftest: FAIL -- bad MThd header: $$HDR" >&2; \
		exit 1; \
	fi
	@echo "selftest: PASS ($(SAMPLE_OUT))"

clean:
	rm -f $(BIN)
