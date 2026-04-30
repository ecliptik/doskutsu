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
# `__attribute__((target("sse")))` — even though our P54C / 486 target has
# no SSE. SDL3 itself sets `SDL_DISABLE_SSE=1` in its INTERNAL build_config.h
# so its own code is fine, but downstream consumers (SDL3_mixer, SDL3_image,
# NXEngine) compile without that internal config and pick up the SSE intrinsic
# paths — which then emit a runtime check that fails on Pentium-class hardware
# (e.g. SDL_mixer.c:685 `MIX_Init: Need SSE instructions but this CPU doesn't
# offer it`). Forwarding these defines through CMAKE_C_FLAGS suppresses the
# intrinsic gate at every consumer's preprocessor level. Includes the AVX
# family for completeness — same upstream issue applies. Found via #26 spike;
# upstream issue draft at .tmp/upstream-sdl-issue-sdl-intrin-propagation.md.
NOSIMD_FLAGS := -DSDL_DISABLE_MMX=1 -DSDL_DISABLE_SSE=1 -DSDL_DISABLE_SSE2=1 \
                -DSDL_DISABLE_SSE3=1 -DSDL_DISABLE_SSE4_1=1 -DSDL_DISABLE_SSE4_2=1 \
                -DSDL_DISABLE_AVX=1 -DSDL_DISABLE_AVX2=1 -DSDL_DISABLE_AVX512F=1

CMAKE_COMMON := \
    -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$(SYSROOT) \
    -DCMAKE_PREFIX_PATH=$(SYSROOT) \
    -DCMAKE_FIND_ROOT_PATH=$(SYSROOT) \
    -DCMAKE_C_FLAGS="$(NOSIMD_FLAGS)" \
    -DCMAKE_CXX_FLAGS="$(NOSIMD_FLAGS)" \
    -DBUILD_SHARED_LIBS=OFF
# CMAKE_FIND_ROOT_PATH=$(SYSROOT) is pre-populated so the DJGPP toolchain file's
# `list(APPEND CMAKE_FIND_ROOT_PATH ${CC_ROOTS})` keeps both — needed because
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
	@echo "  make dpmi-lfn-smoke              Phase 8 prereq: DPMI LFN propagation probe"
	@echo
	@echo "Deploy:"
	@echo "  make dist                        dist/doskutsu-cf.zip (CF-ready bundle)"
	@echo "  make dist-list                   dry-run: print dist manifest, no staging"
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
sdl3-mixer: $(SYSROOT)/lib/libSDL3_mixer.a

# NOSIMD flag train moved to CMAKE_COMMON (project-wide) per team-lead — every
# SDL3 consumer on DJGPP needs the same defines. See the NOSIMD_FLAGS block
# at the top of this file for the full rationale. Per-stage CMAKE_C_FLAGS
# overrides removed; they'd shadow the CMAKE_COMMON value.

$(SYSROOT)/lib/libSDL3_mixer.a: $(SYSROOT)/lib/libSDL3.a
	@test -d "$(MIXER_SRC)" || (echo "error: $(MIXER_SRC) not present — run scripts/fetch-sources.sh" >&2; exit 1)
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
# sdl3-mixer — the SDL3 PUBLIC SDL_intrin.h enables SDL_SSE_INTRINSICS for
# any gcc>=4.9 regardless of target CPU, which would otherwise enable code
# paths that fail on P54C-class hardware. SDL3_image kept the IMG_* prefix
# from SDL2_image (signature drift, not the architectural redesign that
# SDL3_mixer underwent) — see software-architect's note on #28.

SDL3_IMAGE_BUILD := $(BUILD_DIR)/sdl3-image
# NOSIMD flag train inherited from CMAKE_COMMON. See top-of-file NOSIMD_FLAGS.

.PHONY: sdl3-image
sdl3-image: $(SYSROOT)/lib/libSDL3_image.a

$(SYSROOT)/lib/libSDL3_image.a: $(SYSROOT)/lib/libSDL3.a
	@test -d "$(IMAGE_SRC)" || (echo "error: $(IMAGE_SRC) not present — run scripts/fetch-sources.sh" >&2; exit 1)
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
nxengine: $(BUILD_DIR)/doskutsu.exe

$(BUILD_DIR)/doskutsu.exe: $(SYSROOT)/lib/libSDL3_mixer.a $(SYSROOT)/lib/libSDL3_image.a
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

