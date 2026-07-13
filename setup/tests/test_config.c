/*
 * test_config.c -- host-side unit tests for the DOSKUTSU.CFG contract.
 *
 * Covers (1) the engine-side loader (doskutsu_config.h: parse + env-wins
 * precedence + unknown-key ignore), (2) the SETUP-side model round-trip
 * (setupcfg.c), and (3) the recommendation matrix (recommend.c). Builds
 * with a host compiler -- no DOS toolchain or SDL needed.
 *
 *   make -C setup test
 *
 * Exit 0 = all pass. ASCII-only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>   /* mkdir for the midiset scan fixture */

#include "doskutsu_config.h"   /* engine loader */
#include "setupcfg.h"          /* SETUP model   */
#include "profile.h"
#include "recommend.h"
#include "midiset.h"           /* MIDI music-set discovery (#39) */
#include "scancode.h"          /* BIOS-scancode <-> SDL-keycode table (Phase 3) */
#include "bindings.h"          /* control-binding session model (Phase 3)       */

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); ++g_fail; } \
    else         { printf("ok:   %s\n", (msg)); } \
  } while (0)

static void write_file(const char *path, const char *body)
{
  FILE *f = fopen(path, "wb");
  fputs(body, f);
  fclose(f);
}

static void test_loader(void)
{
  const char *path = "/tmp/DKT_TEST.CFG";

  /* a real env SET must survive the file (overwrite=0) */
  setenv("DOSKUTSU_USE_JOYSTICK", "REALSET", 1);
  unsetenv("SDL_HINT_DOSKUTSU_AUDIO_BACKEND");
  unsetenv("SDL_HINT_DOSKUTSU_PERF_MODE");
  unsetenv("SDL_HINT_DOSKUTSU_SB16_FM_VOL");

  write_file(path,
    "; comment\r\n"
    "AUDIO_BACKEND = opl3\r\n"   /* spaces + CRLF */
    "  perf_mode=2\n"            /* leading ws, lowercase, LF */
    "SB16_FM_VOL=20\n"
    "USE_JOYSTICK=1\n"          /* must NOT clobber REALSET */
    "TOTALLY_BOGUS=9\n"         /* unknown -> ignored */
    "\n");

  int n = doskutsu_cfg_load(path);
  CHECK(n >= 3, "loader applied at least 3 recognized keys");
  CHECK(getenv("SDL_HINT_DOSKUTSU_AUDIO_BACKEND") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_AUDIO_BACKEND"), "opl3") == 0,
        "AUDIO_BACKEND mapped to its hint name");
  CHECK(getenv("SDL_HINT_DOSKUTSU_PERF_MODE") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_PERF_MODE"), "2") == 0,
        "PERF_MODE parsed (lowercase key, LF line)");
  CHECK(getenv("SDL_HINT_DOSKUTSU_SB16_FM_VOL") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_SB16_FM_VOL"), "20") == 0,
        "SB16_FM_VOL parsed");
  CHECK(strcmp(getenv("DOSKUTSU_USE_JOYSTICK"), "REALSET") == 0,
        "env > file: real SET preserved (overwrite=0)");
  CHECK(getenv("DOSKUTSU_TOTALLY_BOGUS") == NULL,
        "unknown key ignored");

  CHECK(doskutsu_cfg_load("/tmp/does-not-exist.cfg") == -1,
        "absent file returns -1 (first-run = built-in defaults)");
}

static void test_roundtrip(void)
{
  scfg_t a, b;
  scfg_defaults(&a);
  scfg_set(&a, scfg_index("AUDIO_BACKEND"), "wb");
  scfg_set(&a, scfg_index("PERF_MODE"), "1");
  scfg_set(&a, scfg_index("SB16_VOICE_VOL"), "25");
  CHECK(scfg_save(&a, "/tmp/DKT_RT.CFG") == 0, "scfg_save ok");

  scfg_load(&b, "/tmp/DKT_RT.CFG");
  CHECK(strcmp(scfg_get(&b, scfg_index("AUDIO_BACKEND")), "wb") == 0,
        "round-trip AUDIO_BACKEND");
  CHECK(strcmp(scfg_get(&b, scfg_index("PERF_MODE")), "1") == 0,
        "round-trip PERF_MODE");
  CHECK(strcmp(scfg_get(&b, scfg_index("SB16_VOICE_VOL")), "25") == 0,
        "round-trip SB16_VOICE_VOL");
}

/* T52: scfg_differs drives the ESC "Save setting?" prompt (changed -> prompt;
 * unchanged or change-then-revert -> silent back). Must compare VALUE STRINGS,
 * not raw bytes, so a grow-then-shrink leaves no stale-byte false positive. */
