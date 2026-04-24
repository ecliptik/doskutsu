# doskutsu — top-level orchestrator for the five-stage DOS cross-build.
#
# Stages (each a CMake invocation against the DJGPP toolchain, each installing
# into build/sysroot/ so the next stage consumes it):
#
#   1. sdl3        — libsdl-org/SDL @ pinned SHA, with DOS backend from PR #15377
#   2. sdl2-compat — libsdl-org/sdl2-compat, forwarding SDL2 API to SDL3
#   3. sdl2-mixer  — SDL_mixer release-2.8.x, built against sdl2-compat
#   4. sdl2-image  — SDL_image release-2.8.x, built against sdl2-compat
#   5. nxengine    — nxengine/nxengine-evo, links everything into build/doskutsu.exe
#
# See PLAN.md for the phased rationale behind each stage; see BUILDING.md
# for prerequisites and troubleshooting.

# --- Toolchain ----------------------------------------------------------------
#
# Two dirs are needed from the DJGPP install:
#   bin/                              cross-gcc, g++, ld, ar
#   i586-pc-msdosdjgpp/bin/           target-side utilities (stubedit, stubify, exe2coff)
#
# tools/djgpp is a symlink to ~/emulators/tools/djgpp, created by
# scripts/setup-symlinks.sh. If that symlink isn't there the djgpp-check
# target fails loud.

REPO_ROOT    := $(abspath .)
DJGPP_ROOT   := $(REPO_ROOT)/tools/djgpp
DJGPP_BIN    := $(DJGPP_ROOT)/bin
DJGPP_TBIN   := $(DJGPP_ROOT)/i586-pc-msdosdjgpp/bin

export PATH := $(DJGPP_BIN):$(DJGPP_TBIN):$(PATH)

CC       := i586-pc-msdosdjgpp-gcc
CXX      := i586-pc-msdosdjgpp-g++
STUBEDIT := stubedit

# CMake toolchain file lives inside the SDL3 tree (PR #15377 ships it).
# It's the canonical DJGPP CMake toolchain; sdl2-compat / mixer / image / nxengine
# all use the same one.
TOOLCHAIN_FILE := $(REPO_ROOT)/vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake

# --- Directories --------------------------------------------------------------

BUILD_DIR    := $(REPO_ROOT)/build
SYSROOT      := $(BUILD_DIR)/sysroot

# Per-stage build directories
SDL3_BUILD      := $(BUILD_DIR)/sdl3
COMPAT_BUILD    := $(BUILD_DIR)/sdl2-compat
MIXER_BUILD     := $(BUILD_DIR)/sdl2-mixer
IMAGE_BUILD     := $(BUILD_DIR)/sdl2-image
NXENGINE_BUILD  := $(BUILD_DIR)/nxengine

# Vendor trees (populated by scripts/fetch-sources.sh)
VENDOR_DIR      := $(REPO_ROOT)/vendor
SDL3_SRC        := $(VENDOR_DIR)/SDL
COMPAT_SRC      := $(VENDOR_DIR)/sdl2-compat
MIXER_SRC       := $(VENDOR_DIR)/SDL_mixer
IMAGE_SRC       := $(VENDOR_DIR)/SDL_image
NXENGINE_SRC    := $(VENDOR_DIR)/nxengine-evo

# Vendored DPMI host (tracked in git, used by dist target)
CWSDPMI_EXE     := $(VENDOR_DIR)/cwsdpmi/cwsdpmi.exe
CWSDPMI_DOC     := $(VENDOR_DIR)/cwsdpmi/cwsdpmi.doc

# --- Common CMake args --------------------------------------------------------
#
# Every stage uses the DJGPP toolchain file and installs into SYSROOT.
# CMAKE_PREFIX_PATH makes each stage's output visible to later stages.

CMAKE_COMMON := \
    -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$(SYSROOT) \
    -DCMAKE_PREFIX_PATH=$(SYSROOT) \
    -DBUILD_SHARED_LIBS=OFF

NPROC := $(shell nproc 2>/dev/null || echo 4)

# --- Top-level targets --------------------------------------------------------

.PHONY: all
all: nxengine

