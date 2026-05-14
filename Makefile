# doskutsu -- top-level orchestrator for the five-stage DOS cross-build.
#
# Stages (each a CMake invocation against the DJGPP toolchain, each installing
# into build/sysroot/ so the next stage consumes it):
#
#   1. sdl3        -- libsdl-org/SDL @ pinned SHA, with DOS backend from PR #15377
#   2. sdl2-compat -- libsdl-org/sdl2-compat, forwarding SDL2 API to SDL3
#   3. sdl2-mixer  -- SDL_mixer release-2.8.x, built against sdl2-compat
#   4. sdl2-image  -- SDL_image release-2.8.x, built against sdl2-compat
#   5. nxengine    -- nxengine/nxengine-evo, links everything into build/doskutsu.exe
#
# See PLAN.md for the phased rationale behind each stage; see docs/BUILDING.md
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

# SDL3-NOSIMD compile defines for any SDL3 consumer on DJGPP. SDL3's PUBLIC
# `SDL_intrin.h` (vendor/SDL/include/SDL3/SDL_intrin.h:291-292, 367) enables
# `SDL_SSE_INTRINSICS=1` for any gcc>=4.9 because the compiler *supports*
# `__attribute__((target("sse")))` -- even though our P54C / 486 target has
# no SSE. SDL3 itself sets `SDL_DISABLE_SSE=1` in its INTERNAL build_config.h
# so its own code is fine, but downstream consumers (SDL3_mixer, SDL3_image,
# NXEngine) compile without that internal config and pick up the SSE intrinsic
# paths -- which then emit a runtime check that fails on Pentium-class hardware
# (e.g. SDL_mixer.c:685 `MIX_Init: Need SSE instructions but this CPU doesn't
# offer it`). Forwarding these defines through CMAKE_C_FLAGS suppresses the
# intrinsic gate at every consumer's preprocessor level. Includes the AVX
# family for completeness -- same upstream issue applies. Found via #26 spike;
# upstream issue draft at .tmp/upstream-sdl-issue-sdl-intrin-propagation.md.
NOSIMD_FLAGS := -DSDL_DISABLE_MMX=1 -DSDL_DISABLE_SSE=1 -DSDL_DISABLE_SSE2=1 \
                -DSDL_DISABLE_SSE3=1 -DSDL_DISABLE_SSE4_1=1 -DSDL_DISABLE_SSE4_2=1 \
                -DSDL_DISABLE_AVX=1 -DSDL_DISABLE_AVX2=1 -DSDL_DISABLE_AVX512F=1