static void test_scfg_differs(void)
{
  scfg_t a, b;
  int bi;
  scfg_defaults(&a);
  b = a;
  CHECK(scfg_differs(&a, &b) == 0, "scfg_differs: identical sessions -> 0");

  bi = scfg_index("AUDIO_BACKEND");
  scfg_set(&a, bi, "wb");
  CHECK(scfg_differs(&a, &b) == 1, "scfg_differs: a real change -> 1 (prompt)");

  scfg_set(&a, bi, scfg_get(&b, bi));
  CHECK(scfg_differs(&a, &b) == 0, "scfg_differs: change-then-revert -> 0 (no prompt)");

  /* grow ("organya", 7) then back to the shorter default: string compare must
   * still report no change despite stale post-NUL bytes. */
  scfg_set(&a, bi, "organya");
  scfg_set(&a, bi, scfg_get(&b, bi));
  CHECK(scfg_differs(&a, &b) == 0, "scfg_differs: grow-then-shrink revert -> 0");
}

static void test_recommend(void)
{
  scfg_t c; sysprofile_t p; char why[160];

  /* Pentium + WaveBlaster -> opl3 (WB is NEVER auto-recommended -- manual
   * choice only; operator directive WB-via-SETUP), faithful */
  memset(&p, 0, sizeof(p));
  p.cpu_class = CPU_586; p.has_fpu = 1; p.has_waveblaster = 1;
  p.has_opl3 = 1; p.snd_detected = 1; strcpy(p.cpu_desc, "Pentium 83");
  recommend_apply(&c, &p, why, sizeof(why));
  CHECK(strcmp(scfg_get(&c, scfg_index("AUDIO_BACKEND")), "opl3") == 0,
        "recommend: Pentium+WB -> opl3 (WB never auto-selected)");
  CHECK(strcmp(scfg_get(&c, scfg_index("PERF_MODE")), "0") == 0,
        "recommend: Pentium -> perf 0");

  /* mid 486 (DX2-66) + WaveBlaster -> opl3, smooth (WB not auto-selected) */
  memset(&p, 0, sizeof(p));
  p.cpu_class = CPU_486_MID; p.has_fpu = 1; p.has_waveblaster = 1;
  p.has_opl3 = 1; p.snd_detected = 1; strcpy(p.cpu_desc, "486DX2-66");
  recommend_apply(&c, &p, why, sizeof(why));
  CHECK(strcmp(scfg_get(&c, scfg_index("AUDIO_BACKEND")), "opl3") == 0,
        "recommend: mid 486+WB -> opl3 (WB never auto-selected)");
  CHECK(strcmp(scfg_get(&c, scfg_index("PERF_MODE")), "1") == 0,
        "recommend: mid 486 -> perf 1 (smooth)");

  /* slow 486, no WB -> opl3, smooth */
  memset(&p, 0, sizeof(p));
  p.cpu_class = CPU_486_SLOW; p.has_fpu = 1; p.has_opl3 = 1; p.snd_detected = 1;
  strcpy(p.cpu_desc, "486DX2-50");
  recommend_apply(&c, &p, why, sizeof(why));
  CHECK(strcmp(scfg_get(&c, scfg_index("AUDIO_BACKEND")), "opl3") == 0,
        "recommend: slow 486 no WB -> opl3");
  CHECK(strcmp(scfg_get(&c, scfg_index("PERF_MODE")), "1") == 0,
        "recommend: slow 486 -> perf 1");
  CHECK(strcmp(scfg_get(&c, scfg_index("FIXED_TIMESTEP")), "1") == 0,
        "recommend: fixed-timestep always on");
}

/* Presence-checked keys (AUDIO_OFF -> DOSKUTSU_NO_AUDIO): the engine reads
 * mere getenv()-presence, so a "0" value MUST leave the var ABSENT, else a
 * saved config with AUDIO_OFF=0 would silently disable all audio. */