# --- Phase 8 prerequisite: DPMI LFN propagation probe (task #20) ------------
#
# Builds tests/dpmi-lfn-smoke/probe.c — tiny DJGPP DOS exe that issues
# INT 21h function 716Ch (LFN Extended Open/Create) for a long-named test
# fixture (wavetable.dat, 9-char base, breaks 8.3) via three different paths:
#   1. open() with 8.3 name           — control, always passes
#   2. open() after _use_lfn(1)       — DJGPP libc LFN path
#   3. __dpmi_int(0x21, AX=716Ch)     — raw DPMI, isolates the libc question
#
# Answers the gating Phase 8 question (docs/PHASE8-LFN-DECISION.md): does
# CWSDPMI's INT 21h reflector pass LFN-family calls (function codes
# 7140h-71A8h) to a real-mode TSR loaded under MS-DOS 6.22? Run on dev host
# under DOSBox-X (lfn=true baseline); the actual answer comes from running
# probe.exe on g2k with LFNDOS.COM (or DOSLFN.COM) loaded. See the runner
# script header + tests/dpmi-lfn-smoke/README.md for the decision tree.
#
# 8.3 DOS filename: basename "probe" + ".exe" — fits.
# minstack=256k: probe is tiny, default DPMI stack is plenty.

DPMI_LFN_SMOKE_DIR := $(BUILD_DIR)/dpmi-lfn-smoke
DPMI_LFN_SMOKE_SRC := tests/dpmi-lfn-smoke/probe.c
DPMI_LFN_SMOKE_EXE := $(DPMI_LFN_SMOKE_DIR)/probe.exe

$(DPMI_LFN_SMOKE_EXE): $(DPMI_LFN_SMOKE_SRC) | djgpp-check
	@mkdir -p $(DPMI_LFN_SMOKE_DIR)
	$(CC) -march=i486 -mtune=pentium -O2 -Wall -o $@ $<
	$(STUBEDIT) $@ minstack=256k

.PHONY: dpmi-lfn-smoke
dpmi-lfn-smoke: $(DPMI_LFN_SMOKE_EXE)
	tests/run-dpmi-lfn-smoke.sh

# --- Phase 2d smoke: SDL3 DOS-backend probe -----------------------------------
#
# Builds tests/sdl3-smoke/sdltest.c — our own minimal probe authored against
# public SDL3 APIs — into build/sdl3-smoke/sdltest.exe. Same coverage as
# upstream's testaudioinfo + testdisplayinfo combined (audio driver/device
# enumeration + video driver/display/mode enumeration), but writes via
# printf() to stdout instead of SDL_Log() to stderr.
#
# Why our own probe instead of upstream's tests: SDL_Log goes to stderr
# unconditionally (vendor/SDL/src/SDL_log.c), and neither MS-DOS COMMAND.COM
# nor DOSBox-X's built-in shell support `2>&1` redirection — the headless
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
#   Mix_QuickLoad_RAW (Organya) → MIX_LoadRawAudio
#   Mix_LoadWAV (SFX)           → MIX_LoadAudio_IO from in-memory WAV
#   OGG via stb_vorbis (Remix)  → VORBIS in MIX_GetAudioDecoder list
# See tests/sdl3-mixer-smoke/mixertest.c file header for full rationale.

SDL3_MIXER_SMOKE_DIR := $(BUILD_DIR)/sdl3-mixer-smoke
SDL3_MIXER_SMOKE_SRC := tests/sdl3-mixer-smoke/mixertest.c
# 8.3 DOS filename: basename "mixsmk" (6) + ".exe" (4) — RUN.BAT references it
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
# 8.3 DOS filename — basename "imgsmk" (6) + ".exe" (4); see sdl3-mixer-smoke
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

# --- Distribution -------------------------------------------------------------
#
# make dist        produces dist/doskutsu-cf.zip with the legal-complete payload
# make dist-list   prints the manifest of what dist would package, without
#                  building the binary or staging files — for sanity-checking
#                  the bundle composition against PLAN.md § Licensing
# make install     copies the same payload to a mounted CF card ($CF required)
#
# PAYLOAD (matches PLAN.md § Licensing § Downstream redistribution checklist):
#   DOSKUTSU.EXE       the binary
#   CWSDPMI.EXE        DPMI host
#   CWSDPMI.DOC        CWSDPMI redistribution terms (required by its license)
#   LICENSE.TXT        this repo's MIT license
#   GPLV3.TXT          NXEngine-evo's GPLv3 (dominant license of the binary)
#   THIRD-PARTY.TXT    attribution matrix (CRLF normalized)
#   README.TXT         DOS-readable quick-start + asset-extraction pointer
#   DATA/...           NXEngine-evo bundled engine data — fonts, baseline
#                      .pbm backgrounds, sprite metadata, JSON configs,
#                      tilekey.dat, StageMeta/, endpic/. Cloned verbatim
#                      from vendor/nxengine-evo/data/ — GPLv3-inherited.
#
# Cave Story freeware game data (maps, NPC sprites, .org music, .pxt SFX,
# wavetable.dat, stage.dat) is **NEVER** in this zip — those come from the
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

