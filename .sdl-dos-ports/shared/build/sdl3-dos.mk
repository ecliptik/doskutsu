# shared/build/sdl3-dos.mk -- includable Makefile fragment for the
# SDL3 / SDL3_mixer / SDL3_image cross-build stages, common to every port.
#
# Extracted from doskutsu's Makefile (which interleaves these three generic
# stages with ~150 project-specific probe/QA targets in one file). This
# fragment covers ONLY the generic stages; a port's own Makefile defines its
# own final "game" stage (linking its engine against the sysroot these
# stages populate) and includes this fragment for the rest:
#
#   include .sdl-dos-ports/shared/build/sdl3-dos.mk
#
#   game: verify-patches-applied $(BUILD_DIR)/<game>.exe
#   $(BUILD_DIR)/<game>.exe: $(SYSROOT)/lib/libSDL3.a $(SYSROOT)/lib/libSDL3_mixer.a $(SYSROOT)/lib/libSDL3_image.a ...
#       cmake -S $(ENGINE_SRC) -B $(BUILD_DIR)/<engine> $(CMAKE_COMMON) ...
#       cmake --build $(BUILD_DIR)/<engine> -j$(NPROC)
#       ...
#
# A consuming Makefile should set these before the `include` line (all have
# working defaults if unset, but PORT_NAME should always be set):
#
#   PORT_NAME     short name embedded in the SDL_REVISION string, e.g. "meritous"
#   REPO_ROOT     the PORT repo's root (defaults to $(abspath .))
#   HUB_DIR       path to the sdl-dos-ports submodule (defaults to .sdl-dos-ports)
#   DJGPP_PREFIX  override to use a system DJGPP install instead of tools/djgpp

PORT_NAME    ?= dos-port
REPO_ROOT    ?= $(abspath .)
HUB_DIR      ?= $(REPO_ROOT)/.sdl-dos-ports

# --- Toolchain ----------------------------------------------------------------
#
# Two dirs are needed from the DJGPP install:
#   bin/                      cross-gcc, g++, ld, ar
#   i586-pc-msdosdjgpp/bin/   target-side utilities (stubedit, stubify, exe2coff)
#
# tools/djgpp is expected to be a symlink to a shared DJGPP install, created
# by $(HUB_DIR)/shared/scripts/setup-symlinks.sh. Set DJGPP_PREFIX=/path to
# use a system DJGPP install instead.

DJGPP_ROOT   := $(if $(DJGPP_PREFIX),$(DJGPP_PREFIX),$(REPO_ROOT)/tools/djgpp)
DJGPP_BIN    := $(DJGPP_ROOT)/bin
DJGPP_TBIN   := $(DJGPP_ROOT)/i586-pc-msdosdjgpp/bin

export PATH := $(DJGPP_BIN):$(DJGPP_TBIN):$(PATH)

CC       := i586-pc-msdosdjgpp-gcc
CXX      := i586-pc-msdosdjgpp-g++
STUBEDIT := stubedit

# CMake toolchain file lives inside the SDL3 tree (added by the DOS backend
# PR). It's the canonical DJGPP CMake toolchain; SDL3_mixer, SDL3_image, and
# a port's own engine stage all use the same one.
TOOLCHAIN_FILE := $(REPO_ROOT)/vendor/SDL/build-scripts/i586-pc-msdosdjgpp.cmake

# --- Directories --------------------------------------------------------------

BUILD_DIR    ?= $(REPO_ROOT)/build
SYSROOT      := $(BUILD_DIR)/sysroot

SDL3_BUILD       := $(BUILD_DIR)/sdl3
SDL3_MIXER_BUILD := $(BUILD_DIR)/sdl3-mixer
SDL3_IMAGE_BUILD := $(BUILD_DIR)/sdl3-image

# Vendor trees. A port pins these itself via its own vendor/sources.manifest
# -- shared/ carries patches, not vendored source (see docs/architecture.md
# in the hub).
VENDOR_DIR   := $(REPO_ROOT)/vendor
SDL3_SRC     := $(VENDOR_DIR)/SDL
MIXER_SRC    := $(VENDOR_DIR)/SDL_mixer
IMAGE_SRC    := $(VENDOR_DIR)/SDL_image