static void test_presence_checked(void)
{
  const char *path = "/tmp/DKT_PRES.CFG";

  /* AUDIO_OFF=0 -> DOSKUTSU_NO_AUDIO must NOT be set (stays absent = audio on) */
  unsetenv("DOSKUTSU_NO_AUDIO");
  write_file(path, "AUDIO_OFF=0\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("DOSKUTSU_NO_AUDIO") == NULL,
        "presence-checked AUDIO_OFF=0 leaves DOSKUTSU_NO_AUDIO ABSENT (audio stays on)");

  /* AUDIO_OFF=1 -> DOSKUTSU_NO_AUDIO must be present (audio off) */
  unsetenv("DOSKUTSU_NO_AUDIO");
  write_file(path, "AUDIO_OFF=1\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("DOSKUTSU_NO_AUDIO") != NULL &&
        strcmp(getenv("DOSKUTSU_NO_AUDIO"), "1") == 0,
        "presence-checked AUDIO_OFF=1 sets DOSKUTSU_NO_AUDIO=1 (audio off)");

  /* A real env SET still wins over a file AUDIO_OFF=0 (we never touch it). */
  setenv("DOSKUTSU_NO_AUDIO", "1", 1);
  write_file(path, "AUDIO_OFF=0\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("DOSKUTSU_NO_AUDIO") != NULL &&
        strcmp(getenv("DOSKUTSU_NO_AUDIO"), "1") == 0,
        "env > file: real SET DOSKUTSU_NO_AUDIO=1 survives file AUDIO_OFF=0");
  unsetenv("DOSKUTSU_NO_AUDIO");
}

/* Authoritative keys (BLASTER -> DKT_F_AUTHORITATIVE): unlike the env>file
 * tuning keys, the config value must OVERRIDE an ambient AUTOEXEC SET
 * (file > env) because SETUP's sound-hardware choice is authoritative. */
static void test_authoritative(void)
{
  const char *path = "/tmp/DKT_AUTH.CFG";

  /* ambient SET BLASTER (wrong port/IRQ) must be OVERRIDDEN by the file */
  setenv("BLASTER", "A240 I7 D1 H5", 1);
  write_file(path, "BLASTER=A220 I5 D1 H5 P330 T6\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("BLASTER") && strcmp(getenv("BLASTER"), "A220 I5 D1 H5 P330 T6") == 0,
        "authoritative: file BLASTER OVERRIDES ambient SET (file > env)");

  /* contrast: a non-authoritative key (PERF_MODE) -- a real SET still wins */
  setenv("SDL_HINT_DOSKUTSU_PERF_MODE", "2", 1);
  write_file(path, "PERF_MODE=1\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("SDL_HINT_DOSKUTSU_PERF_MODE") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_PERF_MODE"), "2") == 0,
        "non-authoritative: real SET PERF_MODE still wins (env > file)");
  unsetenv("SDL_HINT_DOSKUTSU_PERF_MODE");

  /* file with NO BLASTER line must leave an ambient SET BLASTER untouched */
  setenv("BLASTER", "A220 I5 D1 H5 T6", 1);
  write_file(path, "PERF_MODE=0\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("BLASTER") && strcmp(getenv("BLASTER"), "A220 I5 D1 H5 T6") == 0,
        "absent BLASTER line leaves ambient SET BLASTER untouched");
  unsetenv("BLASTER");
}

/* T8: auto-suggest ORG_PRERENDER when the user manually picks Organya on a
 * sub-Pentium CPU. Pure logic in recommend_org_prerender(). */
static void test_org_prerender_suggest(void)
{
  scfg_t c; sysprofile_t p;

  /* The ORG_PRERENDER config-key default is now ON (1), matching the engine
   * default-ON, so a default organya install uses the PCM cache. (Regression
   * fix: a CFG default of 0 silently forced live synth via the 0216 shim.) */
  scfg_defaults(&c);
  CHECK(strcmp(scfg_get(&c, scfg_index("ORG_PRERENDER")), "1") == 0,
        "org-prerender: config-key default is ON (1)");

  /* slow 486 + organya, forced off -> prerender auto-enabled (returns 1) */
  scfg_set(&c, scfg_index("AUDIO_BACKEND"), "organya");
  scfg_set(&c, scfg_index("ORG_PRERENDER"), "0"); /* force off to test the enable path */
  memset(&p, 0, sizeof(p)); p.cpu_class = CPU_486_SLOW;
  CHECK(recommend_org_prerender(&c, &p) == 1,
        "org-prerender suggest: slow 486 + organya -> enabled");
  CHECK(strcmp(scfg_get(&c, scfg_index("ORG_PRERENDER")), "1") == 0,
        "org-prerender suggest: ORG_PRERENDER set to 1");

  /* idempotent: already on -> returns 0, stays on */
  CHECK(recommend_org_prerender(&c, &p) == 0,
        "org-prerender suggest: idempotent when already on");

  /* mid-tier 486 (DX2-66 / Am5x86) + organya, forced off -> also auto-enabled */
  scfg_defaults(&c);
  scfg_set(&c, scfg_index("AUDIO_BACKEND"), "organya");
  scfg_set(&c, scfg_index("ORG_PRERENDER"), "0"); /* force off to test the enable path */
  memset(&p, 0, sizeof(p)); p.cpu_class = CPU_486_MID;
  CHECK(recommend_org_prerender(&c, &p) == 1 &&
        strcmp(scfg_get(&c, scfg_index("ORG_PRERENDER")), "1") == 0,
        "org-prerender suggest: mid-486 + organya -> enabled");

  /* Pentium + organya -> recommend does NOT actively enable (live synth is
   * real-time there); from an explicit off it leaves the value alone. */
  scfg_defaults(&c);
  scfg_set(&c, scfg_index("AUDIO_BACKEND"), "organya");
  scfg_set(&c, scfg_index("ORG_PRERENDER"), "0"); /* explicit off: recommend must leave it */
  memset(&p, 0, sizeof(p)); p.cpu_class = CPU_586;
  CHECK(recommend_org_prerender(&c, &p) == 0,
        "org-prerender suggest: Pentium + organya -> NOT enabled");
  CHECK(strcmp(scfg_get(&c, scfg_index("ORG_PRERENDER")), "0") == 0,
        "org-prerender suggest: ORG_PRERENDER stays 0 on Pentium");

  /* non-organya backend on a slow CPU -> NOT enabled */
  scfg_defaults(&c);
  scfg_set(&c, scfg_index("AUDIO_BACKEND"), "opl3");
  memset(&p, 0, sizeof(p)); p.cpu_class = CPU_486_SLOW;
  CHECK(recommend_org_prerender(&c, &p) == 0,
        "org-prerender suggest: opl3 backend -> NOT enabled");
}

