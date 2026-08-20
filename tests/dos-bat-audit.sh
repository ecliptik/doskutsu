#!/usr/bin/env bash
# dos-bat-audit.sh -- comprehensive DOS pre-ship audit for an iter package.
#
# Runs on the EXTRACTED tarball (NOT the staging dir -- a host linter can
# re-flip "> NUL" -> "> /dev/null" in staging after the tar is built; the
# tarball is the immutable shipped artifact). Covers every DOS bug class the
# S3-VIRGE campaign hit, plus the standing CRLF/ASCII/8.3 gates. Permanent
# reusable gate: run on every iter package before sign-off.
#
# Usage:
#   tests/dos-bat-audit.sh <package.tar.gz> [install-script.sh]
#     <package.tar.gz>     the iter/runner tarball (extracted + audited)
#     [install-script.sh]  optional: also bash -n it + cross-check its
#                          TARBALL_SHA + SHA_* constants against the tarball
#
# Exit: 0 = all gates PASS; 1 = one or more FAIL; 2 = invocation error.
#
# Bug classes (campaign-derived):
#   1 REDIRECTS      no "/dev/null" in any BAT (NUL->/dev/null linter bug)
#   2 EXT COMMANDS   every non-internal command is bundled / full-path
#                    (the FIND + MODE "Bad command or file name" bugs)
#   3 LOG PATHS      report each BAT's produced logs + the logback's pulls
#                    for cross-check (the HWINV C:\ capture bug)
#   4 FILE EXISTENCE every file a BAT references is bundled or a known prereq
#   5 STANDING       CRLF, ASCII, 8.3 names, no <>/redirect in REM/ECHO,
#                    LOG_TAG<=5
#   6 SCRIPTS        install/logback bash -n + extract-vs-constants sha

set -uo pipefail

TARBALL="${1:-}"
INSTALL="${2:-}"
[[ -f "$TARBALL" ]] || { echo "usage: $0 <package.tar.gz> [install.sh]" >&2; exit 2; }

# COMMAND.COM internals (no external file needed) + the shell itself.
INTERNALS="ECHO IF REM SET GOTO CALL COPY DEL REN RENAME CD CHDIR MD MKDIR RD RMDIR CLS TYPE PATH PROMPT PAUSE EXIT VER VOL DATE TIME DIR BREAK VERIFY COMMAND COMMAND.COM"
# Known-present g2k prereqs (documented, not bundled). Empty by default --
# the campaign convention is "bundle it or full-path it"; add here only with
# an explicit operator-confirmed-present justification.
# pgusinit: PicoGUS mode-switch utility, resident on the g2k CF and used by
# every sweep that changes card mode. Operator-confirmed present.
KNOWN_PRESENT="PGUSINIT"

stage="$(mktemp -d /tmp/dos-audit.XXXXXX)"
trap 'rm -rf "$stage"' EXIT
tar -xzf "$TARBALL" -C "$stage" || { echo "FAIL: cannot extract $TARBALL" >&2; exit 2; }

mapfile -t BATS < <(find "$stage" -iname '*.BAT' | sort)
mapfile -t BUNDLED < <(find "$stage" -type f -printf '%f\n' | tr '[:lower:]' '[:upper:]' | sort -u)

is_in() { local n="$1"; shift; local x; for x in "$@"; do [[ "$x" == "$n" ]] && return 0; done; return 1; }
is_bundled() { local f; for f in "${BUNDLED[@]}"; do [[ "$f" == "$1" ]] && return 0; done; return 1; }

rc=0
pass(){ printf '  PASS  %s\n' "$*"; }
failr(){ printf '  FAIL  %s\n' "$*" >&2; rc=1; }
note(){ printf '  ..    %s\n' "$*"; }

echo "=== DOS pre-ship audit: $(basename "$TARBALL") ==="
echo "    BATs: ${#BATS[@]}   bundled files: ${#BUNDLED[@]}"

echo "--- [1] REDIRECTS (no /dev/null; NUL-linter bug) ---"
dn=0; for b in "${BATS[@]}"; do c=$(grep -c '/dev/null' "$b" 2>/dev/null || true); c=${c:-0}; [[ "$c" -gt 0 ]] && { failr "$(basename "$b"): $c /dev/null occurrence(s) -- NUL got linter-flipped"; dn=1; }; done
[[ $dn -eq 0 ]] && pass "no /dev/null in any BAT"

