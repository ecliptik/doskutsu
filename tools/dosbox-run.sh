#!/usr/bin/env bash
# dosbox-run.sh — run a DOS executable under DOSBox-X and capture its stdout.
#
# Headless mode (the common case): stages the exe + any --include files into
# a temp C:\, writes a RUN.BAT that invokes the exe with stdout redirected to
# STDOUT.TXT, runs DOSBox-X with `-silent -exit`, and copies STDOUT.TXT out.
#
# Typical use:
#   tools/dosbox-run.sh --exe build/hello.exe --stdout /tmp/hello.out
#   tools/dosbox-run.sh --exe build/hello.exe --fast
#
# Interactive mode (opens the window — prefer dosbox-launch.sh for playtest):
#   tools/dosbox-run.sh --exe build/hello.exe --interactive
#
# DOS has no LFN support. Filenames passed via --include are staged to C:\
# with uppercased basenames; the exe is also uppercased. If you need the
# exe to reference other files, they must be 8.3 and on C:\.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF_PARITY="${SCRIPT_DIR}/dosbox-x.conf"
CONF_FAST="${SCRIPT_DIR}/dosbox-x-fast.conf"

EXE=""
ARGS=""
STDOUT_PATH=""
INTERACTIVE=0
INCLUDES=()
KEEP_STAGE=0
FAST=0
# --merge-stderr: capture stderr too (e.g. SDL_Log goes there). DOSBox-X / DOS shells
# don't always honor `2>&1` cleanly — prefer printf-to-stdout in tests where possible.
MERGE_STDERR=0
CWSDPMI="${CWSDPMI:-$SCRIPT_DIR/../vendor/cwsdpmi/cwsdpmi.exe}"

usage() {
  cat <<'USAGE'
Usage: dosbox-run.sh --exe PATH [--args "..."] [--stdout PATH] [--include PATH]...
                     [--fast] [--interactive] [--keep-stage] [--merge-stderr]

  --exe PATH         DOS executable to run (required). Basename is placed at C:\.
  --args "..."       Arguments passed to the exe (quoted as a single string).
  --stdout PATH      Where to copy captured stdout. Default: stream to host stdout.
  --include PATH     Additional file to stage into C:\ (repeatable). If CWSDPMI
                     is present at vendor/cwsdpmi/cwsdpmi.exe it is auto-included.
  --fast             Use dosbox-x-fast.conf (cycles=max) instead of parity config.
  --interactive      Open the DOSBox-X window instead of running headless.
  --keep-stage       Leave the staging temp dir on exit (useful for debugging).
  --merge-stderr     Capture both stdout and stderr (>STDOUT.TXT 2>&1). Needed for
                     programs that log to stderr (e.g. SDL_Log on DJGPP). DJGPP's
                     runtime processes 2>&1 itself so the syntax works under DOS.
  -h, --help         Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exe)         EXE="$2"; shift 2 ;;
    --args)        ARGS="$2"; shift 2 ;;
    --stdout)      STDOUT_PATH="$2"; shift 2 ;;
    --include)     INCLUDES+=("$2"); shift 2 ;;
    --fast)        FAST=1; shift ;;
    --interactive) INTERACTIVE=1; shift ;;
    --keep-stage)  KEEP_STAGE=1; shift ;;
    --merge-stderr) MERGE_STDERR=1; shift ;;
    -h|--help)     usage; exit 0 ;;
    *) echo "dosbox-run.sh: unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ "$FAST" == "1" ]]; then CONF="$CONF_FAST"; else CONF="$CONF_PARITY"; fi

if [[ -z "$EXE" ]]; then
  echo "dosbox-run.sh: --exe is required" >&2
  usage; exit 2
fi
if [[ ! -f "$EXE" ]]; then
  echo "dosbox-run.sh: exe not found: $EXE" >&2
  exit 2
fi
if [[ ! -f "$CONF" ]]; then
  echo "dosbox-run.sh: conf not found: $CONF" >&2
  exit 2
fi

