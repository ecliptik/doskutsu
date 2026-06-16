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
 * is present and holds >=1 .mid, mapping dir -> logical hint value + label. */
static void test_midiset_scan(void)
{
  const char *base2 = "/tmp/dkt_ms_two";       /* both midi/ + orgmid/ present */
  const char *data2 = "/tmp/dkt_ms_two/data";
  const char *base1 = "/tmp/dkt_ms_one";       /* only midi/ present           */
  const char *data1 = "/tmp/dkt_ms_one/data";
  midiset_t sets[MIDISET_MAX];
  int n, wi, oi;

  mkdir(base2, 0777); mkdir(data2, 0777);
  mkdir("/tmp/dkt_ms_two/data/midi", 0777);
  mkdir("/tmp/dkt_ms_two/data/orgmid", 0777);
  write_file("/tmp/dkt_ms_two/data/midi/curly.mid", "MThd");
  write_file("/tmp/dkt_ms_two/data/midi/access.mid", "MThd");
  write_file("/tmp/dkt_ms_two/data/orgmid/curly.mid", "MThd");
  /* a non-.mid file must NOT be counted */
  write_file("/tmp/dkt_ms_two/data/midi/readme.txt", "x");

  n = midiset_scan(data2, sets, MIDISET_MAX);
  CHECK(n == 2, "midiset_scan: both sets present -> 2");
  wi = midiset_index_by_value(sets, n, "wiimidi");
  oi = midiset_index_by_value(sets, n, "orgmid");
  CHECK(wi >= 0 && strcmp(sets[wi].label, "WiiWare") == 0 &&
        strcmp(sets[wi].dir, "midi") == 0 && sets[wi].mid_count == 2,
        "midiset_scan: wiimidi -> data/midi, label WiiWare, 2 .mid (txt ignored)");
  CHECK(oi >= 0 && strcmp(sets[oi].label, "OrgMIDI") == 0 &&
        strcmp(sets[oi].dir, "orgmid") == 0 && sets[oi].mid_count == 1,
        "midiset_scan: orgmid -> data/orgmid, label OrgMIDI, 1 .mid");

  /* index_by_value: empty/NULL -> default wiimidi; unknown -> -1 */
  CHECK(midiset_index_by_value(sets, n, "") == wi,
        "midiset_index_by_value: empty -> default wiimidi");
  CHECK(midiset_index_by_value(sets, n, NULL) == wi,
        "midiset_index_by_value: NULL -> default wiimidi");
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
  printf("\n%s (%d failures)\n", g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_fail);
  return g_fail ? 1 : 0;
}