.PHONY: help
help:
	@echo "doskutsu — five-stage DOS cross-build"
	@echo
	@echo "One-time setup:"
	@echo "  ./scripts/setup-symlinks.sh      link tools/djgpp to ~/emulators/tools/djgpp"
	@echo "  ./scripts/fetch-sources.sh       clone vendored upstreams at pinned SHAs"
	@echo "  ./scripts/apply-patches.sh       apply patches/<name>/*.patch"
	@echo
	@echo "Build stages:"
	@echo "  make sdl3                        stage 1: SDL3 (+ DOS backend)"
	@echo "  make sdl2-compat                 stage 2: SDL2 API shim"
	@echo "  make sdl2-mixer                  stage 3: SDL_mixer (WAV + OGG)"
	@echo "  make sdl2-image                  stage 4: SDL_image (PNG)"
	@echo "  make nxengine                    stage 5: NXEngine-evo -> build/doskutsu.exe"
	@echo "  make all                         stages 1-5 end to end (default)"
	@echo
	@echo "Test:"
	@echo "  make hello                       build tests/smoketest/hello.exe"
	@echo "  make smoke-fast                  run hello.exe in DOSBox-X (cycles=max)"
	@echo "  make smoke                       run hello.exe in DOSBox-X (parity cycles)"
	@echo
	@echo "Deploy:"
	@echo "  make dist                        dist/doskutsu-cf.zip (CF-ready bundle)"
	@echo "  make install CF=/mnt/cf          copy payload to mounted CF card"
	@echo
	@echo "Cleanup:"
	@echo "  make clean                       remove build/"
	@echo "  make distclean                   clean + remove cloned vendor/<name>/ trees"
	@echo
	@echo "Diagnostics:"
	@echo "  make djgpp-check                 verify DJGPP is installed + on PATH"
	@echo "  make vendor-check                verify vendored sources are present"

# --- Diagnostics --------------------------------------------------------------

.PHONY: djgpp-check
djgpp-check:
	@if [ ! -L "$(DJGPP_ROOT)" ] && [ ! -d "$(DJGPP_ROOT)" ]; then \
	    echo "error: $(DJGPP_ROOT) does not exist. Run ./scripts/setup-symlinks.sh." >&2; \
	    exit 1; \
	fi
	@if ! command -v $(CC) >/dev/null 2>&1; then \
	    echo "error: $(CC) not found on PATH." >&2; \
	    echo "       Tried $(DJGPP_BIN)" >&2; \
	    echo "       Run ~/emulators/scripts/update-djgpp.sh to install." >&2; \
	    exit 1; \
	fi
	@$(CC) --version | head -n1
	@echo "DJGPP ready."

.PHONY: vendor-check
vendor-check:
	@missing=0; \
	for d in $(SDL3_SRC) $(COMPAT_SRC) $(MIXER_SRC) $(IMAGE_SRC) $(NXENGINE_SRC); do \
	    if [ ! -d "$$d" ]; then \
	        echo "error: $$d not present — run ./scripts/fetch-sources.sh" >&2; \
	        missing=1; \
	    fi; \
	done; \
	if [ ! -f "$(CWSDPMI_EXE)" ]; then \
	    echo "error: $(CWSDPMI_EXE) missing — see vendor/cwsdpmi/README.md" >&2; \
	    missing=1; \
	fi; \
	if [ "$$missing" = "1" ]; then exit 1; fi
	@echo "vendor tree OK."

# --- Stage 1: SDL3 ------------------------------------------------------------

.PHONY: sdl3
sdl3: $(SYSROOT)/lib/libSDL3.a

$(SYSROOT)/lib/libSDL3.a: | djgpp-check
	@test -d "$(SDL3_SRC)" || (echo "error: $(SDL3_SRC) not present — run scripts/fetch-sources.sh" >&2; exit 1)
	@test -f "$(TOOLCHAIN_FILE)" || (echo "error: $(TOOLCHAIN_FILE) not found — PR #15377 not in this SDL checkout?" >&2; exit 1)
	cmake -S $(SDL3_SRC) -B $(SDL3_BUILD) $(CMAKE_COMMON) \
	    -DSDL_SHARED=OFF -DSDL_STATIC=ON
	cmake --build $(SDL3_BUILD) -j$(NPROC)
	cmake --install $(SDL3_BUILD)

# --- Stage 2: sdl2-compat -----------------------------------------------------

.PHONY: sdl2-compat
sdl2-compat: $(SYSROOT)/lib/libSDL2.a