# --- Wave-43 RUNMANIFEST flags ------------------------------------------------
#
# Patch 0138 (wave 43): shared schema-v1 emit helper at
# `include/runmanifest.h` consumed by both DOSKUTSU.EXE (engine-side) and
# HWINV.EXE (probe-engineer's task #10/16). All SDL3 + NXEngine compile
# stages get the `-I include/` flag so the engine TU finds runmanifest.h
# without modifying the vendored CMakeLists.txt; harmless to other
# stages since they don't `#include` it.
#
# DOSKUTSU_BUILD_SHA12: source-tree git short sha at make-config time,
# embedded into the binary's RUNMANIFEST `binary_sha12` field. Per
# probe-engineer 2026-05-14 recommendation: git short sha as the
# strongest "this commit produced this binary" anchor. Fallback
# "UNKNOWN_____" (12 chars; pads the sha12 field) if git unavailable
# or repo not yet cloned. Evaluated immediately (`:=`) so we capture
# the sha at the top of the build, not at recipe-execution time.
RUNMANIFEST_INC := -I$(REPO_ROOT)/include
DOSKUTSU_BUILD_SHA12 := $(shell git -C $(REPO_ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo UNKNOWN_____)
# Pass SHA as BARE TOKEN (no quotes); main.cpp's _DOSKUTSU_STR macro
# stringifies via C preprocessor. Avoids multi-layer quote escaping
# through bash + make + cmake -> compiler. SHA is always 12 hex chars
# (a..f, 0..9), valid as a C identifier; fallback "UNKNOWN_____" also
# valid identifier characters.
RUNMANIFEST_FLAGS := $(RUNMANIFEST_INC) -DDOSKUTSU_BUILD_SHA12=$(DOSKUTSU_BUILD_SHA12)

CMAKE_COMMON := \
    -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$(SYSROOT) \
    -DCMAKE_PREFIX_PATH=$(SYSROOT) \
    -DCMAKE_FIND_ROOT_PATH=$(SYSROOT) \
    -DCMAKE_C_FLAGS="$(NOSIMD_FLAGS) $(RUNMANIFEST_FLAGS)" \
    -DCMAKE_CXX_FLAGS="$(NOSIMD_FLAGS) $(RUNMANIFEST_FLAGS)" \
    -DBUILD_SHARED_LIBS=OFF
# CMAKE_FIND_ROOT_PATH=$(SYSROOT) is pre-populated so the DJGPP toolchain file's
# `list(APPEND CMAKE_FIND_ROOT_PATH ${CC_ROOTS})` keeps both -- needed because
# the toolchain sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY, which restricts
# find_package() to those paths. Without our sysroot prepended, downstream
# stages (sdl3-mixer, sdl3-image, nxengine) couldn't find_package(SDL3).
#
# CMAKE_C_FLAGS / CMAKE_CXX_FLAGS carry the NOSIMD train (see above). SDL3's
# own build is unaffected (it sets these internally already); SDL3_mixer,
# SDL3_image, and NXEngine itself need them for the public-header gate.

NPROC := $(shell nproc 2>/dev/null || echo 4)

# --- Top-level targets --------------------------------------------------------

.PHONY: all
all: verify-patches-applied nxengine

.PHONY: help
help:
	@echo "doskutsu -- five-stage DOS cross-build"
	@echo
	@echo "One-time setup:"
	@echo "  ./scripts/setup-symlinks.sh      link tools/djgpp to ~/emulators/tools/djgpp"
	@echo "  ./scripts/fetch-sources.sh       clone vendored upstreams at pinned SHAs"
	@echo "  make patches                     apply patches/<name>/*.patch via scripts/apply-patches.sh"
	@echo "                                   (or equivalently: ./scripts/apply-patches.sh)"
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
	@echo "  make dpmi-lfn-smoke              Phase 8 prereq: DPMI LFN propagation probe"
	@echo "  make tas-smoke                   wave 41 patch 0135 TAS record/replay round-trip"
	@echo "  make probes                      Phase 9 wave 20 P0 probes (dacprog + hwlog)"
	@echo "  make hwinv-86box-smoke           wave-41 hwinv parity smoke under 86Box (closer to real HW)"
	@echo
	@echo "Deploy:"
	@echo "  make dist                        dist/doskutsu-cf.zip (CF-ready bundle)"
	@echo "  make dist-list                   dry-run: print dist manifest, no staging"
	@echo "  make install CF=/mnt/cf          copy payload to mounted CF card"
	@echo
	@echo "Host tooling (Linux-only):"
	@echo "  make org2mid                     build tools/org2mid/org2mid (Organya -> SMF)"
	@echo "  make convert-music               run org2mid across data/org/*.org -> data/mid/"
	@echo "                                   (offline only; not auto-wired into engine path yet)"
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
	        echo "error: $$d not present -- run ./scripts/fetch-sources.sh" >&2; \
	        missing=1; \
	    fi; \
	done; \
	if [ ! -f "$(CWSDPMI_EXE)" ]; then \
	    echo "error: $(CWSDPMI_EXE) missing -- run ./scripts/fetch-vendor-binaries.sh" >&2; \
	    missing=1; \
	fi; \
	if [ "$$missing" = "1" ]; then exit 1; fi
	@echo "vendor tree OK."

# --- Vendored DOS binaries (CWSDPMI + LFNDOS + DOSLFN) ------------------------
#
# As of 2026-04-30 these binaries are no longer tracked in git. The
# fetch-vendor-binaries.sh script populates them on demand from the URLs +
# sha256 pins in vendor/binaries.manifest. Idempotent: re-running is a no-op
# when files already exist with the manifest-pinned hash.
#
# Targets that need the binaries (stage, dist, install, dpmi-lfn-smoke) take
# fetch-binaries as an order-only prerequisite -- runs once on first build,
# subsequent rebuilds short-circuit on the sha check.

.PHONY: fetch-binaries
fetch-binaries:
	@scripts/fetch-vendor-binaries.sh

# --- Patch orchestration ------------------------------------------------------
#
# `make patches` dispatches to scripts/apply-patches.sh -- `git reset --hard`
# each vendor to its manifest-pinned SHA and re-applies the full
# patches/<name>/*.patch series via `git am`. Idempotent; safe to re-run.
#
# `make verify-patches-applied` is a non-destructive pre-flight: for each
# vendor with a patches/<name>/ dir, compares count of *.patch files to count
# of commits since the pinned SHA. Mismatch = a patch landed in patches/ but
# wasn't `git am`'d to vendor/, OR vendor has commits the patch series
# doesn't represent. Wired as a regular prereq on every build-stage
# convenience target below so the build fails before producing a binary
# that silently lacks a patch's effects.

.PHONY: patches verify-patches-applied
patches:
	@$(REPO_ROOT)/scripts/apply-patches.sh

verify-patches-applied:
	@$(REPO_ROOT)/scripts/verify-patches-applied.sh

# --- Stage 1: SDL3 ------------------------------------------------------------

.PHONY: sdl3
sdl3: verify-patches-applied $(SYSROOT)/lib/libSDL3.a

# DOSKUTSU: pin SDL_REVISION to a deterministic string to keep build sha
# reproducible across agent rebuilds. By default SDL3's CMakeLists.txt does
# `git describe` on vendor/SDL/, which embeds the current HEAD commit hash
# into the binary as `SDL-<version>-<sha>`. apply-patches.sh re-applies our
# patch series via `git am`, and `git am` uses wall-clock time for the
# COMMITTER timestamp -- so HEAD commit hashes change every time apply runs,
# making the embedded revision string non-deterministic. Pin it explicitly
# to the base manifest SHA + a doskutsu marker so the binary's embedded
# revision is determined purely by the source content, not by when
# apply-patches happened to run.
SDL_REVISION_PIN := SDL-3.5.0-74a746281+doskutsu

$(SYSROOT)/lib/libSDL3.a: | djgpp-check
	@test -d "$(SDL3_SRC)" || (echo "error: $(SDL3_SRC) not present -- run scripts/fetch-sources.sh" >&2; exit 1)
	@test -f "$(TOOLCHAIN_FILE)" || (echo "error: $(TOOLCHAIN_FILE) not found -- PR #15377 not in this SDL checkout?" >&2; exit 1)
	cmake -S $(SDL3_SRC) -B $(SDL3_BUILD) $(CMAKE_COMMON) \
	    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
	    -DSDL_REVISION="$(SDL_REVISION_PIN)"
	cmake --build $(SDL3_BUILD) -j$(NPROC)
	cmake --install $(SDL3_BUILD)

# --- Path B spike: SDL3_mixer for DOS ----------------------------------------
#
# task #26 spike. Builds SDL_mixer (release-3.2.x) against libSDL3.a with
# WAV (native) + OGG-via-stb_vorbis only. All other codecs OFF; SDLMIXER_DEPS_SHARED=OFF
# disables dynamic codec loading (DJGPP has no real dlopen). PLATFORM_SUPPORTS_SHARED
# is forced OFF via BUILD_SHARED_LIBS=OFF override.
#
# This is the path-B-go/no-go preflight per software-architect's condition 2.

SDL3_MIXER_BUILD := $(BUILD_DIR)/sdl3-mixer

.PHONY: sdl3-mixer
sdl3-mixer: verify-patches-applied $(SYSROOT)/lib/libSDL3_mixer.a

# NOSIMD flag train moved to CMAKE_COMMON (project-wide) per team-lead -- every
# SDL3 consumer on DJGPP needs the same defines. See the NOSIMD_FLAGS block
# at the top of this file for the full rationale. Per-stage CMAKE_C_FLAGS
# overrides removed; they'd shadow the CMAKE_COMMON value.

$(SYSROOT)/lib/libSDL3_mixer.a: $(SYSROOT)/lib/libSDL3.a
	@test -d "$(MIXER_SRC)" || (echo "error: $(MIXER_SRC) not present -- run scripts/fetch-sources.sh" >&2; exit 1)
	cmake -S $(MIXER_SRC) -B $(SDL3_MIXER_BUILD) $(CMAKE_COMMON) \
	    -DSDLMIXER_VENDORED=ON \
	    -DSDLMIXER_DEPS_SHARED=OFF \
	    -DSDLMIXER_TESTS=OFF \
	    -DSDLMIXER_EXAMPLES=OFF \
	    -DSDLMIXER_AIFF=OFF \
	    -DSDLMIXER_VOC=OFF \
	    -DSDLMIXER_AU=OFF \
	    -DSDLMIXER_FLAC=OFF \
	    -DSDLMIXER_GME=OFF \
	    -DSDLMIXER_MOD=OFF \
	    -DSDLMIXER_MP3=OFF \
	    -DSDLMIXER_MIDI=OFF \
	    -DSDLMIXER_OPUS=OFF \
	    -DSDLMIXER_WAVE=ON \
	    -DSDLMIXER_VORBIS_STB=ON \
	    -DSDLMIXER_VORBIS_VORBISFILE=OFF \
	    -DSDLMIXER_WAVPACK=OFF
	cmake --build $(SDL3_MIXER_BUILD) -j$(NPROC)
	cmake --install $(SDL3_MIXER_BUILD)

# --- Path B: SDL3_image for DOS (#28) ----------------------------------------
#
# Builds SDL_image release-3.2.x against libSDL3.a with PNG-via-stb_image
# only. All other codecs OFF; SDLIMAGE_DEPS_SHARED=OFF disables the
# SDL_LoadObject codec loader path. Same SDL_DISABLE_SSE/MMX flag train as
# sdl3-mixer -- the SDL3 PUBLIC SDL_intrin.h enables SDL_SSE_INTRINSICS for
# any gcc>=4.9 regardless of target CPU, which would otherwise enable code
# paths that fail on P54C-class hardware. SDL3_image kept the IMG_* prefix
# from SDL2_image (signature drift, not the architectural redesign that
# SDL3_mixer underwent) -- see software-architect's note on #28.

SDL3_IMAGE_BUILD := $(BUILD_DIR)/sdl3-image
# NOSIMD flag train inherited from CMAKE_COMMON. See top-of-file NOSIMD_FLAGS.

.PHONY: sdl3-image
sdl3-image: verify-patches-applied $(SYSROOT)/lib/libSDL3_image.a

$(SYSROOT)/lib/libSDL3_image.a: $(SYSROOT)/lib/libSDL3.a
	@test -d "$(IMAGE_SRC)" || (echo "error: $(IMAGE_SRC) not present -- run scripts/fetch-sources.sh" >&2; exit 1)
	cmake -S $(IMAGE_SRC) -B $(SDL3_IMAGE_BUILD) $(CMAKE_COMMON) \
	    -DSDLIMAGE_VENDORED=ON \
	    -DSDLIMAGE_DEPS_SHARED=OFF \
	    -DSDLIMAGE_TESTS=OFF \
	    -DSDLIMAGE_SAMPLES=OFF \
	    -DSDLIMAGE_BACKEND_STB=ON \
	    -DSDLIMAGE_PNG=ON \
	    -DSDLIMAGE_AVIF=OFF \
	    -DSDLIMAGE_BMP=OFF \
	    -DSDLIMAGE_GIF=OFF \
	    -DSDLIMAGE_JPG=OFF \
	    -DSDLIMAGE_JXL=OFF \
	    -DSDLIMAGE_LBM=OFF \
	    -DSDLIMAGE_PCX=OFF \
	    -DSDLIMAGE_PNM=OFF \
	    -DSDLIMAGE_QOI=OFF \
	    -DSDLIMAGE_SVG=OFF \
	    -DSDLIMAGE_TGA=OFF \
	    -DSDLIMAGE_TIF=OFF \
	    -DSDLIMAGE_WEBP=OFF \
	    -DSDLIMAGE_XCF=OFF \
	    -DSDLIMAGE_XPM=OFF \
	    -DSDLIMAGE_XV=OFF
	cmake --build $(SDL3_IMAGE_BUILD) -j$(NPROC)
	cmake --install $(SDL3_IMAGE_BUILD)

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
nxengine: verify-patches-applied $(BUILD_DIR)/doskutsu.exe

$(BUILD_DIR)/doskutsu.exe: $(SYSROOT)/lib/libSDL3_mixer.a $(SYSROOT)/lib/libSDL3_image.a
	@test -d "$(NXENGINE_SRC)" || (echo "error: $(NXENGINE_SRC) not present" >&2; exit 1)
	cmake -S $(NXENGINE_SRC) -B $(NXENGINE_BUILD) $(CMAKE_COMMON)
	cmake --build $(NXENGINE_BUILD) -j$(NPROC)
	@# Find the produced exe -- upstream may put it at the build root or under bin/.
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

# --- Phase 8 prerequisite: DPMI LFN propagation probe (task #20) ------------
#
# Builds tests/dpmi-lfn-smoke/probe.c -- tiny DJGPP DOS exe that issues
# INT 21h function 716Ch (LFN Extended Open/Create) for a long-named test
# fixture (wavetable.dat, 9-char base, breaks 8.3) via three different paths:
#   1. open() with 8.3 name           -- control, always passes
#   2. open() after _use_lfn(1)       -- DJGPP libc LFN path
#   3. __dpmi_int(0x21, AX=716Ch)     -- raw DPMI, isolates the libc question
#
# Answers the gating Phase 8 question (docs/PHASE8-LFN-DECISION.md): does
# CWSDPMI's INT 21h reflector pass LFN-family calls (function codes
# 7140h-71A8h) to a real-mode TSR loaded under MS-DOS 6.22? Run on dev host
# under DOSBox-X (lfn=true baseline); the actual answer comes from running
# probe.exe on g2k with LFNDOS.COM (or DOSLFN.COM) loaded. See the runner
# script header + tests/dpmi-lfn-smoke/README.md for the decision tree.
#
# 8.3 DOS filename: basename "probe" + ".exe" -- fits.
# minstack=256k: probe is tiny, default DPMI stack is plenty.

DPMI_LFN_SMOKE_DIR := $(BUILD_DIR)/dpmi-lfn-smoke
DPMI_LFN_SMOKE_SRC := tests/dpmi-lfn-smoke/probe.c
DPMI_LFN_SMOKE_EXE := $(DPMI_LFN_SMOKE_DIR)/probe.exe

$(DPMI_LFN_SMOKE_EXE): $(DPMI_LFN_SMOKE_SRC) | djgpp-check
	@mkdir -p $(DPMI_LFN_SMOKE_DIR)
	$(CC) -march=i486 -mtune=pentium -O2 -Wall -o $@ $<
	$(STUBEDIT) $@ minstack=256k

.PHONY: dpmi-lfn-smoke
dpmi-lfn-smoke: $(DPMI_LFN_SMOKE_EXE) | fetch-binaries
	tests/run-dpmi-lfn-smoke.sh

# --- Phase 2d smoke: SDL3 DOS-backend probe -----------------------------------
#
# Builds tests/sdl3-smoke/sdltest.c -- our own minimal probe authored against
# public SDL3 APIs -- into build/sdl3-smoke/sdltest.exe. Same coverage as
# upstream's testaudioinfo + testdisplayinfo combined (audio driver/device
# enumeration + video driver/display/mode enumeration), but writes via
# printf() to stdout instead of SDL_Log() to stderr.
#
# Why our own probe instead of upstream's tests: SDL_Log goes to stderr
# unconditionally (vendor/SDL/src/SDL_log.c), and neither MS-DOS COMMAND.COM
# nor DOSBox-X's built-in shell support `2>&1` redirection -- the headless
# capture would always be empty. Patching SDL test source to redirect log
# output would conflict with vendor/SDL/CLAUDE.md (no AI-generated code into
# SDL upstream). See tests/sdl3-smoke/README.md for the full rationale.
#
# minstack=512k: DPMI default 256K is tight for SDL3 init paths; doskutsu.exe
# itself gets 2048K but a probe doesn't need that much.

SDL3_SMOKE_DIR     := $(BUILD_DIR)/sdl3-smoke
SDL3_SMOKE_SRC     := tests/sdl3-smoke/sdltest.c
SDL3_SMOKE_EXE     := $(SDL3_SMOKE_DIR)/sdltest.exe
SDL3_TEST_CFLAGS   := -march=i486 -mtune=pentium -O2 -Wall \
                      -I$(SYSROOT)/include
SDL3_TEST_LDLIBS   := -L$(SYSROOT)/lib -lSDL3 -lm
SDL3_TEST_MINSTACK := 512k

$(SDL3_SMOKE_EXE): $(SDL3_SMOKE_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(SDL3_SMOKE_DIR)
	$(CC) $(SDL3_TEST_CFLAGS) -o $@ $< $(SDL3_TEST_LDLIBS)
	$(STUBEDIT) $@ minstack=$(SDL3_TEST_MINSTACK)

.PHONY: sdl3-smoke
sdl3-smoke: $(SDL3_SMOKE_EXE)
	tests/run-sdl3-smoke.sh

# --- Path B spike: SDL3_mixer functional smoke (#26 sharpening) --------------
#
# Software-architect added a functional gate on top of "libSDL3_mixer.a links":
# three NXEngine audio code paths must execute end-to-end. This probe maps:
#   Mix_QuickLoad_RAW (Organya) -> MIX_LoadRawAudio
#   Mix_LoadWAV (SFX)           -> MIX_LoadAudio_IO from in-memory WAV
#   OGG via stb_vorbis (Remix)  -> VORBIS in MIX_GetAudioDecoder list
# See tests/sdl3-mixer-smoke/mixertest.c file header for full rationale.

SDL3_MIXER_SMOKE_DIR := $(BUILD_DIR)/sdl3-mixer-smoke
SDL3_MIXER_SMOKE_SRC := tests/sdl3-mixer-smoke/mixertest.c
# 8.3 DOS filename: basename "mixsmk" (6) + ".exe" (4) -- RUN.BAT references it
# uppercased; DJGPP-built exe with > 8-char basename gets truncated by DOS and
# becomes unfindable from the generated batch invocation.
SDL3_MIXER_SMOKE_EXE := $(SDL3_MIXER_SMOKE_DIR)/mixsmk.exe

$(SDL3_MIXER_SMOKE_EXE): $(SDL3_MIXER_SMOKE_SRC) $(SYSROOT)/lib/libSDL3_mixer.a $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(SDL3_MIXER_SMOKE_DIR)
	$(CC) -march=i486 -mtune=pentium -O2 -Wall \
	      -I$(SYSROOT)/include \
	      -o $@ $< \
	      -L$(SYSROOT)/lib -lSDL3_mixer -lSDL3 -lm
	$(STUBEDIT) $@ minstack=2048k

.PHONY: sdl3-mixer-smoke
sdl3-mixer-smoke: $(SDL3_MIXER_SMOKE_EXE)
	tests/run-sdl3-mixer-smoke.sh

# --- Path B: SDL3_image functional smoke (#28 sharpening) --------------------
#
# Loads a hand-built 68-byte 1x1 RGBA PNG via IMG_Load_IO and verifies the
# returned SDL_Surface has the expected 1x1 geometry. Confirms libSDL3_image
# links against libSDL3 under DJGPP, the stb_image PNG decoder runs under
# DPMI, and SDL_Surface allocation works.

SDL3_IMAGE_SMOKE_DIR := $(BUILD_DIR)/sdl3-image-smoke
SDL3_IMAGE_SMOKE_SRC := tests/sdl3-image-smoke/imagetest.c
# 8.3 DOS filename -- basename "imgsmk" (6) + ".exe" (4); see sdl3-mixer-smoke
# for the rationale on why long basenames break headless DOSBox-X invocation.
SDL3_IMAGE_SMOKE_EXE := $(SDL3_IMAGE_SMOKE_DIR)/imgsmk.exe

$(SDL3_IMAGE_SMOKE_EXE): $(SDL3_IMAGE_SMOKE_SRC) $(SYSROOT)/lib/libSDL3_image.a $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(SDL3_IMAGE_SMOKE_DIR)
	$(CC) -march=i486 -mtune=pentium -O2 -Wall \
	      -I$(SYSROOT)/include \
	      -o $@ $< \
	      -L$(SYSROOT)/lib -lSDL3_image -lSDL3 -lm
	$(STUBEDIT) $@ minstack=2048k

.PHONY: sdl3-image-smoke
sdl3-image-smoke: $(SDL3_IMAGE_SMOKE_EXE)
	tests/run-sdl3-image-smoke.sh

# --- Wave 41 hwinv: 86Box parity smoke for HWINV.EXE --------------------------
#
# Companion to probe-engineer's `make hwinv-dosbox-smoke`. Runs the same
# build/probes/hwinv.exe binary under 86Box (closer-to-real-HW per
# docs/internal/WAVE-41-DOSBOX-PROFILING-PLAN.md sec. 5.1) and verifies:
#
#   1. All 9 HWINV-<TAG>-BEGIN + HWINV-<TAG>-DONE sentinel PAIRS emit
#      (TAG in {ENV, CPU, MEM, VID, AUD, DSK, IRQ, PORT, PCI} per probe-
#      engineer's emit shape; sentinel format is the load-bearing match).
#   2. The final [HWINV-EXIT_OK] marker emits (proves clean exit).
#   3. The DOSBOX_DETECTED=1 line is ABSENT (proves 86Box, not silent
#      mis-routing to DOSBox-X via xvfb-run failure cascade). Per probe-
#      engineer, hwinv emits DOSBOX_DETECTED=1 only when it detects
#      DOSBox via INT 21h AX=4452h. 86Box does not respond; the line
#      should read DOSBOX_DETECTED=0 (or be absent entirely; the gate
#      treats both as PASS).
#
# Failure of (3) is STOP-AND-ACK -- a =1 result means the harness
# silently fell back to DOSBox-X. This MUST NOT pass with a warning.
#
# Section-subset signal (per probe-engineer's 86Box guidance + WAVE-41-HW-
# INVENTORY-PROBE-PLAN sec. 5.1-5.2):
#   RUN under 86Box (silicon-proximity buys signal):
#     ENV / CPU / MEM / IRQ / PORT / PCI
#   RUN-BUT-TAG-EMULATOR-SPECIFIC:
#     AUD (86Box SB16 model unlikely to reproduce CTL0026 quirk)
#   SKIP / delegate elsewhere:
#     VID (HWLOG.EXE + CHIPID.EXE), DSK (86Box CF identify is fictitious)
#
# This gate runs the FULL HWINV.EXE and checks all 9 BEGIN/DONE pairs
# emit (we want sentinel parity for the 3-way-diff matrix). The
# subset-signal advice is recorded here for downstream analysis; the
# smoke does not skip sections at HWINV.EXE invocation level. If probe-
# engineer ships a --sections= flag in a future iter, this target gets
# a --sections=ENV,CPU,MEM,IRQ,PORT,PCI invocation alongside the full
# run for comparison.
#
# Prerequisites NOT installed by this target (operator-side; see
# tools/86box-run.sh header for the full bootstrap):
#   - 86Box AppImage at ~/emulators/86box/86Box.AppImage (or BOX86_APPIMAGE)
#   - 86Box ROMs at ~/emulators/86box/roms (or BOX86_ROMPATH)
#   - DOS hard-disk image at tools/86box-vm/doskutsu-c.img (or BOX86_IMG)
#     with CWSDPMI.EXE installed at C:\
#   - apt: mtools, xvfb
# If any prerequisite is missing, tools/86box-run.sh prints a specific
# message naming the missing piece + exits non-zero (rc=2 = bootstrap).

HWINV_EXE := $(BUILD_DIR)/probes/hwinv.exe
HWINV_86BOX_SMOKE_DIR := $(BUILD_DIR)/hwinv-86box-smoke

.PHONY: hwinv-86box-smoke
hwinv-86box-smoke:
	@test -x "$(HWINV_EXE)" || (echo "error: $(HWINV_EXE) missing -- build with 'make hwinv' (probe-engineer task #10)" >&2; exit 2)
	@mkdir -p $(HWINV_86BOX_SMOKE_DIR)
	@echo "[hwinv-86box-smoke] running HWINV.EXE under 86Box (parity conf: tools/86box-x.conf)"
	@tools/86box-run.sh --exe $(HWINV_EXE) --log $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG --timeout 90 || \
		(echo "[hwinv-86box-smoke] FAIL: 86box-run.sh did not produce a log" >&2; exit 3)
	@echo "[hwinv-86box-smoke] log captured ($$(wc -l <$(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG) lines)"
	@echo "[hwinv-86box-smoke] verifying all 9 HWINV-<TAG>-BEGIN/DONE pairs present..."
	@gate_fail=0; \
	for tag in ENV CPU MEM VID AUD DSK IRQ PORT PCI; do \
		bhits=$$(grep -cE "^\[HWINV-$$tag-BEGIN\]" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG 2>/dev/null || echo 0); \
		dhits=$$(grep -cE "^\[HWINV-$$tag-DONE\]" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG 2>/dev/null || echo 0); \
		if [ "$$bhits" -gt 0 ] && [ "$$dhits" -gt 0 ]; then \
			echo "  PASS  HWINV-$$tag  BEGIN=$$bhits DONE=$$dhits"; \
		else \
			echo "  FAIL  HWINV-$$tag  BEGIN=$$bhits DONE=$$dhits -- pair incomplete" >&2; \
			gate_fail=1; \
		fi; \
	done; \
	echo "[hwinv-86box-smoke] verifying [HWINV-EXIT_OK] terminal marker..."; \
	if grep -qE "^\[HWINV-EXIT_OK\]" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG; then \
		echo "  PASS  HWINV-EXIT_OK present"; \
	else \
		echo "  FAIL  HWINV-EXIT_OK absent -- probe did not reach clean exit" >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying CPUID_FAMILY=5 (Pentium-class; proves 86Box ap5s, not DOSBox-X 486 fictitious)..."; \
	if grep -qE "^\[HWINV-CPU\] CPUID_FAMILY=5" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG; then \
		fam_line=$$(grep -E "^\[HWINV-CPU\] CPUID_FAMILY=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  PASS  $$fam_line"; \
	else \
		fam_line=$$(grep -E "^\[HWINV-CPU\] CPUID_FAMILY=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  FAIL  expected CPUID_FAMILY=5 (Pentium under 86Box ap5s parity); got: $${fam_line:-no CPUID_FAMILY line}" >&2; \
		echo "        STOP-AND-ACK: 86Box machine config drift or harness misroute. Do NOT pass-with-warning." >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying ENVIRONMENT=86box (v2 classifier per probe-engineer task #16)..."; \
	if grep -qE "^\[HWINV-ENV\] ENVIRONMENT=86box" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG; then \
		env_line=$$(grep -E "^\[HWINV-ENV\] ENVIRONMENT=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  PASS  $$env_line"; \
	else \
		env_line=$$(grep -E "^\[HWINV-ENV\] ENVIRONMENT=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  FAIL  expected ENVIRONMENT=86box; got: $${env_line:-no ENVIRONMENT line}" >&2; \
		echo "        Workaround: 86box-run.sh must SET DOSKUTSU_ENVIRONMENT=86box at master DOS prompt" >&2; \
		echo "        (NOT inside HWBOX.BAT -- MS-DOS 6.22 env block may be too full for inner SET to" >&2; \
		echo "        propagate to child)." >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying DOSBOX_DETECTED=0 (v2 AND-gate per probe-engineer #16; was =1 false-positive in v1)..."; \
	if grep -qE "^\[HWINV-ENV\] DOSBOX_DETECTED=0" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG; then \
		echo "  PASS  DOSBOX_DETECTED=0"; \
	else \
		dbline=$$(grep -E "^\[HWINV-ENV\] DOSBOX_DETECTED=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  FAIL  expected DOSBOX_DETECTED=0; got: $${dbline:-no DOSBOX_DETECTED line}" >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying RUNMANIFEST schema-v1 block (flush-instr task #20)..."; \
	rm_begin=$$(grep -cE "^\[RUNMANIFEST-BEGIN\]" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG); \
	rm_end=$$(grep -cE "^\[RUNMANIFEST-END\]" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG); \
	rm_keys=$$(sed -n '/\[RUNMANIFEST-BEGIN\]/,/\[RUNMANIFEST-END\]/p' $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | grep -cE "^[a-z][a-z_0-9]*="); \
	if [ "$$rm_begin" -ge 1 ] && [ "$$rm_end" -ge 1 ] && [ "$$rm_keys" -ge 18 ]; then \
		echo "  PASS  RUNMANIFEST sentinels=$$rm_begin/$$rm_end keys=$$rm_keys"; \
	else \
		echo "  FAIL  RUNMANIFEST incomplete: BEGIN=$$rm_begin END=$$rm_end keys=$$rm_keys (expected 1/1/>=18)" >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying RUNMANIFEST environment field matches gate expectation..."; \
	if sed -n '/\[RUNMANIFEST-BEGIN\]/,/\[RUNMANIFEST-END\]/p' $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | grep -qE "^environment=86box[[:space:]]*$$"; then \
		echo "  PASS  environment=86box"; \
	else \
		rm_env=$$(sed -n '/\[RUNMANIFEST-BEGIN\]/,/\[RUNMANIFEST-END\]/p' $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | grep -E "^environment=" | head -1 | tr -d '\r'); \
		echo "  FAIL  expected environment=86box; got: $${rm_env:-no environment line}" >&2; \
		gate_fail=1; \
	fi; \
	echo "[hwinv-86box-smoke] verifying CIRRUS_CHIP_ID via PCI_FALLBACK (v2 per probe-engineer #16)..."; \
	if grep -qE "^\[HWINV-VID\] CIRRUS_CHIP_ID=0x00A8 .* CHIP_ID_SOURCE=PCI_FALLBACK" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG; then \
		echo "  PASS  CIRRUS_CHIP_ID=0x00A8 NAME=Cirrus CL-GD5434 via PCI_FALLBACK (86Box SR[0x27]=0x00; falls back to PCI walk)"; \
	else \
		cirrus_line=$$(grep -E "^\[HWINV-VID\] CIRRUS_CHIP_ID=" $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG | head -1); \
		echo "  FAIL  expected CIRRUS_CHIP_ID=0x00A8 via PCI_FALLBACK; got: $${cirrus_line:-no CHIP_ID line}" >&2; \
		gate_fail=1; \
	fi; \
	if [ "$$gate_fail" -gt 0 ]; then \
		echo "[hwinv-86box-smoke] GATE FAIL: see above" >&2; \
		echo "[hwinv-86box-smoke] log preserved at $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG" >&2; \
		exit 5; \
	fi
	@echo "[hwinv-86box-smoke] GATE PASS: 9 BEGIN/DONE pairs + EXIT_OK + CPUID_FAMILY=5 + ENVIRONMENT=86box + DOSBOX_DETECTED=0 + RUNMANIFEST schema-v1 + CIRRUS_CHIP_ID PCI_FALLBACK"
	@echo "[hwinv-86box-smoke] log: $(HWINV_86BOX_SMOKE_DIR)/HWINV.LOG"

# Convenience target alias -- the smoke is what's wanted; 86Box harness
# is the implementation detail.
.PHONY: 86box-smoke
86box-smoke: hwinv-86box-smoke

# --- Phase 9 wave 20 standalone diagnostic probes ----------------------------
#
# Builds tests/probes/*.c -- small, isolated DJGPP probes that characterize
# real-HW behavior in ways the live binary's in-context instrumentation can't
# reach. Each probe is a single .c file, pure DJGPP libc + chip-direct I/O,
# NO SDL / NO engine dependency, NO DPMI assumptions beyond what `dosmemput`
# and `__dpmi_int` need.
#
# Source + binaries are gitignored under /tests/probes/ (per
# memory/never_commit_internal_plans.md -- dev-only test scratch). The
# `probes` target itself is the discoverability anchor.
#
# Per-probe DOSBox-X smoke is correctness-only -- DOSBox-X is NOT a
# perf proxy (see memory/dosbox_not_perf_proxy.md). Real numbers require
# the CF-swap iter loop on g2k.
#
# 8.3 DOS filename rule: basename <= 8 chars, .ext <= 3 chars
# (memory/dos_filename_8_3.md). All probe binaries pass.
#
# P0 (load-bearing for Lever C go/no-go + every future iter):
#   DACPROG.EXE  -- VGA DAC programming-cost ablation (FULL/PART64/SINGLE)
#   HWLOG.EXE    -- VBE info + CRTC + chip ID + PCI config-space dump
#
# P1 / P2 probes will land here when authored.

PROBES_DIR     := $(BUILD_DIR)/probes
PROBES_INC     := $(REPO_ROOT)/include
PROBES_CFLAGS  := -march=i486 -mtune=pentium -O2 -Wall -I$(PROBES_INC)
PROBES_MINSTK  := 512k

PROBE_DACPROG_SRC := tests/probes/dacprog.c
PROBE_DACPROG_EXE := $(PROBES_DIR)/dacprog.exe

PROBE_HWLOG_SRC   := tests/probes/hwlog.c
PROBE_HWLOG_EXE   := $(PROBES_DIR)/hwlog.exe

# P1 -- bandwidth/cost characterization (task #8)
PROBE_DPMITHN_SRC := tests/probes/dpmithn.c
PROBE_DPMITHN_EXE := $(PROBES_DIR)/dpmithn.exe

PROBE_L1FILL_SRC  := tests/probes/l1fill.c
PROBE_L1FILL_EXE  := $(PROBES_DIR)/l1fill.exe

PROBE_PARTIAL_SRC := tests/probes/partial.c
PROBE_PARTIAL_EXE := $(PROBES_DIR)/partial.exe

# P3 -- diagnostic probes for the 13.5 ms flip() body baseline (task #12).
# YIELD.EXE links libSDL3 because SDL_Delay / SDL_PumpEvents are SDL3 entry
# points. CFFSYNC + IRQRATE remain pure-DJGPP.
PROBE_YIELD_SRC   := tests/probes/yield.c
PROBE_YIELD_EXE   := $(PROBES_DIR)/yield.exe

PROBE_CFFSYNC_SRC := tests/probes/cffsync.c
PROBE_CFFSYNC_EXE := $(PROBES_DIR)/cffsync.exe

PROBE_IRQRATE_SRC := tests/probes/irqrate.c
PROBE_IRQRATE_EXE := $(PROBES_DIR)/irqrate.exe

# P4 -- Phase 11 wave-22.5 path-B (tilemap caching) decision probe (task #14).
# 76800-byte read/write/memcpy decomposition + cache-tier sweep. Pure DJGPP.
PROBE_MEMBW_SRC   := tests/probes/membw.c
PROBE_MEMBW_EXE   := $(PROBES_DIR)/membw.exe

# P5 -- Phase 10 wave-22-WB-E MPU-401 / WaveBlaster init-sequence probe.
# Source name `mpuwbprobe.c` is host-side (no 8.3 limit); binary renamed to
# the 8.3-fitting MPUPROBE.EXE via explicit Makefile rule below (the generic
# %.exe pattern would emit `mpuwbprobe.exe` whose 9-char basename DOS would
# truncate to MPUWBPRO.EXE, breaking the matching MPUPROBE.LOG fopen).
# Pure DJGPP; no SDL deps. Targets the gate-blocker on g2k Vibra16S+S2 where
# SDL/0042 (probe) + SDL/0044 (blind init) both hang the ISA bus on direct
# MPU port access. See file header comment for full forensic protocol.
PROBE_MPUWB_SRC   := tests/probes/mpuwbprobe.c
PROBE_MPUWB_EXE   := $(PROBES_DIR)/mpuprobe.exe

# P6 -- Phase 10 wave W22-WB iter H reduced-scope SDL+MPU probe.
# Authors: SDL_Init(AUDIO) + audio device open + 1-sec service + direct-port
# MPU-401 init + MIDI byte writes -- all under SDL audio runtime. Discriminates
# "is SDL audio init alone enough to break direct-port MPU access?" -- a
# question MPUPROBE iter F (zero SDL) couldn't answer. SDL3-linked; minstack
# 2048k matches PROBE_YIELD recipe. Source basename `mpusdlprobe` (host-side,
# 11 chars) -> binary basename `mpusdl` (8.3-fit) via explicit rule below.
PROBE_MPUSDL_SRC  := tests/probes/mpusdlprobe.c
PROBE_MPUSDL_EXE  := $(PROBES_DIR)/mpusdl.exe

# P7 -- Phase 11 wave-22.5 / iter H visit-loop overhead measurement.
# Decides nx-engine slot 0113 (off-screen tile skip) ship/no-ship: measures
# real cost of iterating 30000 tiles vs 1900 tiles in tight loop with
# RDTSC timing. Source basename `tileprobe` (9 chars host-side) -> binary
# basename `tileprob` (8.3-fit) via explicit Makefile rule. Pure DJGPP.
PROBE_TILE_SRC    := tests/probes/tileprobe.c
PROBE_TILE_EXE    := $(PROBES_DIR)/tileprob.exe

# P8 -- Phase 11 wave-22.5 / iter H Pixtone synth cost characterization.
# Decides nx-engine slot 0114 (alternate-flip Pixtone mix) ship/no-ship via
# RDTSC-timed Pixtone-equivalent synth. Source basename `pixprobe` (8 chars
# host-side) -> binary `pixprob` (7+3 fits 8.3) per team-lead brief naming.
# Pure DJGPP + libm (math.h: sin in init only).
PROBE_PIX_SRC     := tests/probes/pixprobe.c
PROBE_PIX_EXE     := $(PROBES_DIR)/pixprob.exe

# P9 -- Phase 11 wave-25 / iter J probe suite (4 probes).
#
# AUDBUF.EXE -- SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES sweep (slot 0116 verify).
#   Sweeps buffer sizes {default,512,1024,2048,4096}, opens audio device at
#   each, samples doskutsu_audio_irq_count over 1 sec wall, computes IRQ-rate
#   ratio. SDL3-linked (PROBES_SDL_* recipe).
PROBE_AUDBUF_SRC  := tests/probes/audbuf.c
PROBE_AUDBUF_EXE  := $(PROBES_DIR)/audbuf.exe
#
# IDLEPROB.EXE -- DSP idle-pause CPU yield (slot 0115 verify).
#   Scenarios A (audio active) vs B (SDL_DOSAudioForcePause), both running
#   identical synth-engine loop for 1.0 sec wall; reports yield ms/flip.
#   SDL3-linked. Watchdog: 200 ms pause-engagement cap, 30 sec total cap.
PROBE_IDLE_SRC    := tests/probes/idleprob.c
PROBE_IDLE_EXE    := $(PROBES_DIR)/idleprob.exe
#
# OPAQUE.EXE -- opaque-tile bitmask audit (FPS-DEEPDIVE Cand #2 gating).
#   Pure DJGPP, no SDL. Parses 4bpp Windows BMP (data/Stage/PrtCave.pbm),
#   walks 16x16 tiles, counts colorkey (master-black palette index 0)
#   pixels per tile, reports pct_opaque + per-tile bitmask.
PROBE_OPAQUE_SRC  := tests/probes/opaque.c
PROBE_OPAQUE_EXE  := $(PROBES_DIR)/opaque.exe
#
# BLTFILL.EXE -- Cirrus 5434 BLT solid-fill vs dosmemput head-to-head
#               (FPS-DEEPDIVE Cand #4 gating).
#   v2 in iter K with 3-mode fallback ladder (COLOR_EXPAND -> PATTERN_COPY
#   -> BULK_COPY) + bit-3 busy poll + GR[0x0B] errata clear.
PROBE_BLTFILL_SRC := tests/probes/bltfill.c
PROBE_BLTFILL_EXE := $(PROBES_DIR)/bltfill.exe
#
# CHIPID.EXE -- Cirrus chip-detect + BLT-engagement forensic dump
#              (BLTFILL v2 companion for iter K).
#   Read-only register dump; lighter than HWLOG.EXE focused on
#   the BLT-engagement question. Dumps full CRTC + SR + GR + VBE info +
#   PCI config + decodes BLT engine extension regs.
PROBE_CHIPID_SRC  := tests/probes/chipid.c
PROBE_CHIPID_EXE  := $(PROBES_DIR)/chipid.exe

# P12 -- Phase 11 wave-36 task #10 Cirrus BLT async-parallelism probe.
#   Gates hail-mary slot 0133 (BLT async backdrop flush): does the chip
#   actually let the CPU do useful parallel work while the BLT runs? Source
#   basename `cirrus-blt-async-probe` (host-side, 21 chars, descriptive) ->
#   binary basename `bltasync` (8.3-fit) via explicit Makefile rule below.
#   Pure DJGPP. RDTSC-timed (calibrated against uclock at probe init).
PROBE_BLTASYNC_SRC := tests/probes/cirrus-blt-async-probe.c
PROBE_BLTASYNC_EXE := $(PROBES_DIR)/bltasync.exe

# P13 -- Phase 11 wave-36 Task A Cirrus BLT BULK_COPY variant-matrix probe.
#   Root-cause investigation of BLTASYNC v2 REFUTE_VERIFY_FAIL: 8 variants
#   systematically explore BULK_COPY register sequence + alternates (different
#   src offsets, BLT_RESET ordering, color-reg state, PATTERN_COPY,
#   COLOR_EXPAND reference anchor, offscreen-dst). Per-variant: raw dst
#   first-16 + last-16 hex bytes + auto-classified status. Source basename
#   `cirrus-blt-variant-probe` (host-side, 23 chars) -> binary basename
#   `bltvar` (8.3-fit) via explicit Makefile rule below.
PROBE_BLTVAR_SRC := tests/probes/cirrus-blt-variant-probe.c
PROBE_BLTVAR_EXE := $(PROBES_DIR)/bltvar.exe

# P14 -- Phase 11 wave-36 ceiling-bust probe A: LFB nearptr VRAM-write
#   throughput vs banked dosmemput. Tests whether the 19 MB/s dosmemput
#   ceiling is a banked-mode CPU-PIO artifact (not a chip bandwidth limit)
#   by directly mapping the Cirrus 5434 LFB via DPMI int 0x31 / fn 0x0800 +
#   __djgpp_nearptr_enable() + base-address subtraction for a dereferenceable
#   C pointer. Source basename `lfbnear-probe` (host-side, 13 chars) ->
#   binary basename `lfbnear` (8.3-fit) via explicit Makefile rule below.
PROBE_LFBNEAR_SRC := tests/probes/lfbnear-probe.c
PROBE_LFBNEAR_EXE := $(PROBES_DIR)/lfbnear.exe

# P15 -- Phase 11 wave-36 ceiling-bust probe B: Mode 13h packed-pixel bandwidth.
#   Tests whether bank-switch overhead OR per-byte CPU PIO is the dominant
#   dosmemput-bandwidth ceiling, by running same-shape transfers into a
#   single-bank 64000-byte FB at 0xA000:0. Cross-anchor against LFBNEAR
#   to triangulate: bank-switch vs DPMI thunk vs sysmem-to-VRAM CPU bandwidth.
PROBE_MODE13H_SRC := tests/probes/mode13h-probe.c
PROBE_MODE13H_EXE := $(PROBES_DIR)/mode13h.exe

# P16 -- Phase 11 wave-38 ride-along: Cirrus BLT PATTERN_COPY hail-mary
#   re-attempt of wave-36 V6 (which emitted dst=all-0x00 / FAIL_OTHER).
#   Variants V_PAT_A/B/C/D test mode-agnostic baseline, 1bpp-vs-8bpp
#   source-interp discriminator, BLT_RESET pre-programming, and src-adjacency
#   constraint. V7-lesson-aware probe authoring: FG+BG color regs set
#   explicitly per variant; classifier emits expected-vs-got hex for both
#   1bpp + 8bpp source interpretations. Source basename `bltpat-v2`
#   (host-side, descriptive) -> binary basename `bltpat` (8.3-fit).
PROBE_BLTPAT_SRC := tests/probes/bltpat-v2.c
PROBE_BLTPAT_EXE := $(PROBES_DIR)/bltpat.exe

# P17 -- Phase 11 wave-38 audio Tier 1: pure SB16 IRQ-hook wall-clock probe.
#   Isolates SB16 IRQ-5 dispatch + minimal-ISR cost from SDL_mixer mix cost.
#   Installs minimal ISR (ack DSP + count++; no RingCopyOut, no memset) +
#   runs DMA auto-init playback at 4 rate variants. Refutation candidate
#   vs wave-20 v3 "IRQ IS the cost" prior; informs wave-39 P1/P2/P9 dispatch.
PROBE_AUDRQ_SRC := tests/probes/audrq.c
PROBE_AUDRQ_EXE := $(PROBES_DIR)/audrq.exe

# P18 -- Phase 11 wave-38 audio Tier 1: SDL_MixAudio mix-cost benchmark.
#   Measures SDL3 mix-loop cost at 4 rate variants x 3 channel populations
#   = 12 sub-scenarios. Sizes wave-39 P7 (Lever G rate reduction) precisely;
#   discriminates P4 (silent-channel-skip) and P5 (mix-tick batching).
#   Uses SDL_MixAudio as pure function (no audio device opened).
PROBE_MIXBENCH_SRC := tests/probes/mixbench.c
PROBE_MIXBENCH_EXE := $(PROBES_DIR)/mixbench.exe

# P19 -- Phase 11 wave-38 audio Tier 2: Organya live-synth cost benchmark.
#   Ports Song::Synth() nearest-neighbour per-sample math (Organya.cpp
#   lines 282-328) into a pure DJGPP probe with synthetic wavetable.
#   Measures wave-39 P1 (OPL3) + P2 (WaveBlaster MIDI) candidate work-
#   elimination via MIDI hardware offload. 4 rate variants x 6 instruments.
PROBE_ORGSYNTH_SRC := tests/probes/orgsynth.c
PROBE_ORGSYNTH_EXE := $(PROBES_DIR)/orgsynth.exe

# P20 -- Phase 11 wave-40 task #29: WaveBlaster MIDI sanity probe.
#   Single-mechanism: send Note On + wait 1 sec + Note Off via MPU-401 UART
#   ports (0x330/0x331). First sanity gate for wave-41+ WaveBlaster MIDI
#   offload pipeline (Organya->MIDI bypass of SDL_mixer's mix loop). Per-stage
#   emits between every outportb so hang stage is pinpointed on first iter
#   per defensive_rewrite_requires_identified_failure_stage memory.
PROBE_WBMIDI_SRC := tests/probes/wbmidi.c
PROBE_WBMIDI_EXE := $(PROBES_DIR)/wbmidi.exe

# P21 -- Phase 11 wave-41 task #10 + wave-43 task #16: comprehensive HW-
#   inventory snapshot + RUNMANIFEST schema v1 emit.
#
#   Wave-41 MVP (task #10) per docs/internal/WAVE-41-HW-INVENTORY-PROBE-PLAN.md
#   sec. 4: 8 read-only sections (CPU/MEM/VID/AUD/DSK/IRQ+DMA/PORT/PCI) + ENV
#   marker header. Per-section BEGIN/DONE sentinels with 500 ms watchdog.
#   Pattern-S1 WaveBlaster enumeration (SB DSP reset + version query; ZERO MPU
#   port reads per SDL/0047 + MPUPROBE-W22WB-F finding #1: "MPU status bit 7
#   lies").
#
#   Wave-43 task #16 fixes integrated:
#     1. AND-gated DOSBOX_DETECTED via include/runmanifest.h helper (fixes the
#        OR-gate false-positive under 86Box that build-qa task #11 surfaced)
#     2. CIRRUS_CHIP_ID PCI fallback when SR[0x27]=0x00 (86Box Cirrus emulation
#        doesn't decode the SR extension reg even when PCI walk finds the chip)
#     3. ENVIRONMENT 3-state classifier + RUNMANIFEST schema-v1 emit per
#        docs/internal/WAVE-41-TRI-ENV-CORRELATION-PLAN.md sec. 4.2 + 4.3
#
#   Pure DJGPP. No SDL link. Side-effects bounded to chip-spec-mandated
#   non-destructive operations (Cirrus SR[0x06] unlock; SB DSP reset; OPL3
#   detect-then-reset). Real-HW iter: bundle alongside CWSDPMI.EXE.
#   Output -> HWINV.LOG. Runtime ~3 sec.
PROBE_HWINV_SRC  := tests/probes/hwinv.c
PROBE_HWINV_EXE  := $(PROBES_DIR)/hwinv.exe
PROBE_HWINV_HDR  := $(PROBES_INC)/runmanifest.h

# Stamp the binary's sha12 at compile time so the RUNMANIFEST block's
# binary_sha12 field can be cross-tier matched per schema v1 (proves the
# same binary ran in all three tiers). Computed from the .c source file
# sha256 (first 12 hex chars). tests/probes/ is gitignored so we can't
# use git tree sha; the source content sha is the next-best stable id.
PROBE_HWINV_SHA12 := $(shell sha256sum $(PROBE_HWINV_SRC) 2>/dev/null | cut -c1-12)

# Explicit rule: the generic pattern rule doesn't know HWINV depends on the
# shared runmanifest.h header. Without an explicit dep, a header-only edit
# wouldn't rebuild the binary.
$(PROBE_HWINV_EXE): $(PROBE_HWINV_SRC) $(PROBE_HWINV_HDR) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -DHWINV_BUILD_SHA12=\"$(PROBE_HWINV_SHA12)\" -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# SDLPROB1 + SDLPROB2 -- SDL3-DOS cost decomposition split into two binaries
#   per docs/PHASE11-SDLPROBE-CONTRACT.md (sdl-engine task #23 designs the
#   contract + authors sdlprob1; probe-engineer task #24 authors sdlprob2 +
#   shared sdlprobe_common.h forensic-protocol header).
#
#   SDLPROB1.EXE -- sdl-engine: per-primitive sec. 1 22 sub-cells + auxiliary sec. 3
#   SDLPROB2.EXE -- probe-engineer: composites sec. 2 A-D (this task #24)
#   sdlprobe_common.h -- probe-engineer: shared sec. 4 forensic-protocol header
#                       (BEGIN/DONE markers, watchdog, sample-stash, RDTSC
#                       calibration, scenario-emit framework). Both binaries
#                       #include this header.
#
# Both binaries link against post-slot-0048 libSDL3.a; ~5 min wall each on
# real HW.
PROBE_SDLPROB2_SRC := tests/probes/sdlprob2.c
PROBE_SDLPROB2_EXE := $(PROBES_DIR)/sdlprob2.exe
# sdlprob1.c (sdl-engine task #23): per-primitive sec. 1 22+ sub-cells +
# auxiliary sec. 3 PR #15377 hint status.
PROBE_SDLPROB1_SRC := tests/probes/sdlprob1.c
PROBE_SDLPROB1_EXE := $(PROBES_DIR)/sdlprob1.exe

# Generic build rule for any probe .c with no library deps (P0/P1/P3-pure).
$(PROBES_DIR)/%.exe: tests/probes/%.c | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for SDL3-linked probes -- overrides the pattern rule above
# because Make picks the more-specific dep + recipe when both match.
PROBES_SDL_CFLAGS := $(PROBES_CFLAGS) -I$(SYSROOT)/include
PROBES_SDL_LDLIBS := -L$(SYSROOT)/lib -lSDL3 -lm
PROBES_SDL_MINSTK := 2048k

$(PROBE_YIELD_EXE): $(PROBE_YIELD_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

# Explicit rule for MPU-401/WB probe -- maps source basename `mpuwbprobe`
# (host-side, descriptive) to binary basename `mpuprobe` (8.3-clean for DOS).
# Without this, the generic pattern rule `$(PROBES_DIR)/%.exe: tests/probes/%.c`
# would produce `mpuprobe.exe` from a `mpuprobe.c` source -- but team-lead
# requested the source filename be `mpuwbprobe.c`. Explicit rule has
# precedence over the pattern rule, so this works without conflicts.
$(PROBE_MPUWB_EXE): $(PROBE_MPUWB_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for the reduced-scope SDL+MPU probe -- same source-vs-binary
# basename divergence as PROBE_MPUWB (mpusdlprobe.c -> mpusdl.exe; the source
# name is descriptive and host-side, the binary fits 8.3 for DOS). This probe
# links libSDL3.a (PROBES_SDL_* recipe) + minstack=2048k to match YIELD.
$(PROBE_MPUSDL_EXE): $(PROBE_MPUSDL_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

# Explicit rule for visit-loop measurement probe -- same source-vs-binary
# basename divergence as PROBE_MPUWB (tileprobe.c -> tileprob.exe). Pure
# DJGPP, no SDL deps; uses the same PROBES_CFLAGS as P0/P1 probes.
$(PROBE_TILE_EXE): $(PROBE_TILE_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for Pixtone-synth-cost probe -- pixprobe.c -> pixprob.exe per
# team-lead brief naming convention. Links libm for sin() used in wavetable
# init only; the timed synth loop has no libm calls. Pure DJGPP otherwise.
$(PROBE_PIX_EXE): $(PROBE_PIX_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $< -lm
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# P9 explicit rules -- iter J probe suite.
# AUDBUF + IDLEPROB are SDL3-linked (PROBES_SDL_* recipe, minstack 2048k).
$(PROBE_AUDBUF_EXE): $(PROBE_AUDBUF_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

$(PROBE_IDLE_EXE): $(PROBE_IDLE_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

PROBE_SDLPROB2_HDR := tests/probes/sdlprobe_common.h
$(PROBE_SDLPROB2_EXE): $(PROBE_SDLPROB2_SRC) $(PROBE_SDLPROB2_HDR) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

PROBE_SDLPROB1_HDR := tests/probes/sdlprobe_common.h
$(PROBE_SDLPROB1_EXE): $(PROBE_SDLPROB1_SRC) $(PROBE_SDLPROB1_HDR) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

# OPAQUE + BLTFILL are pure DJGPP (use the generic %.exe pattern rule above).
# No explicit rules needed; the pattern handles them.

# Explicit rule for Cirrus BLT async probe -- same source-vs-binary basename
# divergence as PROBE_MPUWB / PROBE_TILE (host-side descriptive source name
# maps to 8.3-clean DOS binary basename). Pure DJGPP, no SDL deps.
$(PROBE_BLTASYNC_EXE): $(PROBE_BLTASYNC_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for Cirrus BLT variant-matrix probe (wave-36 Task A).
$(PROBE_BLTVAR_EXE): $(PROBE_BLTVAR_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for LFB nearptr probe (wave-36 ceiling-bust A).
$(PROBE_LFBNEAR_EXE): $(PROBE_LFBNEAR_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for Mode 13h bandwidth probe (wave-36 ceiling-bust B).
$(PROBE_MODE13H_EXE): $(PROBE_MODE13H_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for Cirrus BLT PATTERN_COPY hail-mary probe (wave-38 ride-along).
$(PROBE_BLTPAT_EXE): $(PROBE_BLTPAT_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for pure SB16 IRQ-hook wall-clock probe (wave-38 audio Tier 1).
$(PROBE_AUDRQ_EXE): $(PROBE_AUDRQ_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for SDL_MixAudio mix-cost probe (wave-38 audio Tier 1).
# SDL3-linked (like AUDBUF + YIELD); minstack 2048k.
$(PROBE_MIXBENCH_EXE): $(PROBE_MIXBENCH_SRC) $(SYSROOT)/lib/libSDL3.a | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_SDL_CFLAGS) -o $@ $< $(PROBES_SDL_LDLIBS)
	$(STUBEDIT) $@ minstack=$(PROBES_SDL_MINSTK)

# Explicit rule for Organya live-synth cost probe (wave-38 audio Tier 2).
# Pure DJGPP + libm (for pow() in panning math).
$(PROBE_ORGSYNTH_EXE): $(PROBE_ORGSYNTH_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $< -lm
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

# Explicit rule for WaveBlaster MIDI sanity probe (wave-40 task #29).
# Pure DJGPP, no library deps; output-only port writes.
$(PROBE_WBMIDI_EXE): $(PROBE_WBMIDI_SRC) | djgpp-check
	@mkdir -p $(PROBES_DIR)
	$(CC) $(PROBES_CFLAGS) -o $@ $<
	$(STUBEDIT) $@ minstack=$(PROBES_MINSTK)

.PHONY: dacprog hwlog dpmithn l1fill partial yield cffsync irqrate membw mpuwbprobe mpusdlprobe tileprobe pixprobe audbuf idleprob opaque bltfill chipid bltasync bltvar lfbnear mode13h bltpat audrq mixbench orgsynth wbmidi hwinv hwinv-dosbox-smoke sdlprob2 probes probes-p0 probes-p1 probes-p3 probes-p4 probes-p5 probes-p6 probes-p7 probes-p8 probes-p9 probes-p10 probes-p11 probes-p12 probes-p13 probes-p14 probes-p15 probes-p16 probes-p17 probes-p18 probes-p19 probes-p20 probes-p21
dacprog: $(PROBE_DACPROG_EXE)
	@echo "Built $(PROBE_DACPROG_EXE) -- ship via real-HW iter (DOSBox-X is correctness-only)."

hwlog: $(PROBE_HWLOG_EXE)
	@echo "Built $(PROBE_HWLOG_EXE) -- ship via real-HW iter (DOSBox-X PCI/VBE values differ from real HW)."

dpmithn: $(PROBE_DPMITHN_EXE)
	@echo "Built $(PROBE_DPMITHN_EXE)"

l1fill: $(PROBE_L1FILL_EXE)
	@echo "Built $(PROBE_L1FILL_EXE)"

partial: $(PROBE_PARTIAL_EXE)
	@echo "Built $(PROBE_PARTIAL_EXE)"

yield: $(PROBE_YIELD_EXE)
	@echo "Built $(PROBE_YIELD_EXE) -- SDL3-linked; ship via real-HW iter."

cffsync: $(PROBE_CFFSYNC_EXE)
	@echo "Built $(PROBE_CFFSYNC_EXE)"

irqrate: $(PROBE_IRQRATE_EXE)
	@echo "Built $(PROBE_IRQRATE_EXE)"

membw: $(PROBE_MEMBW_EXE)
	@echo "Built $(PROBE_MEMBW_EXE) -- phase11 wave-22.5 path-B decision probe."
	@echo "  Real-HW iter: bundle MEMBW.EXE + CWSDPMI.EXE + tests/probes/membw.bat;"
	@echo "  operator runs MEMBW.BAT once; output -> MEMBW.OUT (logback collects)."

mpuwbprobe: $(PROBE_MPUWB_EXE)
	@echo "Built $(PROBE_MPUWB_EXE) -- phase10 wave-22-WB-E MPU-401 / WaveBlaster"
	@echo "  init-sequence probe. Real-HW iter: bundle as MPUPROBE.EXE alongside"
	@echo "  CWSDPMI.EXE + tests/probes/mpuprobe.bat; operator runs MPUPROBE.BAT;"
	@echo "  output -> MPUPROBE.LOG (logback collects). HAZARD: this probe may"
	@echo "  hang the ISA bus on real Vibra16S+S2 -- that's the diagnostic signal."
	@echo "  Per-step BEGIN/DONE markers in the log identify the hung instruction."

mpusdlprobe: $(PROBE_MPUSDL_EXE)
	@echo "Built $(PROBE_MPUSDL_EXE) -- phase10 wave-22-WB iter H reduced-scope"
	@echo "  SDL+MPU probe. Tests direct-port MPU-401 init AFTER SDL audio is"
	@echo "  running (1 sec service). Real-HW iter: bundle as MPUSDL.EXE +"
	@echo "  CWSDPMI.EXE + tests/probes/mpusdl.bat. Output -> MPUSDL.LOG."
	@echo "  HAZARD: probe may hang mid-step on real HW; that IS the diagnostic."

tileprobe: $(PROBE_TILE_EXE)
	@echo "Built $(PROBE_TILE_EXE) -- phase11 wave-22.5 / iter H visit-loop"
	@echo "  overhead probe. Decides slot 0113 ship/no-ship via 30000-tile"
	@echo "  full-loop vs 1900-tile bbox-loop RDTSC measurement. Real-HW iter:"
	@echo "  bundle as TILEPROB.EXE + CWSDPMI.EXE + tests/probes/tileprob.bat."
	@echo "  Output -> TILEPROB.LOG. No hang risk; runtime ~1-3 sec."

pixprobe: $(PROBE_PIX_EXE)
	@echo "Built $(PROBE_PIX_EXE) -- phase11 wave-22.5 / iter H Pixtone synth"
	@echo "  cost probe. Decides slot 0114 (alternate-flip mix) ship/no-ship"
	@echo "  via faithful Pixtone-equivalent synth x 9-scenario sweep with"
	@echo "  RDTSC timing. Real-HW iter: bundle as PIXPROB.EXE + CWSDPMI.EXE +"
	@echo "  tests/probes/pixprob.bat. Output -> PIXPROB.LOG. No hang risk."

probes-p0: $(PROBE_DACPROG_EXE) $(PROBE_HWLOG_EXE)
	@echo "Built P0 probe set: dacprog.exe + hwlog.exe"

probes-p1: $(PROBE_DPMITHN_EXE) $(PROBE_L1FILL_EXE) $(PROBE_PARTIAL_EXE)
	@echo "Built P1 probe set: dpmithn.exe + l1fill.exe + partial.exe"

probes-p3: $(PROBE_YIELD_EXE) $(PROBE_CFFSYNC_EXE) $(PROBE_IRQRATE_EXE)
	@echo "Built P3 probe set: yield.exe + cffsync.exe + irqrate.exe"

probes-p4: $(PROBE_MEMBW_EXE)
	@echo "Built P4 probe set: membw.exe (wave-22.5 path-B decision)"

probes-p5: $(PROBE_MPUWB_EXE)
	@echo "Built P5 probe set: mpuprobe.exe (wave-22-WB-E MPU/WB init diagnosis)"

probes-p6: $(PROBE_MPUSDL_EXE)
	@echo "Built P6 probe set: mpusdl.exe (wave-22-WB iter H -- reduced-scope SDL+MPU)"

probes-p7: $(PROBE_TILE_EXE)
	@echo "Built P7 probe set: tileprob.exe (wave-22.5 / iter H -- visit-loop overhead)"

probes-p8: $(PROBE_PIX_EXE)
	@echo "Built P8 probe set: pixprob.exe (wave-22.5 / iter H -- Pixtone synth cost)"

audbuf: $(PROBE_AUDBUF_EXE)
	@echo "Built $(PROBE_AUDBUF_EXE) -- phase11 wave-25 / iter J SAMPLE_FRAMES sweep"
	@echo "  (slot 0116 deferred 2026-05-07; AUDBUF data-driven for iter K choice)."
	@echo "  Sweeps 256/384/512/1024/2048/4096; argmin(irq_wall_pct) = recommended."
	@echo "  SDL3-linked. Real-HW iter: bundle alongside CWSDPMI.EXE +"
	@echo "  tests/probes/audbuf.bat. Output -> AUDBUF.LOG."

idleprob: $(PROBE_IDLE_EXE)
	@echo "Built $(PROBE_IDLE_EXE) -- phase11 wave-25 / iter J slot 0115 verify."
	@echo "  SDL3-linked. Real-HW iter: bundle alongside CWSDPMI.EXE +"
	@echo "  tests/probes/idleprob.bat. Output -> IDLEPROB.LOG."
	@echo "  HAZARD: SDL_DOSAudioForcePause manipulates DSP DMA -- bus-lock"
	@echo "  potential on real Vibra16S; BEGIN/DONE markers in log identify"
	@echo "  the stalling instruction if hung."

opaque: $(PROBE_OPAQUE_EXE)
	@echo "Built $(PROBE_OPAQUE_EXE) -- phase11 wave-25 / iter J cand #2 gating."
	@echo "  Pure DJGPP, no hardware-IO. Real-HW iter: bundle alongside"
	@echo "  CWSDPMI.EXE + tests/probes/opaque.bat. Loads PrtCave.pbm from"
	@echo "  data/Stage/, outputs OPAQUE.LOG with pct_opaque + per-tile bitmask."

bltfill: $(PROBE_BLTFILL_EXE)
	@echo "Built $(PROBE_BLTFILL_EXE) -- phase11 wave-27 / iter K cand #4 gating (v2)."
	@echo "  Pure DJGPP. v2: bit-3 BLT_PROGRESS busy poll + GR[0x0B] errata clear +"
	@echo "  3-mode fallback ladder (COLOR_EXPAND -> PATTERN_COPY -> BULK_COPY)."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/bltfill.bat."
	@echo "  Output -> BLTFILL.LOG. Companion: CHIPID.EXE forensic dump."

chipid: $(PROBE_CHIPID_EXE)
	@echo "Built $(PROBE_CHIPID_EXE) -- phase11 wave-27 / iter K BLTFILL v2 companion."
	@echo "  Pure DJGPP, read-only chip-state probe (only side-effect: SR[0x06]"
	@echo "  Cirrus extension unlock -- non-destructive). Dumps full CRTC + SR + GR"
	@echo "  + VBE info + PCI config + decodes BLT engine extension regs."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/chipid.bat."
	@echo "  Output -> CHIPID.LOG."

sdlprob2: $(PROBE_SDLPROB2_EXE)
	@echo "Built $(PROBE_SDLPROB2_EXE) -- phase11 iter L composites sec. 2 (task #24)."
	@echo "  SDL3-linked. 4 composite scenarios A-D per docs/PHASE11-SDLPROBE-CONTRACT.md:"
	@echo "  A=tile_pre_0117  B=tile_post_0117  C=sprite_full  D=menu_INDEX8_alpha_CK_slow."
	@echo "  Includes shared sdlprobe_common.h forensic-protocol header (also used by"
	@echo "  sdl-engine's sdlprob1.c per task #23). Real-HW iter L is the data gate."
	@echo "  Output -> SDLPROB2.LOG. flush-instr authors SDLPROB2.BAT for iter L bundle."

sdlprob1: $(PROBE_SDLPROB1_EXE)
	@echo "Built $(PROBE_SDLPROB1_EXE) -- phase11 iter L per-primitive sec. 1 + aux sec. 3 (task #23)."
	@echo "  SDL3-linked. 24 sub-cell scenarios per docs/PHASE11-SDLPROBE-CONTRACT.md:"
	@echo "  GetTicks / PumpEvents (2) / BlitSurface (12 = 3 sizes x +/-CK x +/-alpha) /"
	@echo "  CreateTex+Destroy / RenderTexture (2) / ColorMod+BlendMode / RenderPresent /"
	@echo "  Delay(1) / audio_callback_overhead / irq_count_baseline. Auxiliary section 3"
	@echo "  emits PR #15377 hint status block at probe init. Includes shared sdlprobe_common.h."
	@echo "  Output -> SDLPROB1.LOG. flush-instr authors SDLPROB1.BAT for iter L bundle."

probes-p9: $(PROBE_AUDBUF_EXE) $(PROBE_IDLE_EXE) $(PROBE_OPAQUE_EXE) $(PROBE_BLTFILL_EXE)
	@echo "Built P9 probe set: audbuf.exe + idleprob.exe + opaque.exe + bltfill.exe"
	@echo "  Phase 11 wave-25 / iter J -- AUDBUF/IDLEPROB verify slot 0115/0116;"
	@echo "  OPAQUE/BLTFILL gate FPS-DEEPDIVE candidates #2/#4 for iter K."

# P10 -- Phase 11 wave-27 / iter K probe additions: BLTFILL v2 fixup + CHIPID
# forensic companion. BLTFILL v2 supersedes iter J's v1 (build target unchanged).
probes-p10: $(PROBE_BLTFILL_EXE) $(PROBE_CHIPID_EXE)
	@echo "Built P10 probe set: bltfill.exe (v2) + chipid.exe"
	@echo "  Phase 11 wave-27 / iter K -- BLTFILL v2 fixes register encoding +"
	@echo "  CHIPID forensic dump runs alongside as fallback diagnostic."

# P11 -- Phase 11 iter L SDL3-DOS cost decomposition: split into SDLPROB1
# (sdl-engine task #23) + SDLPROB2 (probe-engineer task #24, this lane).
# Aggregate target builds whichever is currently authored; sdlprob1 lands
# when sdl-engine completes their work.
probes-p11: $(PROBE_SDLPROB2_EXE)
	@echo "Built P11 probe set (partial): sdlprob2.exe (iter L -- composites sec. 2)"
	@echo "  sdlprob1.exe (sdl-engine task #23) lands separately."

bltasync: $(PROBE_BLTASYNC_EXE)
	@echo "Built $(PROBE_BLTASYNC_EXE) -- phase11 wave-36 task #10."
	@echo "  Gates hail-mary slot 0133 (Cirrus BLT async backdrop flush)."
	@echo "  Pure DJGPP; RDTSC-timed. Real-HW iter: bundle alongside CWSDPMI.EXE"
	@echo "  + tests/probes/bltasync.bat. Output -> BLTASYNC.LOG."
	@echo "  Decision gate in log: VERDICT=SHIP|DEFER|CANCEL|REFUTE_*."
	@echo "  HAZARD: enters VBE 8bpp mode; screen corrupted during BLT scenarios;"
	@echo "  restores text mode before exit. Runtime ~30-60 sec on PODP83."

bltvar: $(PROBE_BLTVAR_EXE)
	@echo "Built $(PROBE_BLTVAR_EXE) -- phase11 wave-36 Task A."
	@echo "  Root-cause investigation of BLTASYNC v2 REFUTE_VERIFY_FAIL: 8 variants"
	@echo "  systematically explore BULK_COPY register sequence + alternates."
	@echo "  Pure DJGPP. Per-variant: raw dst first-16 + last-16 hex bytes +"
	@echo "  auto-classified status. Real-HW iter: bundle alongside CWSDPMI.EXE"
	@echo "  + tests/probes/bltvar.bat. Output -> BLTVAR.LOG."
	@echo "  HAZARD: enters VBE 8bpp mode; restores text mode. Runtime ~5 sec."

lfbnear: $(PROBE_LFBNEAR_EXE)
	@echo "Built $(PROBE_LFBNEAR_EXE) -- phase11 wave-36 ceiling-bust A."
	@echo "  Tests whether 19 MB/s dosmemput ceiling is banked-mode-CPU-PIO artifact"
	@echo "  by mapping Cirrus 5434 LFB via DPMI + nearptr. Pure DJGPP."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/lfbnear.bat."
	@echo "  Output -> LFBNEAR.LOG. Verdict: SHIP/DEFER/DROP/FAIL_NO_LFB/FAIL_LFB_APERTURE_BUG."
	@echo "  HAZARD: enters VBE mode 0x4101; restores text. Runtime ~10 sec."

mode13h: $(PROBE_MODE13H_EXE)
	@echo "Built $(PROBE_MODE13H_EXE) -- phase11 wave-36 ceiling-bust B."
	@echo "  Tests Mode 13h (single-bank 320x200x8) vs banked-mode bandwidth."
	@echo "  Cross-anchor against LFBNEAR to triangulate bank-switch vs DPMI thunk"
	@echo "  vs sysmem-to-VRAM CPU bandwidth ceiling. Pure DJGPP."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/mode13h.bat."
	@echo "  Output -> MODE13H.LOG. Verdict: SHIP_MODE13H/SHIP_NEARPTR/DROP/HARNESS_SUSPECT."
	@echo "  HAZARD: enters Mode 13h; restores text. Runtime ~5 sec."

bltpat: $(PROBE_BLTPAT_EXE)
	@echo "Built $(PROBE_BLTPAT_EXE) -- phase11 wave-38 ride-along (PATTERN_COPY hail-mary)."
	@echo "  Re-attempts wave-36 V6 with V7-lesson-aware probe authoring: 4 variants"
	@echo "  (V_PAT_A baseline_uniform / V_PAT_B byte_checker / V_PAT_C reset_pre /"
	@echo "  V_PAT_D src_far) set FG+BG color regs explicitly, emit expected-vs-got hex"
	@echo "  for both 1bpp and 8bpp source interpretations. Pure DJGPP."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/bltpat.bat."
	@echo "  Output -> BLTPAT.LOG. Runtime ~5 sec."
	@echo "  HAZARD: enters VBE 8bpp mode; restores text mode before exit."

audrq: $(PROBE_AUDRQ_EXE)
	@echo "Built $(PROBE_AUDRQ_EXE) -- phase11 wave-39 task #18 (audrq v2 defensive re-author)."
	@echo "  Isolates SB16 IRQ-5 dispatch + minimal-ISR wall-clock from SDL_mixer mix cost."
	@echo "  4 rate variants: 44100s/22050s/11025s/11025m. 1 sec measurement each (v2)."
	@echo "  v2 defenses: ISR deadman + atexit panic + IRQ-vector verify + pre-mask-during-setup"
	@echo "  + per-stage progress emits. Status enum adds RATE_IRQ_STORM_DETECTED."
	@echo "  Pure DJGPP; raw SB16 DSP+DMA+IRQ-hook (no SDL link). RDTSC min/med/p95/max."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/audrq.bat."
	@echo "  Output -> AUDRQ.LOG. Runtime ~5-8 sec (down from v1's 15-20)."
	@echo "  HAZARD: directly programs SB16; 60-sec watchdog before Ctrl-Alt-Del."
	@echo "  Reduced operator-time on hang AND atexit panic handler in v2."

mixbench: $(PROBE_MIXBENCH_EXE)
	@echo "Built $(PROBE_MIXBENCH_EXE) -- phase11 wave-38 audio Tier 1 (SDL_MixAudio mix-cost)."
	@echo "  Measures SDL3 mix-loop cost: 4 rates x 3 channel-populations = 12 scenarios."
	@echo "  SDL3-linked but does NOT open audio device (pure SDL_MixAudio benchmark)."
	@echo "  Sizes wave-39 P7 (Lever G); discriminates P4 (silent-skip), P5 (batching)."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/mixbench.bat."
	@echo "  Output -> MIXBENCH.LOG. Runtime ~30-60 sec."
	@echo "  HAZARD: none (no audio device, no chip-state); standard DJGPP runtime."

orgsynth: $(PROBE_ORGSYNTH_EXE)
	@echo "Built $(PROBE_ORGSYNTH_EXE) -- phase11 wave-38 audio Tier 2 (Organya synth cost)."
	@echo "  Ports Organya Song::Synth nearest-neighbour per-sample math from vendor"
	@echo "  Organya.cpp. Synthetic wavetable (100x256). 6 simulated instruments."
	@echo "  Measures wave-39 P1 (OPL3) + P2 (WaveBlaster MIDI) work-elimination."
	@echo "  4 rate variants. Pure DJGPP + libm. No audio device, no chip-state."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/orgsynth.bat."
	@echo "  Output -> ORGSYNTH.LOG. Runtime ~10-20 sec."
	@echo "  HAZARD: none (offline synth; no IRQ/DMA touched)."

wbmidi: $(PROBE_WBMIDI_EXE)
	@echo "Built $(PROBE_WBMIDI_EXE) -- phase11 wave-40 task #29 (WaveBlaster MIDI sanity)."
	@echo "  Single-mechanism probe: Note On / wait 1 sec / Note Off via MPU-401."
	@echo "  Per-stage emits between every outportb (defensive_rewrite memory disc)."
	@echo "  First sanity gate for wave-41+ WaveBlaster MIDI offload pipeline."
	@echo "  Pure DJGPP, no SDL link, output-only port writes (0x330/0x331)."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE + tests/probes/wbmidi.bat."
	@echo "  Output -> WBMIDI.LOG. Runtime ~2 sec."
	@echo "  HAZARD: none (no IRQ/DMA/SDL/audio-device); pure port writes."

# P12 -- Phase 11 wave-36 task #10 Cirrus BLT async-parallelism probe.
# Standalone gate for slot 0133 hail-mary; bundled into wave-36 iter or its
# own quick-probe iter per team-lead direction.
probes-p12: $(PROBE_BLTASYNC_EXE)
	@echo "Built P12 probe set: bltasync.exe (wave-36 task #10 -- BLT async gate)"

# P13 -- Phase 11 wave-36 Task A BLT BULK_COPY variant matrix.
probes-p13: $(PROBE_BLTVAR_EXE)
	@echo "Built P13 probe set: bltvar.exe (wave-36 Task A -- BULK_COPY root-cause)"

# P14 -- Phase 11 wave-36 ceiling-bust A: LFB nearptr throughput probe.
probes-p14: $(PROBE_LFBNEAR_EXE)
	@echo "Built P14 probe set: lfbnear.exe (wave-36 ceiling-bust A -- LFB nearptr vs banked)"

# P15 -- Phase 11 wave-36 ceiling-bust B: Mode 13h packed-pixel bandwidth probe.
probes-p15: $(PROBE_MODE13H_EXE)
	@echo "Built P15 probe set: mode13h.exe (wave-36 ceiling-bust B -- Mode 13h vs banked)"

# P16 -- Phase 11 wave-38 ride-along: Cirrus BLT PATTERN_COPY hail-mary.
probes-p16: $(PROBE_BLTPAT_EXE)
	@echo "Built P16 probe set: bltpat.exe (wave-38 PATTERN_COPY hail-mary -- V_PAT_A/B/C/D)"

# P17 -- Phase 11 wave-38 audio Tier 1: pure SB16 IRQ-hook wall-clock probe.
probes-p17: $(PROBE_AUDRQ_EXE)
	@echo "Built P17 probe set: audrq.exe (wave-38 audio Tier 1 -- IRQ-hook wall-clock)"

# P18 -- Phase 11 wave-38 audio Tier 1: SDL_MixAudio mix-cost benchmark.
probes-p18: $(PROBE_MIXBENCH_EXE)
	@echo "Built P18 probe set: mixbench.exe (wave-38 audio Tier 1 -- mix-cost benchmark)"

# P19 -- Phase 11 wave-38 audio Tier 2: Organya live-synth cost benchmark.
probes-p19: $(PROBE_ORGSYNTH_EXE)
	@echo "Built P19 probe set: orgsynth.exe (wave-38 audio Tier 2 -- Organya synth cost)"

# P20 -- Phase 11 wave-40 task #29: WaveBlaster MIDI sanity probe.
probes-p20: $(PROBE_WBMIDI_EXE)
	@echo "Built P20 probe set: wbmidi.exe (wave-40 task #29 -- WaveBlaster MIDI sanity)"

# P21 -- Phase 11 wave-41 task #10: HW-inventory snapshot.
hwinv: $(PROBE_HWINV_EXE)
	@echo "Built $(PROBE_HWINV_EXE) -- phase11 wave-41 task #10 (HW-inventory MVP)."
	@echo "  Pure DJGPP. 8 read-only sections (CPU/MEM/VID/AUD/DSK/IRQ+DMA/PORT/PCI)"
	@echo "  + ENV marker header. Per-section BEGIN/DONE sentinels with 500 ms watchdog."
	@echo "  Pattern-S1 WaveBlaster enumeration (SB DSP reset + version query; ZERO"
	@echo "  MPU port reads). Real-HW iter: bundle alongside CWSDPMI.EXE +"
	@echo "  tests/probes/hwinv.bat. Output -> HWINV.LOG. Runtime ~3 sec."
	@echo "  HAZARD: minimal -- non-destructive Cirrus SR[0x06] unlock + SB DSP reset"
	@echo "  + OPL3 detect-then-reset. MPU-401 ports explicitly skipped (lockup-prone)."

probes-p21: $(PROBE_HWINV_EXE)
	@echo "Built P21 probe set: hwinv.exe (wave-41 task #10 -- HW-inventory MVP)"

# DOSBox-X correctness smoke for HWINV.EXE. Stages probe + CWSDPMI in a temp
# dir, runs hwinv.exe under DOSBox-X, verifies HWINV.LOG has every section's
# BEGIN+DONE sentinels and the EXIT_OK marker. Numbers are emulator-fictitious
# per [[dosbox_not_proxy]]; this gates emit-structure correctness only.
# Companion to build-qa's 86Box parity-smoke target (task #11).
hwinv-dosbox-smoke: $(PROBE_HWINV_EXE) $(CWSDPMI_EXE)
	@tests/run-hwinv-smoke.sh

probes: probes-p0 probes-p1 probes-p3 probes-p4 probes-p5 probes-p6 probes-p7 probes-p8 probes-p9 probes-p10 probes-p11 probes-p12 probes-p13 probes-p14 probes-p15 probes-p16 probes-p17 probes-p18 probes-p19 probes-p20 probes-p21
	@echo "Built ALL P0+P1+P3+P4+P5+P6+P7+P8+P9 probes."
	@echo "  Real-HW iter: bundle alongside CWSDPMI.EXE (memory/iter_must_include_cwsdpmi.md)"
	@echo "  Output filenames on CF: C:\\DACPROG.LOG  C:\\HWLOG.LOG  C:\\DPMITHN.LOG  C:\\L1FILL.LOG  C:\\PARTIAL.LOG  C:\\YIELD.LOG  C:\\CFFSYNC.LOG  C:\\IRQRATE.LOG  C:\\MEMBW.OUT (BAT redirect)  C:\\MPUPROBE.LOG  C:\\MPUSDL.LOG  C:\\TILEPROB.LOG  C:\\PIXPROB.LOG  C:\\AUDBUF.LOG  C:\\IDLEPROB.LOG  C:\\OPAQUE.LOG  C:\\BLTFILL.LOG"

# --- Distribution -------------------------------------------------------------
#
# make dist        produces dist/doskutsu-cf.zip with the legal-complete payload
# make dist-list   prints the manifest of what dist would package, without
#                  building the binary or staging files -- for sanity-checking
#                  the bundle composition against PLAN.md sec. Licensing
# make install     copies the same payload to a mounted CF card ($CF required)
#
# PAYLOAD (matches PLAN.md sec. Licensing sec. Downstream redistribution checklist):
#   DOSKUTSU.EXE       the binary
#   CWSDPMI.EXE        DPMI host
#   CWSDPMI.DOC        CWSDPMI redistribution terms (required by its license)
#   LICENSE.TXT        this repo's MIT license
#   GPLV3.TXT          NXEngine-evo's GPLv3 (dominant license of the binary)
#   THIRD-PARTY.TXT    attribution matrix (CRLF normalized)
#   README.TXT         DOS-readable quick-start + asset-extraction pointer
#   DATA/...           NXEngine-evo bundled engine data -- fonts, baseline
#                      .pbm backgrounds, sprite metadata, JSON configs,
#                      tilekey.dat, StageMeta/, endpic/. Cloned verbatim
#                      from vendor/nxengine-evo/data/ -- GPLv3-inherited.
#
# Cave Story freeware game data (maps, NPC sprites, .org music, .pxt SFX,
# wavetable.dat, stage.dat) is **NEVER** in this zip -- those come from the
# user's own Doukutsu.exe extraction per docs/ASSETS.md. The DATA/ subdir
# in the dist contains only what NXEngine-evo upstream ships in its data/
# directory; users add their extracted Cave Story content on top after install.

CF             ?=
DIST_DIR       := $(REPO_ROOT)/dist
CF_STAGE       := $(DIST_DIR)/doskutsu-cf
CF_ZIP         := $(DIST_DIR)/doskutsu-cf.zip

# CRLF filter for DOS-facing text
CRLF := awk 'BEGIN{ORS="\r\n"} {sub(/\r$$/, ""); print}'

# GPL text source: the cloned NXEngine-evo tree ships its LICENSE file at the root.
NX_LICENSE := $(NXENGINE_SRC)/LICENSE

# Engine-bundled data tree -- cloned verbatim into the zip's DATA/ subdir.
# Contents (as of vendor SHA pinned in vendor/sources.manifest): bitmap
# fonts (font_*.fnt + font_*_*.png), Face*.pbm dialog portraits, sprites.sif
# atlas, tilekey.dat, system.json + music.json + music_dirs.json, spot.png
# focus glow, several bk*.pbm parallax backgrounds (the *480fix variants are
# the engine's full-HD overrides, kept since 320x240 mode never reaches
# them), StageMeta/*.json (~54 stage-metadata records), endpic/credit*.bmp.
# Total ~86 files, ~3.5 MiB. Verify with `make dist-list`.
NX_DATA_SRC    := $(NXENGINE_SRC)/data

define DIST_README
DOSKUTSU - Cave Story for MS-DOS 6.22
=====================================

DOSKUTSU is a port of Cave Story (Doukutsu Monogatari) via NXEngine-evo
to MS-DOS 6.22, cross-compiled with DJGPP against a DOS-ported SDL3.

HOW TO RUN
----------

 1. Place DOSKUTSU.EXE, CWSDPMI.EXE, and the DATA directory in the same
    folder on your DOS machine, e.g. C:\DOSKUTSU\.
 2. Extract Cave Story game data INTO that DATA directory (see below).
 3. Boot DOS with HIMEM.SYS loaded and NO EMS page frame (DJGPP uses DPMI).
 4. Ensure your SB16 BLASTER environment variable is set correctly,
    e.g.  SET BLASTER=A220 I5 D1 H5 T6
 5. Load a VESA 1.2+ BIOS driver if your video card doesn't provide one
    in its firmware (UNIVBE as fallback).
 6. Run:
        C:\>CD \DOSKUTSU
        C:\DOSKUTSU>DOSKUTSU

YOU MUST SUPPLY CAVE STORY DATA
-------------------------------

This bundle includes only the NXEngine-evo engine data (fonts, baseline
backgrounds, sprite metadata) under DATA\. It does NOT include the Cave
Story game content (maps, NPC sprites, music, sound effects, Organya
wavetable). You must extract those from the 2004 freeware Doukutsu.exe
yourself. Source: https://www.cavestory.org/

The extracted Cave Story content is added to the same DATA directory
that ships with this bundle, populating these subdirectories alongside
what's already there:

    C:\DOSKUTSU\DATA\Stage\          (Cave Story maps: .pxm/.pxe/.pxa/.tsc)
    C:\DOSKUTSU\DATA\Npc\            (Cave Story NPC sprites)
    C:\DOSKUTSU\DATA\org\            (Cave Story Organya music)
    C:\DOSKUTSU\DATA\pxt\            (Cave Story Pixtone SFX params)
    C:\DOSKUTSU\DATA\wavetable.dat   (Organya synth PCM, from Doukutsu.exe)
    C:\DOSKUTSU\DATA\stage.dat       (stage index, generated by extract script)

There is no DATA\BASE\ subdirectory; all assets coexist directly under
DATA\. See the project's docs/ASSETS.md for extraction details.

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
the complete attribution matrix. The DATA\ contents shipped here inherit
NXEngine-evo's GPLv3.

SOURCE
------

Full source, including build scripts and DOS-port patches:
    @REPO_URL@
endef
export DIST_README

# --- dist-list -- manifest dry-run ---------------------------------------------
#
# Prints what `make dist` would package, without building doskutsu.exe or
# staging files. Used to sanity-check the bundle composition against
# PLAN.md sec. Licensing sec. Downstream redistribution checklist before cutting
# a release. Sources that don't exist (e.g. vendor tree not cloned, binary
# not built) are flagged "[MISSING]" but do not fail the target -- this is
# intentional: dist-list answers "would this bundle the right things?"
# regardless of whether the build artifacts are present yet.

.PHONY: dist-list
dist-list:
	@printf '== make dist manifest (dry run) ==\n'
	@printf '   target zip: %s\n\n' "$(CF_ZIP)"
	@printf 'top-level files:\n'
	@$(call _dist_list_entry,DOSKUTSU.EXE,$(BUILD_DIR)/doskutsu.exe,binary (rename to upper-case))
	@$(call _dist_list_entry,CWSDPMI.EXE,$(CWSDPMI_EXE),DPMI host (vendored in repo))
	@$(call _dist_list_entry,CWSDPMI.DOC,$(CWSDPMI_DOC),CWSDPMI redistribution terms)
	@$(call _dist_list_entry,LICENSE.TXT,$(REPO_ROOT)/LICENSE,MIT - repo source [CRLF])
	@$(call _dist_list_entry,GPLV3.TXT,$(NX_LICENSE),GPLv3 - binary as a whole [CRLF])
	@$(call _dist_list_entry,THIRD-PARTY.TXT,$(REPO_ROOT)/THIRD-PARTY.md,attribution matrix [CRLF])
	@printf '  %-22s %-55s %s\n' "README.TXT" "(generated from DIST_README)" "DOS-readable quick start [CRLF]"
	@printf '\nDATA/ subdirectory (engine-bundled - NXEngine-evo GPLv3):\n'
	@# LC_ALL=C on the sort: byte-order, locale-stable. Without this the
	@# manifest output drifts between en_US.UTF-8 and C locales (UTF-8
	@# collation treats underscore specially, so 'Face_0.pbm' sorts before
	@# 'Face.pbm' under UTF-8 but after under C). Same trap as the
	@# patches/<name>/ alpha-suffix numbering issue -- keep dry-run output
	@# diffable across reviewer environments.
	@if [ -d "$(NX_DATA_SRC)" ]; then \
	    cd "$(NX_DATA_SRC)" && find . -type f | LC_ALL=C sort | sed 's|^\./|  DATA/|'; \
	    count=$$(find "$(NX_DATA_SRC)" -type f | wc -l); \
	    bytes=$$(find "$(NX_DATA_SRC)" -type f -exec stat -c '%s' {} + | awk '{s+=$$1} END{print s+0}'); \
	    printf '  (%d files, %d bytes from %s)\n' "$$count" "$$bytes" "$(NX_DATA_SRC)"; \
	else \
	    printf '  [MISSING] %s -- run scripts/fetch-sources.sh\n' "$(NX_DATA_SRC)"; \
	fi
	@printf '\nNOT included (per PLAN.md section Licensing item 4):\n'
	@printf '  Cave Story freeware game data -- user extracts per docs/ASSETS.md.\n'
	@printf '  Specifically excluded: data/Stage/, data/Npc/, data/org/, data/pxt/,\n'
	@printf '  data/wavetable.dat, data/stage.dat, and any data/wav/ content.\n'

# Helper for dist-list: prints "  <staged-name>  <src-path>  <comment>" with a
# [MISSING] tag if the source path doesn't exist. Args via $(call); commas
# separate args so don't put commas in args. Don't pad call sites with
# whitespace alignment -- $(call) does NOT strip leading whitespace from args
# and would inject spaces into the path (test -e " /path" then fails).
define _dist_list_entry
	if [ -e "$(2)" ]; then \
	    printf '  %-22s %-55s %s\n' "$(1)" "$(2)" "$(3)"; \
	else \
	    printf '  %-22s %-55s %s\n' "$(1)" "[MISSING] $(2)" "$(3)"; \
	fi
endef

.PHONY: dist
dist: $(BUILD_DIR)/doskutsu.exe | fetch-binaries
	@test -f "$(CWSDPMI_EXE)"   || (echo "error: $(CWSDPMI_EXE) missing -- run ./scripts/fetch-vendor-binaries.sh" >&2; exit 1)
	@test -f "$(CWSDPMI_DOC)"   || (echo "error: $(CWSDPMI_DOC) missing" >&2; exit 1)
	@test -f "$(NX_LICENSE)"    || (echo "error: $(NX_LICENSE) missing -- run scripts/fetch-sources.sh" >&2; exit 1)
	@test -d "$(NX_DATA_SRC)"   || (echo "error: $(NX_DATA_SRC) missing -- run scripts/fetch-sources.sh" >&2; exit 1)
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
	@# Engine-bundled data tree -> DATA/ in the zip. cp -R preserves the
	@# StgMeta/ and endpic/ subdirs; no Cave Story freeware data here.
	@# bk*480fix.pbm files are widescreen-only backdrops; the source path
	@# that would load them (map.cpp:560) is gated on `widescreen` which
	@# patch 0005-renderer-lock-320x240-fullscreen forces to false on DOS.
	@# Excluding them from the dist saves ~2 MB and dodges 5 of the 76
	@# 8.3-violator filenames inventoried in
	@# docs/PHASE8-LFN-RENAME-PLAN.md without needing to rename them.
	@mkdir -p "$(CF_STAGE)/DATA"
	@cp -R "$(NX_DATA_SRC)/." "$(CF_STAGE)/DATA/"
	@rm -f "$(CF_STAGE)/DATA/"bk*480fix.pbm
	@(cd "$(CF_STAGE)" && zip -q -r "$(CF_ZIP)" .)
	@echo "built $(CF_ZIP) ($$(stat -c '%s' $(CF_ZIP)) bytes)"

# --- Runtime staging for DOSBox-X testing -------------------------------------
#
# `make stage` produces $(BUILD_DIR)/stage/ -- the DOS-side runtime layout
# (DOSKUTSU.EXE + CWSDPMI.EXE + DATA/) -- which is what tools/dosbox-launch.sh
# mounts as C: when invoked with `--stage`. NXEngine-evo's ResourceManager
# resolves data via SDL_GetBasePath() + "data/" on DOS, so the .exe and the
# data tree must be co-located at runtime; the repo layout (build/doskutsu.exe
# + data/ at repo root) doesn't satisfy that on its own.
#
# data/ is symlinked rather than copied -- fast iteration, no rsync churn, and
# DOSBox-X's host-mount layer follows the symlink transparently. The symlink
# is recreated each run to track repo-side data/ updates without stale-link
# guards.

STAGE_DIR := $(BUILD_DIR)/stage

.PHONY: stage
stage: $(BUILD_DIR)/doskutsu.exe | fetch-binaries
	@test -f "$(CWSDPMI_EXE)" || (echo "error: $(CWSDPMI_EXE) missing -- run ./scripts/fetch-vendor-binaries.sh" >&2; exit 1)
	@mkdir -p "$(STAGE_DIR)"
	@# SDL/0024 routes SDL_Log to /DOSKUTSU/sdldbg.log (or /DOSKUTSU/<TAG>SDL.LOG
	@# when DOSKUTSU_LOG_TAG is set). Under the staged layout DOSBox-X mounts
	@# STAGE_DIR as C:, so the engine fopens "/DOSKUTSU/sdldbg.log" -> host path
	@# $(STAGE_DIR)/DOSKUTSU/sdldbg.log. DJGPP fopen silently returns NULL when
	@# the target dir is missing; SDL_Log messages get lost without any error.
	@# Pre-create the subdir so the smoke gate's banner-emit check sees SDL_Log
	@# output (real-HW CF cards already have this dir from `make install`).
	@mkdir -p "$(STAGE_DIR)/DOSKUTSU"
	@install -m 0644 $(BUILD_DIR)/doskutsu.exe "$(STAGE_DIR)/DOSKUTSU.EXE"
	@install -m 0644 $(CWSDPMI_EXE)            "$(STAGE_DIR)/CWSDPMI.EXE"
	@if [ -d "$(REPO_ROOT)/data" ]; then \
	    rm -f "$(STAGE_DIR)/data" "$(STAGE_DIR)/DATA"; \
	    ln -s "$(REPO_ROOT)/data" "$(STAGE_DIR)/data"; \
	    echo "staged $(STAGE_DIR)/ (data/ symlinked from repo)"; \
	else \
	    echo "note: data/ not present at repo root -- see docs/ASSETS.md"; \
	    echo "      $(STAGE_DIR)/ contains only DOSKUTSU.EXE + CWSDPMI.EXE"; \
	fi

.PHONY: install
install: $(BUILD_DIR)/doskutsu.exe | fetch-binaries
ifeq ($(strip $(CF)),)
	@echo "error: set CF=/path/to/cf/mount (e.g. make install CF=/mnt/cf)" >&2; exit 1
else
	@test -d "$(CF)" || (echo "error: CF=$(CF) is not a directory" >&2; exit 1)
	@test -f "$(CWSDPMI_EXE)" || (echo "error: $(CWSDPMI_EXE) missing -- run ./scripts/fetch-vendor-binaries.sh" >&2; exit 1)
	@test -f "$(CWSDPMI_DOC)" || (echo "error: $(CWSDPMI_DOC) missing" >&2; exit 1)
	@mkdir -p "$(CF)/DOSKUTSU"
	@install -m 0644 $(BUILD_DIR)/doskutsu.exe "$(CF)/DOSKUTSU/DOSKUTSU.EXE"
	@install -m 0644 $(CWSDPMI_EXE)            "$(CF)/DOSKUTSU/CWSDPMI.EXE"
	@install -m 0644 $(CWSDPMI_DOC)            "$(CF)/DOSKUTSU/CWSDPMI.DOC"
	@if [ -d "$(REPO_ROOT)/data" ]; then \
	    echo "copying data tree to $(CF)/DOSKUTSU/DATA/"; \
	    mkdir -p "$(CF)/DOSKUTSU/DATA"; \
	    cp -r "$(REPO_ROOT)/data/"* "$(CF)/DOSKUTSU/DATA/"; \
	    rm -f "$(CF)/DOSKUTSU/DATA/"bk*480fix.pbm; \
	    echo "  (excluded bk*480fix.pbm -- dead code on DOS per patch 0005)"; \
	else \
	    echo "note: data/ not present -- see docs/ASSETS.md for extraction"; \
	fi
	@echo "installed doskutsu payload to $(CF)/DOSKUTSU/"
endif

# --- TAS smoke (patch nxengine-evo/0135, wave 41) ----------------------------
#
# Round-trips the TAS subsystem under DOSBox-X: records a short
# title-screen session into PLAY.TAS, then replays it back. Validates
# the .TAS file magic + the engagement banners in both passes.
#
# Depends on `make stage` (full layout under build/stage/) since
# DOSKUTSU.EXE requires the data/ tree.

.PHONY: tas-smoke
tas-smoke: stage
	tests/run-tas-smoke.sh

# --- Host tooling: org2mid (Linux-only Organya -> SMF converter) --------------
#
# Wave 41 first cycle. tools/org2mid/ is a standalone C99 host tool (no DJGPP,
# no SDL, no engine link). Converts data/org/*.org files into Standard MIDI
# Files compatible with the engine's MidiScheduler (vendor/nxengine-evo/src/
# sound/MidiScheduler.{h,cpp}). Output ships nowhere on the CF card by itself;
# it's an offline pre-build artifact for wave-41's task #4 listening session.
#
# Output directory note: `make convert-music` writes to data/mid/. This dir is
# OFFLINE-ONLY for now -- the engine recognizes SDL_HINT_DOSKUTSU_AUDIO_MIDI_
# SOURCE=wiimidi (-> data/midi/) and =orgmid (-> data/orgmid/) only. There is
# NO engine path to data/mid/ today. A future cycle ("WB-with-org2mid" iter)
# will either add a new value =org2mid -> data/mid/ or replace data/midi/
# contents with our converter output. Don't accidentally wire data/mid/ into
# the engine's search path here -- that's the next cycle's lever.
#
# The data/ directory is .gitignored at the repo root (/data/), so data/mid/
# is automatically untracked. Treat as user-extracted/build-time-derived,
# same posture as data/base/.

ORG2MID_DIR := $(REPO_ROOT)/tools/org2mid
ORG2MID_BIN := $(ORG2MID_DIR)/org2mid
ORG_SRC_DIR := $(REPO_ROOT)/data/org

# Wave-42 (patch 0137; GM_VARIANT engine env var; revised 2026-05-14
# after STOP-2 design-flaw catch): GM_VARIANT controls our org2mid's
# GM-patch table; ONLY affects the orgmid source path. Wiimidi source
# is external GitHub content with pre-baked GM events not under our
# converter's control -- variant is a no-op on wiimidi.
#
# 3-tool, 4-output story (engine resolution table per team-lead's
# corrected wave-42 spec):
#   SOURCE     VARIANT  ->  dir            ; provenance
#   wiimidi    (any)    ->  data/midi/     ; external WiiMidi GitHub
#   orgmid     (unset)  ->  data/orgmid/   ; Hart's ORGMID (legacy)
#   orgmid     v1       ->  data/orgmid1/  ; our org2mid v1 GM
#   orgmid     v2       ->  data/orgmid2/  ; our org2mid v2 GM
#
# DOS 8.3 dir-naming preserved (7-char basenames; no orgmid-v1 trap).
ORGMID_V1_DIR := $(REPO_ROOT)/data/orgmid1
ORGMID_V2_DIR := $(REPO_ROOT)/data/orgmid2

.PHONY: org2mid
org2mid: $(ORG2MID_BIN)

# Sub-make PATH override: the top-level Makefile exports PATH with DJGPP_BIN
# prepended (line 30) so all build-stage targets find the cross-compiler. But
# tools/org2mid/ is a HOST build -- host `cc` would then find DJGPP `as` first
# on PATH and fail with "as: unrecognized option '--64'" because the host x86_64
# assembler flags don't match the DJGPP i486 assembler. Override PATH back to a
# host-default set for just this sub-make invocation.
$(ORG2MID_BIN): $(ORG2MID_DIR)/org2mid.c $(ORG2MID_DIR)/Makefile
	@PATH=/usr/local/bin:/usr/bin:/bin $(MAKE) -C $(ORG2MID_DIR)

# convert-music: generate the 2 wave-42 A/B-listening .mid dirs from
# data/org/*.org using our org2mid converter. v1 = original task #2
# design-doc GM mapping; v2 = wave-42 retune addressing operator
# wave-41 "thumps and cowbell" feedback. Both dirs ship in the wave-42
# tarball; operator A/B's between them via the SDL_HINT_DOSKUTSU_AUDIO_
# MIDI_GM_VARIANT engine env var in TAS-deterministic PLAY cells.
#
# Does NOT generate data/midi-v1/ or data/midi-v2/: wiimidi source is
# external GitHub content (data/midi/, fetched by scripts/fetch-cs-
# midi.py); GM_VARIANT cannot rewrite its pre-baked GM events without
# a separate midi-retagger tool (out of scope for wave-42 per team-
# lead arbitration). data/orgmid/ (Hart's ORGMID, legacy from wave-41)
# is also untouched; ships alongside as the 4th audio reference.
.PHONY: convert-music
convert-music: $(ORG2MID_BIN)
	@if [ ! -d "$(ORG_SRC_DIR)" ]; then \
	    echo "error: $(ORG_SRC_DIR) not present -- see docs/ASSETS.md for extraction" >&2; \
	    exit 1; \
	fi
	@mkdir -p $(ORGMID_V1_DIR) $(ORGMID_V2_DIR)
	@for variant in v1 v2; do \
	    case "$$variant" in v1) outdir=$(ORGMID_V1_DIR);; v2) outdir=$(ORGMID_V2_DIR);; esac; \
	    n_done=0; n_fail=0; \
	    for f in $(ORG_SRC_DIR)/*.org; do \
	        base=`basename $$f .org`; \
	        out=$$outdir/$$base.mid; \
	        if $(ORG2MID_BIN) --force --gm-table=$$variant $$f $$out >/dev/null 2>&1; then \
	            n_done=$$((n_done + 1)); \
	        else \
	            echo "convert-music: FAIL $$f -> $$outdir" >&2; \
	            n_fail=$$((n_fail + 1)); \
	        fi; \
	    done; \
	    printf '  %s/%s: %d converted, %d failed (--gm-table=%s)\n' "data" "`basename $$outdir`" "$$n_done" "$$n_fail" "$$variant"; \
	    if [ $$n_fail -gt 0 ]; then exit 1; fi; \
	done; \
	echo "convert-music: 2 dirs populated (data/orgmid1, data/orgmid2)"

# --- Housekeeping -------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@$(MAKE) -C $(ORG2MID_DIR) clean 2>/dev/null || true

.PHONY: distclean
distclean: clean
	rm -rf $(DIST_DIR)
	rm -rf $(SDL3_SRC) $(COMPAT_SRC) $(MIXER_SRC) $(IMAGE_SRC) $(NXENGINE_SRC)
	@echo "distclean: vendor/cwsdpmi/ retained; vendor/sources.manifest retained"