/* T67: test_wb_risky() removed with recommend_wb_risky() -- the WaveBlaster
 * freeze-risk advisory is gone (WB fixed + ships default-on). */

/* Phase-1 / T5: SETUP-only keys (NULL env_name, e.g. SPEED_CLASS) must be
 * SKIPPED by the engine loader -- never setenv (a NULL name would crash) --
 * while engine-consumed keys on the same file still apply. SETUP itself must
 * still round-trip the value through DOSKUTSU.CFG (it remembers the preset). */
static void test_speed_class(void)
{
  const char *path = "/tmp/DKT_SPEED.CFG";
  scfg_t a, b;
  int idx;

  unsetenv("SPEED_CLASS");
  unsetenv("SDL_HINT_DOSKUTSU_PERF_MODE");
  write_file(path, "SPEED_CLASS=fast\r\nPERF_MODE=1\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("SPEED_CLASS") == NULL,
        "SETUP-only SPEED_CLASS is NOT published to the environment");
  CHECK(getenv("SDL_HINT_DOSKUTSU_PERF_MODE") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_PERF_MODE"), "1") == 0,
        "engine-consumed key past a SETUP-only key still applies");
  unsetenv("SDL_HINT_DOSKUTSU_PERF_MODE");

  idx = scfg_index("SPEED_CLASS");
  CHECK(idx >= 0, "SPEED_CLASS is a known SETUP key");
  scfg_defaults(&a);
  scfg_set(&a, idx, "fast");
  CHECK(scfg_save(&a, path) == 0, "scfg_save with SPEED_CLASS ok");
  scfg_load(&b, path);
  CHECK(strcmp(scfg_get(&b, scfg_index("SPEED_CLASS")), "fast") == 0,
        "round-trip SPEED_CLASS preset");
}

/* #39 / T1: the MIDI_SET key. Default is the byte-neutral "wiimidi" (the value
 * that maps to data/midi/ WITHOUT tripping the engine's unrecognized-fallback
 * warning a literal "midi" would). It round-trips through the SETUP model and
 * the engine loader publishes it to the MIDI-source hint. */
static void test_midiset_key(void)
{
  const char *path = "/tmp/DKT_MIDISET.CFG";
  scfg_t a, b;
  int idx;

  idx = scfg_index("MIDI_SET");
  CHECK(idx >= 0, "MIDI_SET is a known SETUP key");

  scfg_defaults(&a);
  CHECK(strcmp(scfg_get(&a, idx), "wiimidi") == 0,
        "MIDI_SET default is wiimidi (byte-neutral, no fallback warning)");

  scfg_set(&a, idx, "orgmid");
  CHECK(scfg_save(&a, path) == 0, "scfg_save with MIDI_SET ok");
  scfg_load(&b, path);
  CHECK(strcmp(scfg_get(&b, scfg_index("MIDI_SET")), "orgmid") == 0,
        "round-trip MIDI_SET=orgmid");

  /* the engine loader maps MIDI_SET -> SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE */
  unsetenv("SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE");
  write_file(path, "MIDI_SET=orgmid\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE"), "orgmid") == 0,
        "MIDI_SET mapped to its hint name (SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE)");
  unsetenv("SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE");
}