$(SYSROOT)/lib/libSDL2.a: $(SYSROOT)/lib/libSDL3.a
	@test -d "$(COMPAT_SRC)" || (echo "error: $(COMPAT_SRC) not present" >&2; exit 1)
	cmake -S $(COMPAT_SRC) -B $(COMPAT_BUILD) $(CMAKE_COMMON) \
	    -DSDL2COMPAT_STATIC=ON \
	    -DSDL2COMPAT_TESTS=OFF
	cmake --build $(COMPAT_BUILD) -j$(NPROC)
	cmake --install $(COMPAT_BUILD)

# --- Stage 3: SDL2_mixer ------------------------------------------------------

.PHONY: sdl2-mixer
sdl2-mixer: $(SYSROOT)/lib/libSDL2_mixer.a

$(SYSROOT)/lib/libSDL2_mixer.a: $(SYSROOT)/lib/libSDL2.a
	@test -d "$(MIXER_SRC)" || (echo "error: $(MIXER_SRC) not present" >&2; exit 1)
	cmake -S $(MIXER_SRC) -B $(MIXER_BUILD) $(CMAKE_COMMON) \
	    -DSDL2MIXER_VENDORED=ON \
	    -DSDL2MIXER_OPUS=OFF \
	    -DSDL2MIXER_MOD=OFF \
	    -DSDL2MIXER_MP3=OFF \
	    -DSDL2MIXER_FLAC=OFF \
	    -DSDL2MIXER_MIDI=OFF \
	    -DSDL2MIXER_VORBIS=STB \
	    -DSDL2MIXER_WAVE=ON
	cmake --build $(MIXER_BUILD) -j$(NPROC)
	cmake --install $(MIXER_BUILD)

# --- Stage 4: SDL2_image ------------------------------------------------------

.PHONY: sdl2-image
sdl2-image: $(SYSROOT)/lib/libSDL2_image.a

$(SYSROOT)/lib/libSDL2_image.a: $(SYSROOT)/lib/libSDL2.a
	@test -d "$(IMAGE_SRC)" || (echo "error: $(IMAGE_SRC) not present" >&2; exit 1)
	cmake -S $(IMAGE_SRC) -B $(IMAGE_BUILD) $(CMAKE_COMMON) \
	    -DSDL2IMAGE_VENDORED=ON \
	    -DSDL2IMAGE_BACKEND_STB=ON \
	    -DSDL2IMAGE_PNG=ON \
	    -DSDL2IMAGE_JPG=OFF \
	    -DSDL2IMAGE_TIF=OFF \
	    -DSDL2IMAGE_WEBP=OFF \
	    -DSDL2IMAGE_AVIF=OFF
	cmake --build $(IMAGE_BUILD) -j$(NPROC)
	cmake --install $(IMAGE_BUILD)

# --- Stage 5: NXEngine-evo ----------------------------------------------------
#
# This produces $(BUILD_DIR)/doskutsu.exe. The patches/nxengine-evo/ set
# renames the CMake target to 'doskutsu', so the raw output is already named
# correctly. We copy out of $(NXENGINE_BUILD) to $(BUILD_DIR) for a tidy path.
# Post-link stubedit bumps the DPMI min stack from 256K to 2048K.

MINSTACK := 2048k

.PHONY: nxengine
nxengine: $(BUILD_DIR)/doskutsu.exe

$(BUILD_DIR)/doskutsu.exe: $(SYSROOT)/lib/libSDL2_mixer.a $(SYSROOT)/lib/libSDL2_image.a
	@test -d "$(NXENGINE_SRC)" || (echo "error: $(NXENGINE_SRC) not present" >&2; exit 1)
	cmake -S $(NXENGINE_SRC) -B $(NXENGINE_BUILD) $(CMAKE_COMMON)
	cmake --build $(NXENGINE_BUILD) -j$(NPROC)
	@# Find the produced exe — upstream may put it at the build root or under bin/.
	@src_exe=""; \
	for candidate in $(NXENGINE_BUILD)/doskutsu.exe $(NXENGINE_BUILD)/bin/doskutsu.exe; do \
	    if [ -f "$$candidate" ]; then src_exe="$$candidate"; break; fi; \
	done; \
	if [ -z "$$src_exe" ]; then \
	    echo "error: doskutsu.exe not found under $(NXENGINE_BUILD)/" >&2; \
	    echo "       Check patches/nxengine-evo/ renamed the target correctly." >&2; \
	    exit 1; \
	fi; \
	cp "$$src_exe" $@
	$(STUBEDIT) $@ minstack=$(MINSTACK)
	@echo "built $@ ($$(stat -c '%s' $@) bytes)"