# --- NOSIMD flag train ---------------------------------------------------------
#
# SDL3's PUBLIC SDL_intrin.h enables SDL_SSE_INTRINSICS for any gcc>=4.9
# because the compiler *supports* `__attribute__((target("sse")))` -- even
# though a P54C/486-class DOS target has no SSE. SDL3 itself sets
# SDL_DISABLE_SSE=1 in its INTERNAL build_config.h so its own code is fine,
# but downstream consumers (SDL3_mixer, SDL3_image, and a port's engine)
# compile without that internal config and pick up the SSE intrinsic paths,
# which then emit a runtime check that fails on Pentium-class hardware
# (e.g. "MIX_Init: Need SSE instructions but this CPU doesn't offer it").
# Forwarding these defines through CMAKE_C_FLAGS/CMAKE_CXX_FLAGS suppresses
# the intrinsic gate at every consumer's preprocessor level. A port's own
# engine stage MUST also pass NOSIMD_FLAGS (via CMAKE_COMMON, below) or it
# will hit the same runtime check.
NOSIMD_FLAGS := -DSDL_DISABLE_MMX=1 -DSDL_DISABLE_SSE=1 -DSDL_DISABLE_SSE2=1 \
                -DSDL_DISABLE_SSE3=1 -DSDL_DISABLE_SSE4_1=1 -DSDL_DISABLE_SSE4_2=1 \
                -DSDL_DISABLE_AVX=1 -DSDL_DISABLE_AVX2=1 -DSDL_DISABLE_AVX512F=1

# --- Common CMake args ----------------------------------------------------------
#
# Every stage uses the DJGPP toolchain file and installs into SYSROOT.
# CMAKE_PREFIX_PATH/CMAKE_FIND_ROOT_PATH make each stage's output visible to
# later stages -- CMAKE_FIND_ROOT_PATH is pre-populated with SYSROOT because
# the toolchain file sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY, which
# restricts find_package() to those paths; without this, later stages
# couldn't find_package(SDL3).
CMAKE_COMMON := \
    -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$(SYSROOT) \
    -DCMAKE_PREFIX_PATH=$(SYSROOT) \
    -DCMAKE_FIND_ROOT_PATH=$(SYSROOT) \
    -DCMAKE_C_FLAGS="$(NOSIMD_FLAGS)" \
    -DCMAKE_CXX_FLAGS="$(NOSIMD_FLAGS)" \
    -DBUILD_SHARED_LIBS=OFF

NPROC := $(shell nproc 2>/dev/null || echo 4)

# --- Diagnostics ----------------------------------------------------------------

.PHONY: djgpp-check
djgpp-check:
	@if [ ! -L "$(DJGPP_ROOT)" ] && [ ! -d "$(DJGPP_ROOT)" ]; then \
	    echo "error: $(DJGPP_ROOT) does not exist. Run $(HUB_DIR)/shared/scripts/setup-symlinks.sh." >&2; \
	    exit 1; \
	fi
	@if ! command -v $(CC) >/dev/null 2>&1; then \
	    echo "error: $(CC) not found on PATH." >&2; \
	    echo "       Tried $(DJGPP_BIN)" >&2; \
	    exit 1; \
	fi
	@$(CC) --version | head -n1
	@echo "DJGPP ready."

# --- Patch orchestration (this port's own vendor + this shared patch set) -----
#
# Uses $(HUB_DIR)/shared/scripts/{fetch-sources.sh,apply-patches.sh,
# verify-patches-applied.sh}, driven by this port's own vendor/sources.manifest
# and patches/ directory. scripts/new-port.sh sets patches/SDL and
# patches/SDL_mixer up as symlinks into $(HUB_DIR)/shared/patches/sdl3-dos/
# and shared/patches/sdl3-mixer/, so apply-patches.sh (which only knows
# about this port's own patches/<name>/) picks up the shared series with no
# special-casing. patches/<engine>/ is a real directory this port owns.

.PHONY: sources patches verify-patches-applied
sources:
	@$(HUB_DIR)/shared/scripts/fetch-sources.sh

patches:
	@$(HUB_DIR)/shared/scripts/apply-patches.sh

verify-patches-applied:
	@$(HUB_DIR)/shared/scripts/verify-patches-applied.sh