/* #39 / T2: midiset_scan() lists ONLY the known logical sets whose data subdir
 * is present and holds >=1 .mid, mapping dir -> logical hint value + label.
 *
 * ORGMID2 WORLD (main 5605db3, 2026-07-06): the known set at index 0 is now
 * { "orgmid2", "orgmid2", "OrgMIDI" } -- our org2mid v2 native-GM conversion --
 * and MIDISET_DEFAULT_VALUE moved wiimidi -> orgmid2 (operator g2k A/B: distinct
 * GM drums, richer arrangements). This fixture had never been updated, so it
 * built data/orgmid where production now expects data/orgmid2. The LEGACY
 * data/orgmid dir is no longer a known set at all: it falls through to the #39b
 * custom-drop-in path as "Custom (orgmid)". We now assert BOTH, so the next
 * rename cannot silently strand this test again.
 *
 * NOTE (deliberately NOT asserted here): the shared registry default for the
 * MIDI_SET key (include/doskutsu_config_keys.h) is still "wiimidi" and diverges
 * from MIDISET_DEFAULT_VALUE. That divergence is a production question, not a
 * test question -- test_midiset_key() below pins the registry side as it stands.
 */
static void test_midiset_scan(void)
{
  const char *base2 = "/tmp/dkt_ms_two";   /* midi/ + orgmid2/ + drop-ins present */
  const char *data2 = "/tmp/dkt_ms_two/data";
  const char *base1 = "/tmp/dkt_ms_one";       /* only midi/ present           */
  const char *data1 = "/tmp/dkt_ms_one/data";
  midiset_t sets[MIDISET_MAX];
  int n, wi, oi;

  mkdir(base2, 0777); mkdir(data2, 0777);
  mkdir("/tmp/dkt_ms_two/data/midi", 0777);
  mkdir("/tmp/dkt_ms_two/data/orgmid2", 0777);   /* the KNOWN OrgMIDI set now */
  write_file("/tmp/dkt_ms_two/data/midi/curly.mid", "MThd");
  write_file("/tmp/dkt_ms_two/data/midi/access.mid", "MThd");
  write_file("/tmp/dkt_ms_two/data/orgmid2/curly.mid", "MThd");
  /* a non-.mid file must NOT be counted */
  write_file("/tmp/dkt_ms_two/data/midi/readme.txt", "x");
  /* 5605db3: the LEGACY data/orgmid is NOT a known set any more -- it must
   * surface as a plain custom drop-in, exactly like any user dir. */
  mkdir("/tmp/dkt_ms_two/data/orgmid", 0777);
  write_file("/tmp/dkt_ms_two/data/orgmid/curly.mid", "MThd");
  /* #39b: an unknown subdir with >=1 .mid must surface as a custom drop-in */
  mkdir("/tmp/dkt_ms_two/data/mymidi", 0777);
  write_file("/tmp/dkt_ms_two/data/mymidi/curly.mid", "MThd");
  write_file("/tmp/dkt_ms_two/data/mymidi/access.mid", "MThd");
  /* #39b: an unknown subdir with NO .mid must NOT be offered */
  mkdir("/tmp/dkt_ms_two/data/org", 0777);            /* .org source, no .mid */
  write_file("/tmp/dkt_ms_two/data/org/curly.org", "Org-02");

  {
    int ci, li;
    n = midiset_scan(data2, sets, MIDISET_MAX);
    CHECK(n == 4, "midiset_scan: 2 known + 2 custom drop-ins -> 4 (empty dir ignored)");
    wi = midiset_index_by_value(sets, n, "wiimidi");
    oi = midiset_index_by_value(sets, n, "orgmid2");
    CHECK(wi >= 0 && wi < 2 && strcmp(sets[wi].label, "WiiWare") == 0 &&
          strcmp(sets[wi].dir, "midi") == 0 && sets[wi].mid_count == 2,
          "midiset_scan: wiimidi -> data/midi, label WiiWare, 2 .mid (txt ignored)");
    CHECK(oi >= 0 && oi < 2 && strcmp(sets[oi].label, "OrgMIDI") == 0 &&
          strcmp(sets[oi].dir, "orgmid2") == 0 && sets[oi].mid_count == 1,
          "midiset_scan: orgmid2 -> data/orgmid2, label OrgMIDI, 1 .mid (5605db3)");
    /* #39b custom drop-in: value=dir, label "Custom (<dir>)", appended AFTER
     * the known sets (idx >= MIDISET_KNOWN count). The two customs are appended
     * in readdir order, so assert only "after the known sets", never a fixed slot. */
    ci = midiset_index_by_value(sets, n, "mymidi");
    CHECK(ci >= 2 && strcmp(sets[ci].dir, "mymidi") == 0 &&
          strcmp(sets[ci].label, "Custom (mymidi)") == 0 &&
          sets[ci].mid_count == 2,
          "midiset_scan: #39b mymidi -> Custom (mymidi), value mymidi, 2 .mid, after known sets");
    /* the 5605db3 regression guard: legacy orgmid is a CUSTOM drop-in now. */
    li = midiset_index_by_value(sets, n, "orgmid");
    CHECK(li >= 2 && strcmp(sets[li].dir, "orgmid") == 0 &&
          strcmp(sets[li].label, "Custom (orgmid)") == 0 &&
          sets[li].mid_count == 1,
          "midiset_scan: legacy orgmid is a custom drop-in, NOT the known OrgMIDI set");
    CHECK(midiset_index_by_value(sets, n, "org") == -1,
          "midiset_scan: #39b data/org (no .mid) not offered");
  }

  /* index_by_value: empty/NULL -> MIDISET_DEFAULT_VALUE (orgmid2 since 5605db3);
   * unknown -> -1 */
  CHECK(midiset_index_by_value(sets, n, "") == oi,
        "midiset_index_by_value: empty -> default orgmid2 (MIDISET_DEFAULT_VALUE)");
  CHECK(midiset_index_by_value(sets, n, NULL) == oi,
        "midiset_index_by_value: NULL -> default orgmid2 (MIDISET_DEFAULT_VALUE)");
  CHECK(midiset_index_by_value(sets, n, "nope") == -1,
        "midiset_index_by_value: unknown value -> -1");

  /* single-set case: only midi/ present -> 1 (the Music row would be omitted) */
  mkdir(base1, 0777); mkdir(data1, 0777);
  mkdir("/tmp/dkt_ms_one/data/midi", 0777);
  write_file("/tmp/dkt_ms_one/data/midi/curly.mid", "MThd");
  n = midiset_scan(data1, sets, MIDISET_MAX);
  CHECK(n == 1, "midiset_scan: only midi/ present -> 1 (row hidden when <2)");

  /* no data dir at all -> 0 */
  n = midiset_scan("/tmp/dkt_ms_does_not_exist", sets, MIDISET_MAX);
  CHECK(n == 0, "midiset_scan: absent data dir -> 0");
}