# --- Phase 0 smoke: tests/smoketest/hello.exe ---------------------------------

HELLO_EXE := $(BUILD_DIR)/hello.exe
HELLO_SRC := tests/smoketest/hello.c

.PHONY: hello
hello: $(HELLO_EXE)

$(HELLO_EXE): $(HELLO_SRC) | djgpp-check
	@mkdir -p $(BUILD_DIR)
	$(CC) -march=i486 -mtune=pentium -O2 -Wall -o $@ $<
	$(STUBEDIT) $@ minstack=256k

.PHONY: smoke-fast
smoke-fast: $(HELLO_EXE)
	tests/run-smoke.sh --exe $(HELLO_EXE) --fast

.PHONY: smoke
smoke: $(HELLO_EXE)
	tests/run-smoke.sh --exe $(HELLO_EXE)

# --- Distribution -------------------------------------------------------------
#
# make dist      produces dist/doskutsu-cf.zip with the legal-complete payload
# make install   copies the same payload to a mounted CF card ($CF required)
#
# PAYLOAD (matches THIRD-PARTY.md § Verification):
#   DOSKUTSU.EXE       the binary
#   CWSDPMI.EXE        DPMI host
#   CWSDPMI.DOC        CWSDPMI redistribution terms (required by its license)
#   LICENSE.TXT        this repo's MIT license
#   GPLV3.TXT          NXEngine-evo's GPLv3 (dominant license of the binary)
#   THIRD-PARTY.TXT    attribution matrix (CRLF normalized)
#   README.TXT         DOS-readable quick-start + asset-extraction pointer

CF             ?=
DIST_DIR       := $(REPO_ROOT)/dist
CF_STAGE       := $(DIST_DIR)/doskutsu-cf
CF_ZIP         := $(DIST_DIR)/doskutsu-cf.zip

# CRLF filter for DOS-facing text
CRLF := awk 'BEGIN{ORS="\r\n"} {sub(/\r$$/, ""); print}'

# GPL text source: the cloned NXEngine-evo tree ships its LICENSE file at the root.
NX_LICENSE := $(NXENGINE_SRC)/LICENSE

define DIST_README
DOSKUTSU - Cave Story for MS-DOS 6.22
=====================================

DOSKUTSU is a port of Cave Story (Doukutsu Monogatari) via NXEngine-evo
to MS-DOS 6.22, cross-compiled with DJGPP against a DOS-ported SDL3.

HOW TO RUN
----------

 1. Place DOSKUTSU.EXE, CWSDPMI.EXE, and the DATA directory (containing
    extracted Cave Story game assets) in the same folder on your DOS
    machine, e.g. C:\DOSKUTSU\.
 2. Boot DOS with HIMEM.SYS loaded and NO EMS page frame (DJGPP uses DPMI).
 3. Ensure your SB16 BLASTER environment variable is set correctly,
    e.g.  SET BLASTER=A220 I5 D1 H5 T6
 4. Load a VESA 1.2+ BIOS driver if your video card doesn't provide one
    in its firmware (UNIVBE as fallback).
 5. Run:
        C:\>CD \DOSKUTSU
        C:\DOSKUTSU>DOSKUTSU

YOU MUST SUPPLY CAVE STORY DATA
-------------------------------

This bundle does NOT include the Cave Story game data (maps, sprites,
music). You must extract them from the 2004 freeware Doukutsu.exe
yourself. Source: https://www.cavestory.org/

Place the extracted files under:
    C:\DOSKUTSU\DATA\BASE\Stage\
    C:\DOSKUTSU\DATA\BASE\Npc\
    C:\DOSKUTSU\DATA\BASE\org\
    C:\DOSKUTSU\DATA\BASE\wav\

See the project's docs/ASSETS.md for extraction details.

CWSDPMI
-------

CWSDPMI.EXE is the DPMI host required by DJGPP-compiled programs on DOS.
It must be in the current directory or on PATH when DOSKUTSU.EXE runs.
License terms: CWSDPMI.DOC.

LICENSES
--------

This binary is licensed under the GNU General Public License v3 because
it statically links NXEngine-evo (GPLv3). The port source code in this
repository is MIT licensed. See LICENSE.TXT (MIT, for the repo source)
and GPLV3.TXT (GPLv3, for the binary as a whole). THIRD-PARTY.TXT has
the complete attribution matrix.

