# setup/Makefile -- standalone build for DOSKUTSU SETUP.EXE.
#
# Kept separate from the top-level Makefile for now so SETUP can be built
# without touching the (in-flight) production build graph. It will be folded
# into the top-level Makefile + `dist` target as a follow-up.
#
# Usage (from repo root or this dir):
#   make -C setup           # build build/setup/setup.exe
#   make -C setup clean
#   make -C setup test      # host-side unit tests (loader + recommend)
#
# Toolchain resolution mirrors the top-level Makefile.

REPO_ROOT   := $(abspath ..)
ORIG_PATH   := $(PATH)
DJGPP_ROOT  := $(if $(DJGPP_PREFIX),$(DJGPP_PREFIX),$(REPO_ROOT)/tools/djgpp)
DJGPP_BIN   := $(DJGPP_ROOT)/bin
DJGPP_TBIN  := $(DJGPP_ROOT)/i586-pc-msdosdjgpp/bin
export PATH := $(DJGPP_BIN):$(DJGPP_TBIN):$(PATH)

CC       := i586-pc-msdosdjgpp-gcc
STUBEDIT := stubedit

OUT_DIR := $(REPO_ROOT)/build/setup
# EXE path is AUDIOTEST-dependent (set in the ifeq block below). The dev
# scaffold stub (AUDIOTEST=0) and the release audio-backend build (AUDIOTEST=1)
# MUST NOT share an output path -- v1.6.2 shipped the AUDIOTEST=0 stub because
# both wrote build/setup/setup.exe and a `make stage` (dev `setup`, AUDIOTEST=0)
# re-clobbered the AUDIOTEST=1 release binary. Separate filenames make that
# clobber impossible. Default here is the stub name; the ifeq overrides for =1.
EXE     := $(OUT_DIR)/setup.exe

# DPMI min stack. The scaffold (AUDIOTEST_LINKED=0) build does not strictly
# need more than the 256K DJGPP default, but SETUP.EXE links the SDL3 + mixer
# + synth audio stack once audiotest_sdl.c lands (T3) and then needs the same
# 2048K stack DOSKUTSU.EXE uses. Apply it unconditionally now so the staged /
# dist binary is correct the moment the audio backend flips on -- stubedit is
# idempotent and harmless on the no-SDL build.
MINSTACK := 2048k

# Already-built static libs from the four-stage cross-build (build/sysroot).
SYSROOT  := $(REPO_ROOT)/build/sysroot

# Same arch/tune flags as the engine (no MMX/SSE; P54C target).
CFLAGS  := -std=gnu99 -Wall -Wextra -Werror -march=i486 -mtune=pentium -O2 \
           -I$(REPO_ROOT)/include -I.

# ---- Audio-test backend select (AUDIOTEST) ---------------------------------
# DEFAULT (AUDIOTEST=0): the no-SDL scaffold (audiotest_stub.c). This is the
# build the shared `make setup` / `make stage` use, so they stay GREEN with NO
# dependency on build/sysroot/ being populated -- the scaffold SETUP.EXE is all
# the E2E (T4) needs to drive the TUI + verify DOSKUTSU.CFG round-trip.
#
# OPT-IN (`make setup AUDIOTEST=1`): the live audio-test backend
# (audiotest_sdl.c) statically linked against the SDL3 + SDL3_mixer stack from
# build/sysroot/. Brings the SB16 device up through the same SDL3-DOS audio path
# the game uses, so SETUP can play test SFX/music itself (the T5 WAV-capture
# build). Requires the four-stage cross-build to have run (libSDL3*.a present).
AUDIOTEST ?= 0