# Stage ---------------------------------------------------------------------
STAGE="$(mktemp -d -t doskutsu-dosbox.XXXXXX)"
cleanup() {
  if [[ "$KEEP_STAGE" == "1" ]]; then
    echo "dosbox-run.sh: stage kept at $STAGE" >&2
  else
    rm -rf "$STAGE"
  fi
}
trap cleanup EXIT

cp "$EXE" "$STAGE/"
EXE_BASENAME="$(basename "$EXE")"
EXE_DOSNAME="${EXE_BASENAME^^}"

# Auto-include cwsdpmi.exe if vendored. DJGPP binaries need it; the hello
# smoke test doesn't strictly, but having it in the stage costs nothing.
if [[ -f "$CWSDPMI" && ! " ${INCLUDES[*]:-} " =~ [[:space:]]"$CWSDPMI"[[:space:]] ]]; then
  INCLUDES+=("$CWSDPMI")
fi

for f in "${INCLUDES[@]:-}"; do
  [[ -z "$f" ]] && continue
  if [[ ! -f "$f" ]]; then
    echo "dosbox-run.sh: --include not found: $f" >&2
    exit 2
  fi
  # DOS uppercases at runtime; uppercase here so the RUN.BAT references match.
  dest_name="$(basename "$f" | tr '[:lower:]' '[:upper:]')"
  cp "$f" "$STAGE/$dest_name"
done

# RUN.BAT — invokes the exe with stdout captured to STDOUT.TXT.
# With --merge-stderr we add `2>&1` so SDL_Log (and any stderr writer) is also
# captured. DJGPP's runtime parses argv-level redirection itself, so 2>&1 works
# under DOS even though MS-DOS COMMAND.COM proper doesn't support it natively.
if [[ "$MERGE_STDERR" == "1" ]]; then
  REDIR='> STDOUT.TXT 2>&1'
else
  REDIR='> STDOUT.TXT'
fi
case "${EXE_DOSNAME##*.}" in
  BAT|bat)
    INVOKE_LINE="$(printf 'COMMAND /C %s %s %s\r\n' "$EXE_DOSNAME" "$ARGS" "$REDIR")"
    ;;
  *)
    INVOKE_LINE="$(printf '%s %s %s\r\n' "$EXE_DOSNAME" "$ARGS" "$REDIR")"
    ;;
esac
{
  printf '@ECHO OFF\r\n'
  printf 'SET BLASTER=A220 I5 D1 H5 T6\r\n'
  # SDL_DOS_AUDIO_SB_SKIP_DETECTION — escape hatch from patches/SDL/0001.
  # DOSBox-X's emulated SB16 returns 0xFF on the DSP detection read regardless
  # of timing tuning, so SDL3-DOS audio init fails without this in the emulator.
  # Real hardware (g2k Phase 8) MUST NOT set this — would mask Vibra16S regressions.
  # See tools/dosbox-launch.sh for the matching parity-config injection.
  printf 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1\r\n'
  printf '%s' "$INVOKE_LINE"
} > "$STAGE/RUN.BAT"

# Invoke DOSBox-X -----------------------------------------------------------
DBX_ARGS=(-conf "$CONF" -nopromptfolder
          -c "MOUNT C $STAGE"
          -c "C:"
          -c "CALL RUN.BAT")

if [[ "$INTERACTIVE" == "1" ]]; then
  # Visible run — user drives; don't auto-exit.
  export DISPLAY="${DOSBOX_DISPLAY:-:0}"
  dosbox-x "${DBX_ARGS[@]}"
else
  DBX_ARGS+=(-c "EXIT" -silent -exit -nogui -nomenu)
  dosbox-x "${DBX_ARGS[@]}" >/dev/null 2>&1 || {
    rc=$?
    echo "dosbox-run.sh: dosbox-x exited non-zero ($rc)" >&2
    exit $rc
  }
fi

# Deliver captured stdout ---------------------------------------------------
OUT="$STAGE/STDOUT.TXT"
if [[ -f "$OUT" ]]; then
  if [[ -n "$STDOUT_PATH" ]]; then
    cp "$OUT" "$STDOUT_PATH"
  else
    cat "$OUT"
  fi
else
  echo "dosbox-run.sh: no STDOUT.TXT produced (exe may have crashed under DPMI)" >&2
  exit 3
fi
