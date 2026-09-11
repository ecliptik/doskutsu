#!/usr/bin/env python3
"""score-pump -- score a paired-arm PUMP run against a banked one.

NOT GENERIC HARNESS TOOLING. This is a one-off analysis script for a single
already-completed doskutsu Pi5-migration benchmarking campaign: the cell
tags, the baseline directory, and the pass bands below are all specific to
that one experiment's own pre-registered profile (vcctrl's
docs/lab/PI5-MIGRATION.md sec. 6a), not reusable defaults. Kept here as the
record of exactly how that campaign's numbers were scored, not as a template
for a new one -- a new benchmarking campaign needs its own script with its
own pre-registered bands, per vcctrl's docs/HARNESS-STANDARD.md.

WRITTEN BEFORE THE RUN'S NUMBERS EXISTED. That is the point of it. The bands
below come from the benchmarking session's profile sec. 3.2b and are recorded
in vcctrl's docs/lab/PI5-MIGRATION.md sec. 6a; a threshold chosen after
seeing the result is a threshold that agrees with whatever you were hoping
for.

WHAT IS COMPARED, AND WHAT IS DELIBERATELY NOT. Not absolute fps. The same
configuration measured a day apart gave 30.4 and 29.6 against a within-session
band of 0.2, so an across-session fps comparison has an effective band several
times wider than anything a hardware change could plausibly move. Two
within-session quantities are used instead:

  * PAIR SPREAD -- the two control cells against each other, and the two arm
    cells against each other. A harness that has added timing variance widens
    these. Threshold 0.2, declared by the profile before this test existed.

  * ARM DELTA -- mean(arm) - mean(control). Robust to a session confound
    whether it is additive (cancels exactly) or multiplicative (2.8% of 1.30
    is 0.04, an order of magnitude inside the band).

THREE STATES, NOT TWO, and the middle one is load-bearing. A pair spread is a
2-sample range and therefore a poor variance estimator: at sigma 0.10 the
difference of two cells has SD 0.14, so ONE pair exceeds 0.2 by chance about
one time in six with nothing wrong. With two pairs per run that is a one-in-
three event on healthy hardware. Reading one wide pair as failure would fail
good hardware most of the time it was tested, so it reads INCONCLUSIVE and the
answer is another run rather than a diagnosis.

The delta's band is NOT tighter than a single cell's, which is worth stating
because it looks wrong: Var(delta) = sigma^2/2 + sigma^2/2 = sigma^2. Averaging
two cells halves the variance and doing it to both arms then differencing adds
it straight back. Four cells buy no more precision than one.

THE TWO VERDICTS ARE NOT INDEPENDENT and are therefore not combined
symmetrically. The delta window rests on sigma = 0.10, and the pair spread is
what estimates sigma. So a run with wide spreads has refuted the premise of its
own delta threshold, and the delta there is UNSCORED rather than failed --
"this run cannot tell you" is a different statement from "the delta is bad",
and collapsing them counts one fault as two. Only on a spread PASS, where the
data confirm the sigma, does the delta stand on its own.

Both quantities are always printed whatever the verdict, because they test
different hypotheses: spread asks whether the apparatus added variance, delta
asks whether it shifted the measurement. One exit code cannot say which moved.

WHAT IT REFUSES TO SCORE. A comparison is only worth making if the thing that
changed is the thing under test, so it checks the things that would quietly
confound it and stops rather than reporting a number:

  * a missing cell log -- absent is not zero
  * a build_sha12 that differs between cells, or from the baseline: a different
    DOS binary means the Pi is not the only variable
  * a log whose contents predate the run, which is what a cell that never
    executed leaves behind

Usage:
  score-pump.py [--incoming DIR] [--baseline DIR]
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_INCOMING = os.path.expanduser("~/doskutsu-netiter/incoming")
DEFAULT_BASELINE = os.path.expanduser(
    "~/doskutsu-netiter/banked/roundP-pi3-2026-08-20")

CONTROL = ("GPU0", "GPU0B")
ARM = ("GPUA", "GPUAB")
CELLS = CONTROL + ARM

# --- the bands, fixed in advance (vcctrl's docs/lab/PI5-MIGRATION.md sec. 6a) --
SIGMA = 0.10                  # per-cell, from the profile's 0.2 read as 2 sigma
SPREAD_MAX = 0.2              # per pair
DELTA_CENTRE = 1.30
DELTA_PASS = (1.10, 1.50)     # +/- 2 sigma
DELTA_INCONCLUSIVE = (1.00, 1.60)
RESOLVE_FLOOR = 0.2           # below this you are reading quantisation

EMIT = re.compile(r"\[runmanifest-emit\]")
FIELD = re.compile(r"(\w+)=('[^']*'|\S+)")
STAMP = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\]")


def read_cell(d, tag):
    """Parse one cell's manifest line. Returns a dict, or None if absent.

    None means the file is not there. It is NOT folded into a zero or a
    default: a cell that did not run has no fps, and inventing one is how a
    hole becomes a data point.
    """
    p = os.path.join(d, tag + ".LOG")
    if not os.path.exists(p):
        return None
    line = None
    with open(p, errors="replace") as fh:
        for ln in fh:
            if EMIT.search(ln):
                line = ln.strip()
    if line is None:
        return {"tag": tag, "path": p, "error": "no runmanifest-emit line"}
    out = {"tag": tag, "path": p, "raw": line}
    for k, v in FIELD.findall(line):
        out[k] = v.strip("'")
    m = STAMP.match(line)
    if m:
        out["clock"] = "%s:%s:%s" % m.groups()
        # Cell MIDPOINT in seconds, from the engine's own emit timestamp less
        # half the duration it reports. Recoverable from every log already
        # collected, so drift can be fitted against real time rather than
        # against cell position -- position is only a proxy for time if every
        # cell costs the same, and these run 126-132 s.
        h, mi, sec = (int(x) for x in m.groups())
        try:
            out["mid_s"] = h * 3600 + mi * 60 + sec - float(out["dur"].rstrip("s")) / 2.0
        except (KeyError, ValueError, AttributeError):
            pass
    try:
        out["fps"] = float(out["per_loop_fps"])
    except (KeyError, ValueError):
        out["error"] = "no per_loop_fps"
    return out


def spread_verdict(a, b):
    s = round(abs(a - b), 4)
    return s, (s <= SPREAD_MAX)


def load(d, label, required=True):
    """Gather four cells. `required=False` is for the baseline, which the run
    can be scored without -- but the message must then say SKIPPING rather
    than REFUSING, or a soft absence reads as a hard stop."""
    cells, missing, broken = {}, [], []
    for t in CELLS:
        c = read_cell(d, t)
        if c is None:
            missing.append(t)
        elif "error" in c:
            broken.append("%s (%s)" % (t, c["error"]))
        else:
            cells[t] = c
    if missing or broken:
        print("%s %s" % ("REFUSING TO SCORE" if required
                         else "SKIPPING", label))
        if missing:
            print("  missing cell logs: %s" % ", ".join(missing))
            print("  A missing cell is not a zero and not an average of its")
            print("  neighbours. Something did not run; find out what.")
        if broken:
            print("  unparseable: %s" % ", ".join(broken))
        return None
    return cells


def manifest_diff(inc, base, have_baseline):
    """Report declared-field differences between the manifests, without
    failing on them.

    A declaration is what somebody believed was fitted; it is not a
    measurement, so a difference here does not invalidate a comparison by
    itself. But it is the cheapest possible signal that the two runs were not
    set up identically, and it should be the TOOL that surfaces it rather than
    whoever happens to read both files. This run differed in exactly one field
    and it was caught by eye; the next one might differ in a field that
    matters.
    """
    if not have_baseline:
        return
    a = os.path.join(base, "GPUMP.NFO")
    b = os.path.join(inc, "GPUMP.NFO")
    if not (os.path.exists(a) and os.path.exists(b)):
        print("\n== manifest ==")
        print("  no manifest to compare (%s)"
              % ("baseline missing" if not os.path.exists(a) else "run missing"))
        return

    def fields(p):
        out = {}
        for ln in open(p, errors="replace"):
            k, _, v = ln.strip().partition("=")
            if _ and " " not in k:
                out.setdefault(k, []).append(v.strip())
        return out

    fa, fb = fields(a), fields(b)
    keys = sorted(set(fa) | set(fb))
    diffs = [(k, fa.get(k), fb.get(k)) for k in keys if fa.get(k) != fb.get(k)]
    print("\n== manifest ==")
    if not diffs:
        print("  identical to the baseline in every declared field")
        return
    for k, va, vb in diffs:
        print("  %-20s baseline %-16s run %s"
              % (k, va[0] if va else "(absent)", vb[0] if vb else "(absent)"))
    print("  Declared fields, not measurements. A difference here does not")
    print("  invalidate the comparison on its own -- but it means the two runs")
    print("  were not set up identically, and you should know why.")


def main(argv):
    inc = DEFAULT_INCOMING
    base = DEFAULT_BASELINE
    if "--incoming" in argv:
        inc = argv[argv.index("--incoming") + 1]
    if "--baseline" in argv:
        base = argv[argv.index("--baseline") + 1]

    print("== cells ==")
    now = load(inc, "the new run")
    if now is None:
        return 2
    old = load(base, "the baseline", required=False)

    for t in CELLS:
        c = now[t]
        print("  %-6s %5.1f fps   dur=%-5s pump=%-2s build=%s  %s"
              % (t, c["fps"], c.get("dur", "?"), c.get("pump", "?"),
                 c.get("build_sha12", "?"), c.get("clock", "no timestamp")))

    # --- confound checks, before any arithmetic ---------------------------
    print("\n== is the Pi the only thing that changed? ==")
    builds = {now[t].get("build_sha12") for t in CELLS}
    if len(builds) != 1:
        print("  REFUSED: cells were produced by different builds: %s"
              % ", ".join(sorted(str(b) for b in builds)))
        print("  The comparison would attribute a code change to the harness.")
        return 2
    build = builds.pop()
    print("  all four cells built from %s" % build)

    if old:
        oldbuilds = {old[t].get("build_sha12") for t in CELLS}
        if oldbuilds != {build}:
            print("  REFUSED: baseline was built from %s, this run from %s."
                  % (", ".join(sorted(str(b) for b in oldbuilds)), build))
            print("  Two variables moved. Rebuild the baseline or re-run on the")
            print("  matching binary; do not difference these.")
            return 2
        print("  baseline matches, so the binary is not a variable")
    else:
        print("  NOTE: no baseline loaded -- reporting this run's own numbers")
        print("  only. Spread is still meaningful; the delta has nothing to be")
        print("  compared against.")

    if old:
        stale = [t for t in CELLS
                 if now[t].get("clock") and now[t]["clock"] == old[t].get("clock")]
        if stale:
            print("  REFUSED: %s carry the baseline's own timestamp."
                  % ", ".join(stale))
            print("  These are the previous run's logs, collected again.")
            return 2

    # --- the two quantities ----------------------------------------------
    manifest_diff(inc, base, old is not None)

    print("\n== pair spread (threshold %.1f) ==" % SPREAD_MAX)
    verdicts, signed = [], []
    for name, (a, b) in (("control", CONTROL), ("arm", ARM)):
        s, ok = spread_verdict(now[a]["fps"], now[b]["fps"])
        d = round(now[b]["fps"] - now[a]["fps"], 4)
        signed.append(d)
        was = ""
        if old:
            os_, _ = spread_verdict(old[a]["fps"], old[b]["fps"])
            od = round(old[b]["fps"] - old[a]["fps"], 4)
            was = "   (was %.1f, %+.1f)" % (os_, od)
        print("  %-8s %s %.1f -> %s %.1f  ->  spread %.1f (%+.1f)  %s%s"
              % (name, a, now[a]["fps"], b, now[b]["fps"], s, d,
                 "ok" if ok else "WIDE", was))
        verdicts.append(ok)

    # The SIGN matters more than the magnitude, and the magnitude alone hides
    # it. The repeat cell runs later in the sweep, so a consistent negative
    # difference on both pairs is monotone drift -- thermal, cache or harness
    # state -- and NOT the added variance the threshold was meant to catch. A
    # pair range is only a variance estimator when the sign is random; when it
    # is not, this test is a drift budget wearing a variance label.
    if all(x < 0 for x in signed) or all(x > 0 for x in signed):
        print("  NOTE: both pairs decline in the same direction (%s)."
              % ", ".join("%+.1f" % x for x in signed))
        print("  The repeat cell runs later, so a consistent sign is DRIFT")
        print("  across the sweep, not scatter within it. This run therefore")
        print("  says little about added variance -- the quantity the")
        print("  threshold nominally tests.")

    wide = verdicts.count(False)
    if wide == 0:
        spread_state = "pass"
    elif wide == 1:
        spread_state = "inconclusive"
    else:
        spread_state = "fail"

    print("\n== arm delta ==")
    cm = (now[CONTROL[0]]["fps"] + now[CONTROL[1]]["fps"]) / 2.0
    am = (now[ARM[0]]["fps"] + now[ARM[1]]["fps"]) / 2.0
    delta = am - cm
    print("  control mean %.2f   arm mean %.2f   delta %+.2f" % (cm, am, delta))
    if old:
        ocm = (old[CONTROL[0]]["fps"] + old[CONTROL[1]]["fps"]) / 2.0
        oam = (old[ARM[0]]["fps"] + old[ARM[1]]["fps"]) / 2.0
        odelta = oam - ocm
        print("  Pi 3 delta %+.2f   difference %+.2f" % (odelta, delta - odelta))
        if abs(delta - odelta) < RESOLVE_FLOOR:
            print("  (below the %.1f resolution floor -- reporting resolution is"
                  % RESOLVE_FLOOR)
            print("   0.1 and the statistical band is 0.1, so this difference is")
            print("   not a measurement of anything)")

    print("  bands: pass %.2f-%.2f  inconclusive %.2f-%.2f  outside = investigate"
          % (DELTA_PASS + DELTA_INCONCLUSIVE))

    # REPORTED, NOT SCORED -- deliberately. The naive delta is what the
    # pre-registered band applies to, and moving the verdict onto a better
    # statistic after seeing the data is the thing this whole tool exists to
    # avoid. But the naive delta is BIASED, and the reader should see by how
    # much.
    #
    # In execution order C A C A the control cells sit at positions 1 and 3
    # (mean 2.0) and the arm cells at 2 and 4 (mean 3.0), so the arm mean is
    # one cell-interval later and the delta carries slope x 1 cell. Fit a
    # common linear drift plus a constant arm offset -- four points, three
    # parameters -- and the offset is what the delta would have been with the
    # arms balanced. The residual on the fourth cell is the check: the model
    # has one degree of freedom left, so a non-zero residual means "constant
    # offset plus linear drift" does not describe this run.
    c0, c1_, a0, a1_ = (now[CONTROL[0]], now[CONTROL[1]],
                        now[ARM[0]], now[ARM[1]])
    have_times = all("mid_s" in x for x in (c0, c1_, a0, a1_))
    if not have_times:
        # Report-only section, so a missing timestamp skips it rather than
        # stopping the scoring. The verdict does not depend on any of this.
        print("\n  cannot fit drift: a cell is missing its timestamp or dur")
    if have_times:
        t = {k: x["mid_s"] - c0["mid_s"] for k, x in
             (("c0", c0), ("c1", c1_), ("a0", a0), ("a1", a1_))}
        slope = (c1_["fps"] - c0["fps"]) / (t["c1"] - t["c0"])
        slope_a = (a1_["fps"] - a0["fps"]) / (t["a1"] - t["a0"])
        offset = a0["fps"] - (c0["fps"] + slope * t["a0"])
        resid = a1_["fps"] - (c0["fps"] + offset + slope * t["a1"])
        imbalance = (t["a0"] + t["a1"]) / 2.0 - (t["c0"] + t["c1"]) / 2.0
        bias = slope * imbalance
        print("\n  drift %+.3e fps/s from the control pair, %+.3e from the arm pair"
              % (slope, slope_a))
        print("  arm cells sit %.1f s later on average -> delta carries %+.2f"
              % (imbalance, bias))
        print("  drift-corrected offset %+.2f   (residual %+.3f)" % (offset, resid))
        if slope and abs(slope_a - slope) / abs(slope) > 0.25:
            print("  NOTE: the two arms disagree on the slope by more than 25%.")
            print("  A single common drift does not describe this run, so the")
            print("  correction rests on a model the data do not support.")
        if abs(resid) > 0.05:
            print("  RESIDUAL IS NOT ZERO: 'constant offset plus linear drift' does")
            print("  not describe this run, so the correction above is not")
            print("  trustworthy either. Do not use it.")
    print("  Reported, NOT scored -- the band above was fixed on the naive")
    print("  delta and is not being moved after the fact. The fix for the bias")
    print("  is a counterbalanced C A A C cell order in a successor sweep.")
    print("  DO NOT promote this to the verdict. Measured over three runs, the")
    print("  correction makes run-to-run spread WORSE: naive delta SD 0.058,")
    print("  drift-corrected SD 0.077, +32%. A slope fitted from two points per")
    print("  arm, observed to vary 2x on identical hardware 30 min apart,")
    print("  imports its own instability into whatever it corrects. A bias you")
    print("  cannot measure reliably cannot be subtracted reliably.")

    # The two verdicts are NOT independent, so they are not combined
    # symmetrically. The delta window came from SD(delta) = sigma at sigma =
    # 0.10, and the pair spread is precisely what estimates sigma. A run whose
    # spreads have gone wide has contradicted the premise its own delta
    # threshold rests on, and a delta that drifted in that run is EXPLAINED by
    # the widened spread rather than being separate evidence. Scoring it anyway
    # would count one fault twice and dress it as two independent findings.
    if spread_state == "fail":
        delta_state = "unscored"
    elif DELTA_PASS[0] <= delta <= DELTA_PASS[1]:
        delta_state = "pass"
    elif DELTA_INCONCLUSIVE[0] <= delta <= DELTA_INCONCLUSIVE[1]:
        delta_state = "inconclusive"
    else:
        delta_state = "fail"

    # --- verdict ----------------------------------------------------------
    # Both quantities are always reported, whatever the combined verdict says.
    # They test different hypotheses -- spread asks whether the apparatus added
    # variance, delta asks whether it shifted the measurement -- and one word
    # cannot say which moved.
    print("\n== verdict ==")
    print("  pair spread : %-12s (did the apparatus add variance?)"
          % spread_state.upper())
    print("  arm delta   : %-12s (did it shift the measurement?)"
          % delta_state.upper())

    if spread_state == "fail":
        print("\n  FAIL -- both pairs are wide, so the harness has added")
        print("  variance. The delta is printed above but NOT scored: its")
        print("  window assumes the very sigma these spreads have just")
        print("  contradicted. This run cannot tell you anything about the")
        print("  delta, which is different from telling you the delta is bad.")
        return 1
    if spread_state == "inconclusive":
        print("\n  INCONCLUSIVE -- one pair of two is wide. That is a one-in-")
        print("  three event on healthy hardware, so it is not a diagnosis.")
        print("  The delta is scored above but rests on a sigma this run only")
        print("  half-confirms. Re-run PUMP.")
        return 3
    # spread PASS: sigma ~ 0.10 is confirmed by this run's own data, so the
    # delta window is valid and stands on its own.
    if delta_state == "fail":
        print("\n  FAIL -- spreads are tight, so the apparatus is quiet and the")
        print("  delta window is sound. The measurement itself has shifted.")
        return 1
    if delta_state == "inconclusive":
        print("\n  INCONCLUSIVE -- the apparatus is quiet, but the delta sits")
        print("  outside the pass window. Re-run PUMP; do not diagnose from a")
        print("  single pair per arm.")
        return 3
    print("\n  PASS -- both within-session quantities reproduce. The migration")
    print("  did not move the timing path.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
