#!/usr/bin/env bash
# embed-bats.sh -- regenerate the BATs embedded inside install-qa.sh from the
# tracked sources in this directory, then VERIFY every one round-trips.
#
# Why this exists: the installer carries the BATs as base64 so it works from
# scratch with no companion downloads. That embed is a SNAPSHOT, and a snapshot
# silently goes stale the moment someone edits a source BAT -- which is exactly
# what happened: GAP.BAT was fixed twice while the installer kept shipping the
# version before those fixes, so a card populated from it ran the old sweep and
# reproduced bugs that were already fixed in the repo.
#
# Run this after ANY change to a .BAT here. It is idempotent.
#   bash tests/qa/embed-bats.sh [path/to/install-qa.sh]
set -eu
cd "$(dirname "$0")"
TARGET="${1:-install-qa.sh}"
[ -f "$TARGET" ] || { echo "no installer at $TARGET" >&2; exit 1; }

python3 - "$TARGET" <<'PY'
import re, sys, base64
t = sys.argv[1]
s = open(t).read()
changed = []
for name in ("PROVE","RB","GAP","EAR","QA"):
    src = open("%s.BAT" % name, "rb").read()
    b64 = base64.b64encode(src).decode()
    wrapped = "\n".join(b64[i:i+76] for i in range(0, len(b64), 76))
    pat = re.compile(r"(_B64_%s=')\n.*?\n(')" % name, re.S)
    if not pat.search(s):
        print("  WARN %s.BAT is not embedded in the installer" % name); continue
    new = pat.sub(lambda m: m.group(1) + "\n" + wrapped + "\n" + m.group(2), s, count=1)
    if new != s:
        changed.append(name)
    s = new
open(t, "w").write(s)
print("  refreshed:", " ".join(changed) if changed else "(nothing changed)")
PY

echo "  verifying round-trip..."
python3 - "$TARGET" <<'PY'
import re, sys, base64
t = sys.argv[1]
s = open(t).read(); bad = 0
for name in ("PROVE","RB","GAP","EAR","QA"):
    m = re.search(r"_B64_%s='\n(.*?)\n'\n" % name, s, re.S)
    if not m:
        continue
    got = base64.b64decode(m.group(1).replace("\n", ""))
    want = open("%s.BAT" % name, "rb").read()
    ok = got == want
    print("    %-6s %5d bytes  %s" % (name, len(got), "ok" if ok else "*** MISMATCH ***"))
    bad += 0 if ok else 1
sys.exit(1 if bad else 0)
PY
echo "  all embedded BATs match their sources"