ifeq ($(AUDIOTEST),1)
  # Distinct output name so a dev `make stage`/`make setup` (AUDIOTEST=0, writes
  # setup.exe) can NEVER clobber the shipped release binary. dist / stage-release
  # in the top Makefile consume THIS file.
  EXE         := $(OUT_DIR)/setup-release.exe
  CPPFLAGS    := -DAUDIOTEST_LINKED=1
  # SDL3 / SDL3_mixer public headers.
  CFLAGS      += -I$(SYSROOT)/include
  # T28/T36: audiotest_sdl.c + the standalone Cave Story audio cores (all pure
  # C, no engine link): cs_pixtone.c (Pixtone SFX synth, +libm) for the real
  # Polar Star; cs_smf.c (SMF parser/scheduler) + cs_opl3midi.c (OPL3 MIDI
  # voice backend) + cs_opl2midi.c (T80: OPL2/AdLib MIDI voice backend, the
  # music-only AdLib test path) for the real title theme; cs_orgcache.c
  # (pre-rendered Organya PCM cache reader) for the Organya-mode title snippet.
  AUDIOTEST_SRC := audiotest_sdl.c cs_pixtone.c cs_smf.c cs_opl3midi.c cs_opl2midi.c cs_orgcache.c
  # Static-link the audio stack. Order matters for static archives: dependents
  # before dependencies (mixer -> image -> SDL3 -> libm). SDL3_mixer (stb_vorbis
  # + WAV) and SDL3_image (stb_image PNG) carry no external codec deps in this
  # pipeline, so the SDL3 core + libm close the link.
  LIBS        := -L$(SYSROOT)/lib -lSDL3_mixer -lSDL3_image -lSDL3 -lm
else
  CPPFLAGS    := -DAUDIOTEST_LINKED=0
  AUDIOTEST_SRC := audiotest_stub.c
  LIBS        :=
endif

SRCS := main.c tui.c setupcfg.c recommend.c profile.c midiset.c \
        scancode.c bindings.c joyport.c \
        audiotest_progress.c $(AUDIOTEST_SRC)

# Flag-stamp: make tracks timestamps, not flag values, so flipping AUDIOTEST
# (e.g. dev scaffold <-> `dist` release) would otherwise leave a stale setup.exe
# if it is already newer than the sources. The stamp path encodes the current
# AUDIOTEST value; switching values makes the old stamp vanish, so the rule
# re-runs (clearing any prior stamp + re-touching), the stamp becomes newer
# than $(EXE), and the binary is rebuilt with the right backend. Same value =
# stamp already present = no spurious rebuild (incremental dev stays fast).
FLAGSTAMP := $(OUT_DIR)/.audiotest-$(AUDIOTEST)

.PHONY: all clean test
all: $(EXE)

$(FLAGSTAMP): | $(OUT_DIR)
	@rm -f $(OUT_DIR)/.audiotest-*
	@touch $@

$(EXE): $(SRCS) $(REPO_ROOT)/include/doskutsu_config_keys.h \
        $(REPO_ROOT)/include/doskutsu_config.h $(FLAGSTAMP) | $(OUT_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) $(LIBS) -o $@
	$(STUBEDIT) $@ minstack=$(MINSTACK)
	@echo "Built $@ (AUDIOTEST=$(AUDIOTEST)) -- ship via real-HW iter alongside DOSKUTSU.EXE + CWSDPMI.EXE."

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

clean:
	rm -f $(OUT_DIR)/setup.exe $(OUT_DIR)/setup-release.exe $(OUT_DIR)/.audiotest-*

# Host-side logic tests (no DOS toolchain): config loader + recommend matrix.
# Runs with the ORIGINAL PATH so the host cc does not pick up DJGPP's `as`.
test: | $(OUT_DIR)
	PATH="$(ORIG_PATH)" cc -std=gnu11 -Wall -Wextra -Werror \
	   -I$(REPO_ROOT)/include -I. \
	   tests/test_config.c setupcfg.c recommend.c profile.c midiset.c \
	   scancode.c bindings.c \
	   -o $(OUT_DIR)/test_config_host && $(OUT_DIR)/test_config_host