/* Phase 3 / #40: the BIND_* per-action remap keys + the reserved JOY_CAL
 * calibration key. The engine loader (input.cpp consumes the env vars; here we
 * only verify the shim publishes them to the right names) maps BIND_<ACTION>
 * to DOSKUTSU_BIND_<ACTION> and JOY_CAL to SDL_HINT_DOSKUTSU_JOY_CAL. Defaults
 * are "" so SETUP omits the line at default (killswitch: absent == today's
 * controls). The SETUP model must round-trip the value strings. */
static void test_input_bindings(void)
{
  const char *path = "/tmp/DKT_BIND.CFG";
  scfg_t a, b;
  int idx;

  /* keys are registered (engine-consumed, real env names) */
  CHECK(scfg_index("BIND_JUMP") >= 0, "BIND_JUMP is a known key");
  CHECK(scfg_index("BIND_MAP") >= 0, "BIND_MAP is a known key");
  CHECK(scfg_index("JOY_CAL") >= 0, "JOY_CAL is a known key");

  /* defaults are empty (so SETUP omits them -> behavior-neutral) */
  scfg_defaults(&a);
  CHECK(strcmp(scfg_get(&a, scfg_index("BIND_JUMP")), "") == 0,
        "BIND_JUMP default is empty (killswitch: action keeps settings.dat binding)");
  CHECK(strcmp(scfg_get(&a, scfg_index("JOY_CAL")), "") == 0,
        "JOY_CAL default is empty (SDL uses auto-calibration)");

  /* the engine loader publishes BIND_JUMP -> DOSKUTSU_BIND_JUMP (key+button)
   * and JOY_CAL -> SDL_HINT_DOSKUTSU_JOY_CAL, verbatim value pass-through */
  unsetenv("DOSKUTSU_BIND_JUMP");
  unsetenv("DOSKUTSU_BIND_FIRE");
  unsetenv("SDL_HINT_DOSKUTSU_JOY_CAL");
  write_file(path,
    "BIND_JUMP=k:122,b:0\r\n"   /* SDLK_Z + gameport button 0 */
    "BIND_FIRE=k:120\r\n"       /* SDLK_X, no button           */
    "JOY_CAL=12,250,14,248\r\n");
  doskutsu_cfg_load(path);
  CHECK(getenv("DOSKUTSU_BIND_JUMP") &&
        strcmp(getenv("DOSKUTSU_BIND_JUMP"), "k:122,b:0") == 0,
        "BIND_JUMP mapped to DOSKUTSU_BIND_JUMP (key+button verbatim)");
  CHECK(getenv("DOSKUTSU_BIND_FIRE") &&
        strcmp(getenv("DOSKUTSU_BIND_FIRE"), "k:120") == 0,
        "BIND_FIRE mapped to DOSKUTSU_BIND_FIRE (key only)");
  CHECK(getenv("SDL_HINT_DOSKUTSU_JOY_CAL") &&
        strcmp(getenv("SDL_HINT_DOSKUTSU_JOY_CAL"), "12,250,14,248") == 0,
        "JOY_CAL mapped to SDL_HINT_DOSKUTSU_JOY_CAL (SDL-consumed)");
  unsetenv("DOSKUTSU_BIND_JUMP");
  unsetenv("DOSKUTSU_BIND_FIRE");
  unsetenv("SDL_HINT_DOSKUTSU_JOY_CAL");

  /* SETUP model round-trip of a binding string */
  idx = scfg_index("BIND_LEFT");
  scfg_defaults(&a);
  scfg_set(&a, idx, "k:1073741904");   /* SDLK_LEFT */
  CHECK(scfg_save(&a, path) == 0, "scfg_save with BIND_LEFT ok");
  scfg_load(&b, path);
  CHECK(strcmp(scfg_get(&b, scfg_index("BIND_LEFT")), "k:1073741904") == 0,
        "round-trip BIND_LEFT binding string");
}