echo "--- [2] EXTERNAL COMMANDS (bundled / full-path / internal only) ---"
ext=0
for b in "${BATS[@]}"; do
  # tokenize commands: strip @, REM, labels; split IF-EXIST prefix; split pipes
  while IFS= read -r line; do
    line="${line%$'\r'}"; line="${line#"${line%%[![:space:]]*}"}"   # rstrip CR, lstrip
    [[ -z "$line" ]] && continue
    line="${line#@}"
    up="$(printf '%s' "$line" | tr '[:lower:]' '[:upper:]')"
    case "$up" in REM\ *|REM|:*) continue;; esac
    # split on pipe into command segments
    IFS='|' read -ra segs <<< "$line"
    for seg in "${segs[@]}"; do
      seg="${seg#"${seg%%[![:space:]]*}"}"
      # strip an IF [NOT] EXIST <path> prefix to reach the real command
      while :; do
        u="$(printf '%s' "$seg" | tr '[:lower:]' '[:upper:]')"
        if [[ "$u" == IF\ * ]]; then seg="$(printf '%s' "$seg" | sed -E 's/^[Ii][Ff][[:space:]]+([Nn][Oo][Tt][[:space:]]+)?([Ee][Xx][Ii][Ss][Tt][[:space:]]+[^[:space:]]+[[:space:]]+|[^[:space:]]+==[^[:space:]]+[[:space:]]+|[Ee][Rr][Rr][Oo][Rr][Ll][Ee][Vv][Ee][Ll][[:space:]]+[0-9]+[[:space:]]+)?//')"; continue; fi
        break
      done
      cmd="$(printf '%s' "$seg" | awk '{print $1}')"
      [[ -z "$cmd" ]] && continue
      CMD="$(printf '%s' "$cmd" | tr '[:lower:]' '[:upper:]')"
      case "$CMD" in :*|"") continue;; esac
      # full-path (drive: or backslash) = OK
      [[ "$CMD" == *:* || "$CMD" == \\* ]] && continue
      is_in "$CMD" $INTERNALS && continue
      # internal glued to a "." idiom or .ext: ECHO. (blank line), ECHO.msg,
      # COMMAND.COM, etc. -- strip at the first "." and re-check the internals.
      is_in "${CMD%%.*}" $INTERNALS && continue
      # bare token: must be a bundled file (.EXE/.COM/.BAT) -- try as-is + with .EXE
      if is_bundled "$CMD" || is_bundled "$CMD.EXE" || is_bundled "$CMD.COM" || is_bundled "$CMD.BAT"; then continue; fi
      is_in "$CMD" $KNOWN_PRESENT && { note "$(basename "$b"): '$cmd' = documented-present prereq"; continue; }
      failr "$(basename "$b"): external command '$cmd' is NOT internal, NOT bundled, NOT full-path (Bad-command-or-file-name risk)"
      ext=1
    done
  done < "$b"
done
[[ $ext -eq 0 ]] && pass "all commands internal / bundled / full-path"

echo "--- [3] LOG PATHS (BAT-produced logs vs logback pulls -- cross-check) ---"
note "logs the BATs produce (REN targets + known fixed-name outputs):"
grep -hoE "REN [A-Z0-9._\\]+ [A-Z0-9._]+\.(LOG|OUT)" "${BATS[@]}" 2>/dev/null | awk '{print "      "$3}' | sort -u
grep -hoE "[A-Z0-9_]+\.(LOG|OUT)" "${BATS[@]}" 2>/dev/null | sort -u | sed 's/^/      /'
note "(confirm the logback pulls each of these from CWD/game-dir with C:\\ fallback)"

echo "--- [4] FILE EXISTENCE (referenced files bundled or prereq) ---"
fe=0
refs="$(grep -hoE "[A-Z0-9_]+\.(EXE|GLD|TAS|DAT|COM)" "${BATS[@]}" 2>/dev/null | tr '[:lower:]' '[:upper:]' | sort -u)"
for r in $refs; do
  is_bundled "$r" && continue
  case "$r" in PROFILE.DAT) note "PROFILE.DAT = created at runtime from PROFILE.GLD (COPY) -- OK";; \
               COMMAND.COM) note "COMMAND.COM = DOS shell (always present) -- OK";; \
               PLAY.TAS) is_bundled "PLAY.TAS" && continue; note "PLAY.TAS = TAS/ dir or prereq -- confirm";; \
               FIND.EXE) note "FIND.EXE = MS-DOS 6.22 external command in C:\\DOS, on PATH -- OK";; \
               MODE12.COM) note "MODE12.COM = VGACAP capture tool; every call is guarded by IF EXIST C:\\VGACAP\\ -- OK";; \
               *) failr "referenced file '$r' not bundled (prereq? document or bundle)"; fe=1;; esac
done
[[ $fe -eq 0 ]] && pass "all referenced files bundled or runtime-created/prereq"