MANIFEST_FILE      := $(VENDOR_DIR)/sources.manifest
SDL3_PATCHES       := $(wildcard $(HUB_DIR)/shared/patches/sdl3-dos/*.patch)
SDL3_MIXER_PATCHES := $(wildcard $(HUB_DIR)/shared/patches/sdl3-mixer/*.patch)
SDL3_IMAGE_PATCHES :=

# --- Stage 1: SDL3 ---------------------------------------------------------------

.PHONY: sdl3
sdl3: verify-patches-applied $(SYSROOT)/lib/libSDL3.a

# Pin SDL_REVISION to a deterministic string (not `git describe`, which
# embeds a HEAD commit hash that changes every time patches are re-applied
# via `git am`, making the embedded revision non-reproducible across
# rebuilds of identical source). Doskutsu's own build additionally embeds a
# content-based build-sha fingerprint (see its Makefile's RUNMANIFEST_FLAGS
# / DOS_PORT_BUILD_SHA12) -- that's a doskutsu-specific diagnostic feature,
# not part of this shared fragment; a port wanting the same can add its own
# equivalent following that pattern.
SDL_REVISION_PIN := SDL3-DOS+$(PORT_NAME)

$(SYSROOT)/lib/libSDL3.a: $(SDL3_PATCHES) $(MANIFEST_FILE) | djgpp-check
	@test -d "$(SDL3_SRC)" || (echo "error: $(SDL3_SRC) not present -- run make sources" >&2; exit 1)
	@test -f "$(TOOLCHAIN_FILE)" || (echo "error: $(TOOLCHAIN_FILE) not found -- is this SDL checkout post-DOS-backend?" >&2; exit 1)
	# SDL_TESTS=OFF -- skip SDL3's upstream test executables, which link
	# libSDL3.a alone. If any patch adds an SDL<->engine cross-link-unit
	# symbol (an extern the engine defines), those test exes won't resolve
	# it and the build halts here before the game binary is built. Nothing
	# ships or runs those test exes.
	cmake -S $(SDL3_SRC) -B $(SDL3_BUILD) $(CMAKE_COMMON) \
	    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
	    -DSDL_TESTS=OFF \
	    -DSDL_REVISION="$(SDL_REVISION_PIN)"
	cmake --build $(SDL3_BUILD) -j$(NPROC)
	cmake --install $(SDL3_BUILD)

# --- Stage 2: SDL3_mixer ----------------------------------------------------------
#
# WAV (native) + OGG-via-stb_vorbis only. All other codecs OFF;
# SDLMIXER_DEPS_SHARED=OFF disables dynamic codec loading (DJGPP has no
# real dlopen). A port needing a different codec mix edits this stanza (or
# overrides it in its own Makefile after the include).

.PHONY: sdl3-mixer
sdl3-mixer: verify-patches-applied $(SYSROOT)/lib/libSDL3_mixer.a

$(SYSROOT)/lib/libSDL3_mixer.a: $(SYSROOT)/lib/libSDL3.a $(SDL3_MIXER_PATCHES)
	@test -d "$(MIXER_SRC)" || (echo "error: $(MIXER_SRC) not present -- run make sources" >&2; exit 1)
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

# --- Stage 3: SDL3_image ----------------------------------------------------------
#
# PNG-via-stb_image only. All other codecs OFF; SDLIMAGE_DEPS_SHARED=OFF
# disables the SDL_LoadObject codec loader path. A port needing a different
# format mix edits this stanza (or overrides it after the include).

.PHONY: sdl3-image
sdl3-image: verify-patches-applied $(SYSROOT)/lib/libSDL3_image.a

$(SYSROOT)/lib/libSDL3_image.a: $(SYSROOT)/lib/libSDL3.a $(SDL3_IMAGE_PATCHES)
	@test -d "$(IMAGE_SRC)" || (echo "error: $(IMAGE_SRC) not present -- run make sources" >&2; exit 1)
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

.PHONY: sdl3-dos-clean
sdl3-dos-clean:
	rm -rf $(SDL3_BUILD) $(SDL3_MIXER_BUILD) $(SDL3_IMAGE_BUILD) $(SYSROOT)
