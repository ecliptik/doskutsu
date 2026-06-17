# tools/dosbox-teardown.sh -- conf/PID-scoped DOSBox-X teardown helper (SOURCE, do not exec)
#
# WHY THIS EXISTS:
#   Test scripts historically tore DOSBox-X down with a global `pkill -x dosbox-x`,
#   which kills EVERY dosbox-x process on the machine -- not just the one the script
#   launched. With concurrent workstreams (an operator reviewing a build in one
#   window, a smoke gate running in another, a palette matrix in a third) that global
#   kill is a cross-workstream grenade: it has aborted a live operator review twice
#   this session. See [[dosbox_global_pkill_collides_concurrent_workstreams]].
#
#   dbx_kill_conf scopes the kill to the instance launched with a SPECIFIC -conf file.
#   It finds candidate pids via `pgrep -f` on the conf path, then -- the safety that
#   makes this robust -- kills ONLY pids whose /proc/<pid>/comm is an actual dosbox
#   binary. That comm-filter means it can never self-kill the calling shell (comm=bash)
#   even though the script's own command line contains the conf path (and so matches the
#   pgrep -f), and it never touches another conf's dosbox instance.
#
# USAGE:
#   source "$(dirname "$0")/../tools/dosbox-teardown.sh"   # path relative to the caller
#   dbx_kill_conf "$CONF"          # graceful TERM of the dosbox-x started with $CONF
#   dbx_kill_conf "$CONF" KILL     # force SIGKILL follow-up
#
# Guards: an empty conf arg is a no-op (returns 0) so a teardown in a script that never
# launched dosbox (early exit, or CONF unset) can't degrade into a broad match.

# dbx_kill_conf <conf-path> [signal]
#   signal defaults to TERM; pass KILL (or 9) for the force follow-up.
dbx_kill_conf() {
  local conf="$1"
  local sig="${2:-TERM}"

  # Empty/unset conf: refuse to match (an empty pattern would match broadly). No-op.
  if [[ -z "$conf" ]]; then
    return 0
  fi

  # Candidate pids: any process whose command line carries `-conf <conf>` or `-conf=<conf>`.
  # `-f` matches the full command line; `--` ends option parsing so a conf path starting
  # with `-` can't be read as a pgrep flag.
  local pids
  pids=$(pgrep -f -- "-conf[ =]*${conf}" 2>/dev/null || true)
  [[ -n "$pids" ]] || return 0

  local pid comm
  for pid in $pids; do
    # comm-filter: only signal real dosbox binaries. The calling shell (comm=bash) and any
    # other helper whose argv happens to contain the conf path are skipped. This is what
    # makes the helper safe to call from a script whose own argv matches the pgrep pattern.
    comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
    case "$comm" in
      dosbox-x|dosbox-x-fast|dbxreview|dosbox*)
        kill "-${sig}" "$pid" 2>/dev/null || true
        ;;
    esac
  done
  return 0
}