# Engine-bundled data tree — cloned verbatim into the zip's DATA/ subdir.
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

# --- dist-list — manifest dry-run ---------------------------------------------
#
# Prints what `make dist` would package, without building doskutsu.exe or
# staging files. Used to sanity-check the bundle composition against
# PLAN.md § Licensing § Downstream redistribution checklist before cutting
# a release. Sources that don't exist (e.g. vendor tree not cloned, binary
# not built) are flagged "[MISSING]" but do not fail the target — this is
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
	@# patches/<name>/ alpha-suffix numbering issue — keep dry-run output
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
dist: $(BUILD_DIR)/doskutsu.exe
	@test -f "$(CWSDPMI_EXE)"   || (echo "error: $(CWSDPMI_EXE) missing — see vendor/cwsdpmi/README.md" >&2; exit 1)
	@test -f "$(CWSDPMI_DOC)"   || (echo "error: $(CWSDPMI_DOC) missing" >&2; exit 1)
	@test -f "$(NX_LICENSE)"    || (echo "error: $(NX_LICENSE) missing — run scripts/fetch-sources.sh" >&2; exit 1)
	@test -d "$(NX_DATA_SRC)"   || (echo "error: $(NX_DATA_SRC) missing — run scripts/fetch-sources.sh" >&2; exit 1)
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
	@# Engine-bundled data tree → DATA/ in the zip. cp -R preserves the
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
# `make stage` produces $(BUILD_DIR)/stage/ — the DOS-side runtime layout
# (DOSKUTSU.EXE + CWSDPMI.EXE + DATA/) — which is what tools/dosbox-launch.sh
# mounts as C: when invoked with `--stage`. NXEngine-evo's ResourceManager
# resolves data via SDL_GetBasePath() + "data/" on DOS, so the .exe and the
# data tree must be co-located at runtime; the repo layout (build/doskutsu.exe
# + data/ at repo root) doesn't satisfy that on its own.
#
# data/ is symlinked rather than copied — fast iteration, no rsync churn, and
# DOSBox-X's host-mount layer follows the symlink transparently. The symlink
# is recreated each run to track repo-side data/ updates without stale-link
# guards.

STAGE_DIR := $(BUILD_DIR)/stage

.PHONY: stage
stage: $(BUILD_DIR)/doskutsu.exe
	@test -f "$(CWSDPMI_EXE)" || (echo "error: $(CWSDPMI_EXE) missing — see vendor/cwsdpmi/README.md" >&2; exit 1)
	@mkdir -p "$(STAGE_DIR)"
	@install -m 0644 $(BUILD_DIR)/doskutsu.exe "$(STAGE_DIR)/DOSKUTSU.EXE"
	@install -m 0644 $(CWSDPMI_EXE)            "$(STAGE_DIR)/CWSDPMI.EXE"
	@if [ -d "$(REPO_ROOT)/data" ]; then \
	    rm -f "$(STAGE_DIR)/data" "$(STAGE_DIR)/DATA"; \
	    ln -s "$(REPO_ROOT)/data" "$(STAGE_DIR)/data"; \
	    echo "staged $(STAGE_DIR)/ (data/ symlinked from repo)"; \
	else \
	    echo "note: data/ not present at repo root — see docs/ASSETS.md"; \
	    echo "      $(STAGE_DIR)/ contains only DOSKUTSU.EXE + CWSDPMI.EXE"; \
	fi

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
	@if [ -d "$(REPO_ROOT)/data" ]; then \
	    echo "copying data tree to $(CF)/DOSKUTSU/DATA/"; \
	    mkdir -p "$(CF)/DOSKUTSU/DATA"; \
	    cp -r "$(REPO_ROOT)/data/"* "$(CF)/DOSKUTSU/DATA/"; \
	    rm -f "$(CF)/DOSKUTSU/DATA/"bk*480fix.pbm; \
	    echo "  (excluded bk*480fix.pbm — dead code on DOS per patch 0005)"; \
	else \
	    echo "note: data/ not present — see docs/ASSETS.md for extraction"; \
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