/* Phase 3 / scancode table: BIOS getch() result -> SDL key identity. The
 * table is built on fixed SDL facts so it is testable on the host. */
static void test_scancode(void)
{
  const setup_key_t *k;
  char tok[24];

  /* lowercase + uppercase letters fold to the same (lowercase) keycode */
  k = scancode_decode(0, 'z');
  CHECK(k && k->keycode == 'z' && strcmp(k->sdl_name, "Z") == 0,
        "scancode: 'z' -> SDLK_Z, name Z");
  k = scancode_decode(0, 'Z');
  CHECK(k && k->keycode == 'z', "scancode: shifted 'Z' folds to the 'z' keycode");

  /* digit + unshifted/shifted punctuation (US layout) -> base physical key */
  k = scancode_decode(0, '1');
  CHECK(k && k->keycode == '1', "scancode: '1' -> SDLK_1");
  k = scancode_decode(0, '!');
  CHECK(k && k->keycode == '1', "scancode: shifted '!' folds to the '1' key");
  k = scancode_decode(0, ':');
  CHECK(k && k->keycode == ';', "scancode: shifted ':' folds to the ';' key");

  /* extended (arrow) scancodes */
  k = scancode_decode(1, 0x4B);
  CHECK(k && strcmp(k->sdl_name, "Left") == 0 && k->keycode == (0x40000000L + 80),
        "scancode: ext 0x4B -> Left arrow (SDLK_LEFT)");
  k = scancode_decode(1, 0x48);
  CHECK(k && strcmp(k->sdl_name, "Up") == 0, "scancode: ext 0x48 -> Up arrow");

  /* issue 2: Enter is EXCLUDED from the bindable set (it is the row-confirm key)
   * so getch() of '\r' decodes to NULL -> capture re-prompts, never binds it. */
  CHECK(scancode_decode(0, '\r') == NULL,
        "scancode: Enter ('\\r') excluded from the bindable set -> NULL");
  CHECK(scancode_by_keycode('\r') == NULL,
        "scancode: SDLK_RETURN not in the table (Enter unbindable)");
  CHECK(scancode_decode(1, 0x3B) == NULL,
        "scancode: F1 (ext 0x3B) not in remappable set -> NULL");

  /* issue 1: lone modifiers resolve through scancode_modifier (captured from the
   * BIOS shift byte, not getch). Ctrl/Alt fold to the LEFT variants. */
  k = scancode_modifier(SETUP_MOD_LSHIFT);
  CHECK(k && k->keycode == (0x40000000L + 225), "scancode_modifier: LShift -> SDLK_LSHIFT");
  k = scancode_modifier(SETUP_MOD_RSHIFT);
  CHECK(k && k->keycode == (0x40000000L + 229), "scancode_modifier: RShift -> SDLK_RSHIFT");
  k = scancode_modifier(SETUP_MOD_CTRL);
  CHECK(k && k->keycode == (0x40000000L + 224), "scancode_modifier: Ctrl -> SDLK_LCTRL");
  k = scancode_modifier(SETUP_MOD_ALT);
  CHECK(k && k->keycode == (0x40000000L + 226), "scancode_modifier: Alt -> SDLK_LALT");
  CHECK(scancode_modifier(SETUP_MOD_NONE) == NULL,
        "scancode_modifier: NONE -> NULL");
  /* a modifier keycode round-trips through the display/bind-token path */
  k = scancode_by_keycode(0x40000000L + 224);
  scancode_bind_token(tok, sizeof(tok), k);
  CHECK(strcmp(tok, "1073742048") == 0, "scancode_bind_token: LCtrl -> '1073742048'");

  /* name lookup is case-insensitive (matches any contract spelling) */
  CHECK(scancode_by_name("left") == scancode_by_name("Left"),
        "scancode_by_name: case-insensitive");

  /* the bind token is the DECIMAL SDL keycode (engine contract, patch 0227) */
  k = scancode_by_name("Left");
  scancode_bind_token(tok, sizeof(tok), k);
  CHECK(strcmp(tok, "1073741904") == 0, "scancode_bind_token: Left -> '1073741904'");
  k = scancode_by_keycode('z');
  scancode_bind_token(tok, sizeof(tok), k);
  CHECK(strcmp(tok, "122") == 0, "scancode_bind_token: SDLK_Z -> '122'");
}