SOURCE
------

Full source, including build scripts and DOS-port patches:
    @REPO_URL@
endef
export DIST_README

.PHONY: dist
dist: $(BUILD_DIR)/doskutsu.exe
	@test -f "$(CWSDPMI_EXE)"   || (echo "error: $(CWSDPMI_EXE) missing — see vendor/cwsdpmi/README.md" >&2; exit 1)
	@test -f "$(CWSDPMI_DOC)"   || (echo "error: $(CWSDPMI_DOC) missing" >&2; exit 1)
	@test -f "$(NX_LICENSE)"    || (echo "error: $(NX_LICENSE) missing — run scripts/fetch-sources.sh" >&2; exit 1)
	@test -f LICENSE            || (echo "error: LICENSE missing in repo root" >&2; exit 1)
	@test -f THIRD-PARTY.md     || (echo "error: THIRD-PARTY.md missing" >&2; exit 1)
	@rm -rf "$(CF_STAGE)" "$(CF_ZIP)"
	@mkdir -p "$(CF_STAGE)"
	@install -m 0644 $(BUILD_DIR)/doskutsu.exe "$(CF_STAGE)/DOSKUTSU.EXE"
	@install -m 0644 $(CWSDPMI_EXE)            "$(CF_STAGE)/CWSDPMI.EXE"
	@install -m 0644 $(CWSDPMI_DOC)            "$(CF_STAGE)/CWSDPMI.DOC"
	@$(CRLF) < LICENSE           > "$(CF_STAGE)/LICENSE.TXT"
	@$(CRLF) < $(NX_LICENSE)     > "$(CF_STAGE)/GPLV3.TXT"
	@$(CRLF) < THIRD-PARTY.md    > "$(CF_STAGE)/THIRD-PARTY.TXT"
	@url='$(shell git remote get-url origin 2>/dev/null || echo "https://forgejo.ecliptik.com/ecliptik/doskutsu")'; \
	    printf '%s\n' "$$DIST_README" | \
	    awk -v url="$$url" '{gsub(/@REPO_URL@/, url); print}' | \
	    $(CRLF) > "$(CF_STAGE)/README.TXT"
	@(cd "$(CF_STAGE)" && zip -q -r "$(CF_ZIP)" .)
	@echo "built $(CF_ZIP) ($$(stat -c '%s' $(CF_ZIP)) bytes)"

.PHONY: install
install: $(BUILD_DIR)/doskutsu.exe
ifeq ($(strip $(CF)),)
	@echo "error: set CF=/path/to/cf/mount (e.g. make install CF=/mnt/cf)" >&2; exit 1
else
	@test -d "$(CF)" || (echo "error: CF=$(CF) is not a directory" >&2; exit 1)
	@test -f "$(CWSDPMI_EXE)" || (echo "error: $(CWSDPMI_EXE) missing" >&2; exit 1)
	@test -f "$(CWSDPMI_DOC)" || (echo "error: $(CWSDPMI_DOC) missing" >&2; exit 1)
	@mkdir -p "$(CF)/DOSKUTSU"
	@install -m 0644 $(BUILD_DIR)/doskutsu.exe "$(CF)/DOSKUTSU/DOSKUTSU.EXE"
	@install -m 0644 $(CWSDPMI_EXE)            "$(CF)/DOSKUTSU/CWSDPMI.EXE"
	@install -m 0644 $(CWSDPMI_DOC)            "$(CF)/DOSKUTSU/CWSDPMI.DOC"
	@if [ -d "$(REPO_ROOT)/data/base" ]; then \
	    echo "copying extracted Cave Story data to $(CF)/DOSKUTSU/DATA/BASE/"; \
	    mkdir -p "$(CF)/DOSKUTSU/DATA/BASE"; \
	    cp -r "$(REPO_ROOT)/data/base/"* "$(CF)/DOSKUTSU/DATA/BASE/"; \
	else \
	    echo "note: data/base/ not present — Cave Story data must be copied separately"; \
	fi
	@echo "installed doskutsu payload to $(CF)/DOSKUTSU/"
endif

# --- Housekeeping -------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	rm -rf $(DIST_DIR)
	rm -rf $(SDL3_SRC) $(COMPAT_SRC) $(MIXER_SRC) $(IMAGE_SRC) $(NXENGINE_SRC)
	@echo "distclean: vendor/cwsdpmi/ retained; vendor/sources.manifest retained"