echo "--- [5] STANDING gates (CRLF / ASCII / 8.3 / REM-ECHO redirect / LOG_TAG) ---"
g5=0
for b in "${BATS[@]}"; do
  bn="$(basename "$b")"
  file "$b" | grep -q "CRLF line terminators" || { failr "$bn: not CRLF"; g5=1; }
  file "$b" | grep -q "UTF-8" && { failr "$bn: UTF-8 (non-ASCII)"; g5=1; }
  LC_ALL=C grep -qP '[^\x00-\x7F\r]' "$b" && { failr "$bn: non-ASCII byte"; g5=1; }
  base="${bn%.*}"; [[ ${#base} -le 8 ]] || { failr "$bn: base >8 chars (8.3)"; g5=1; }
  # REM and ECHO both hand their line to COMMAND.COM's redirect parser, so a
  # stray > in either creates a file. But `ECHO field=value >> manifest` is the
  # harness manifest mechanism the standard mandates, and flagging every one of
  # those made this gate fire on 19 of 22 conforming BATs -- noise that got the
  # whole audit ignored. Discriminate on the redirect TARGET: a log/manifest
  # sink is intended, anything else (`ECHO a -> b` writes a file called b) is
  # the footgun. A redirect in a REM is never intended.
  while IFS=: read -r ln txt; do
    [[ -z "$ln" ]] && continue
    if [[ "$txt" =~ ^[[:space:]]*[Rr][Ee][Mm] ]]; then
      failr "$bn:$ln: redirect inside REM -- COMMAND.COM executes it"; g5=1; continue
    fi
    rest="$(sed -E 's/[[:space:]]*>>?[[:space:]]*(NUL|%[A-Za-z_][A-Za-z0-9_]*%|[A-Za-z0-9_\\:.%]*\.(NFO|LOG|TMP|OUT|CHK|DAT))[[:space:]]*$//I' <<<"$txt")"
    if [[ "$rest" == *"<"* || "$rest" == *">"* ]]; then
      failr "$bn:$ln: stray < or > outside a log redirect -- creates a file"; g5=1
    fi
  done < <(grep -nE '^[[:space:]]*([Rr][Ee][Mm]|[Ee][Cc][Hh][Oo])\b.*[<>]' "$b" | tr -d '\r')
  while IFS= read -r t; do t="${t%$'\r'}"; t="${t##*=}"; t="${t//%QAM%/Q}"; [[ ${#t} -le 5 ]] || { failr "$bn: LOG_TAG '$t' >5 chars"; g5=1; }; done < <(grep -hE 'SET[[:space:]]+DOSKUTSU_LOG_TAG=' "$b" 2>/dev/null)
done
[[ $g5 -eq 0 ]] && pass "CRLF + ASCII + 8.3 + no REM/ECHO redirect + LOG_TAG<=5"

if [[ -n "$INSTALL" && -f "$INSTALL" ]]; then
  echo "--- [6] install/logback scripts ---"
  bash -n "$INSTALL" && pass "$(basename "$INSTALL") bash -n" || failr "$(basename "$INSTALL") syntax"
  LC_ALL=C grep -qP '[^\x00-\x7F]' "$INSTALL" && failr "$(basename "$INSTALL") non-ASCII" || pass "$(basename "$INSTALL") ASCII"
  ts="$(sha256sum "$TARBALL" | awk '{print $1}')"
  its="$(grep -E '^TARBALL_SHA=' "$INSTALL" | head -1 | sed 's/^TARBALL_SHA=//; s/"//g')"
  [[ "$ts" == "$its" ]] && pass "TARBALL_SHA matches tarball" || failr "TARBALL_SHA mismatch (install=$its tar=$ts)"
  # SHA_* constants vs bundled files (best-effort: match by basename token)
  while IFS= read -r sl; do
    cnst="${sl%%=*}"; want="$(printf '%s' "${sl#*=}" | sed 's/"//g')"
    key="${cnst#SHA_}"   # e.g. S3BLT, GAME, CWSDPMI
    f="$(find "$stage" -iname "*${key}*" -type f | head -1)"
    [[ "$key" == GAME ]] && f="$(find "$stage" -iname 'DOSKUTSU.EXE' | head -1)"
    [[ -z "$f" ]] && continue
    got="$(sha256sum "$f" | awk '{print $1}')"
    [[ "$got" == "$want" ]] && pass "$cnst == $(basename "$f")" || failr "$cnst mismatch vs $(basename "$f") (want $want got $got)"
  done < <(grep -E '^SHA_[A-Z0-9]+=' "$INSTALL")
fi

echo "=== $([ $rc -eq 0 ] && echo 'AUDIT PASS' || echo 'AUDIT FAIL') ==="
exit $rc