/* Phase 3 / control bindings: the session model defaults + conflict detection.
 * The full BIND_* config round-trip activates once input-eng adds the BIND_*
 * registry keys (bindings_save/load are no-ops for keys the registry omits);
 * until then these cover the contract-independent logic. */
static void test_bindings(void)
{
  binding_t b[BIND_COUNT];
  scfg_t c;
  int written;

  bindings_defaults(b);
  /* action 0 = Left (arrow), action 4 = Jump (Z) per the engine defaults */
  CHECK(strcmp(b[0].name, "Left") == 0 && b[0].keycode == (0x40000000L + 80),
        "bindings: action 0 default = Left arrow");
  CHECK(strcmp(b[4].name, "Jump") == 0 && b[4].keycode == 'z',
        "bindings: Jump default = Z");
  /* Q-B2 joystick defaults: Jump/Fire/WpnPrev/WpnNext = buttons 0..3 */
  CHECK(b[4].jbut == 0 && b[5].jbut == 1 && b[7].jbut == 2 && b[8].jbut == 3,
        "bindings: Q-B2 joystick button defaults");
  CHECK(b[0].jbut == -1 && b[10].jbut == -1,
        "bindings: non-default actions have <None> joystick button");

  /* conflict detection (drives the Q-B1 keyboard swap) */
  CHECK(bindings_find_key(b, 'z', 99) == 4, "bindings_find_key: 'z' -> Jump");
  CHECK(bindings_find_key(b, 'z', 4) == -1, "bindings_find_key: skips the except row");
  CHECK(bindings_find_jbut(b, 1, 99) == 5, "bindings_find_jbut: button 1 -> Fire");
  CHECK(bindings_find_jbut(b, -1, 99) == -1, "bindings_find_jbut: <None> never matches");

  /* save -> config (the registry now defines BIND_*): defaults write the
   * numeric keycode token + the Q-B2 joystick button where set. */
  scfg_defaults(&c);
  written = bindings_save(b, &c);
  CHECK(written == BIND_COUNT, "bindings_save: writes all 11 BIND_* keys");
  CHECK(strcmp(scfg_get(&c, scfg_index("BIND_JUMP")), "k:122,b:0") == 0,
        "bindings_save: Jump -> k:122,b:0 (SDLK_Z + button 0)");
  CHECK(strcmp(scfg_get(&c, scfg_index("BIND_LEFT")), "k:1073741904") == 0,
        "bindings_save: Left -> k:1073741904 (no button, <None>)");

  /* load round-trip: a rebound Jump (X, no button) reads back correctly */
  scfg_set(&c, scfg_index("BIND_JUMP"), "k:120");
  {
    binding_t b2[BIND_COUNT];
    bindings_load(b2, &c);
    CHECK(b2[4].keycode == 'x' && b2[4].jbut == -1,
          "bindings_load: BIND_JUMP=k:120 -> keycode X, button cleared");
    CHECK(b2[0].keycode == (0x40000000L + 80),
          "bindings_load: BIND_LEFT round-trips to the Left-arrow keycode");
  }
}

int main(void)
{
  test_loader();
  test_roundtrip();
  test_scfg_differs();
  test_recommend();
  test_presence_checked();
  test_authoritative();
  test_org_prerender_suggest();
  test_speed_class();
  test_midiset_key();
  test_midiset_scan();
  test_input_bindings();
  test_scancode();
  test_bindings();
  printf("\n%s (%d failures)\n", g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_fail);
  return g_fail ? 1 : 0;
}
