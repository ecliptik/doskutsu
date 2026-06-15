/*
 * recommend.c -- profile -> recommended settings. See recommend.h.
 *
 * Recommendation matrix (from CURRENT-STATE.md cross-CPU anchors +
 * v1_0_8_shipped):
 *
 *   Detected               Music backend   Perf mode
 *   --------------------   -------------   ----------------------
 *   Pentium-class          opl3            0 (faithful)
 *   486DX2-66 / Am5x86     opl3            1 (smooth)
 *   486DX2-50 or slower    opl3            1 (slower -> 2 if no FPU headroom)
 *   no sound card          auto            (auto-chain = OPL3 -> Organya)
 *
 * WaveBlaster is NEVER auto-recommended (operator directive: WB only via
 * SETUP). The blind MPU-401 init cannot confirm a wavetable is actually
 * present, so auto-selecting "wb" risks silent music on a box without one;
 * it is a MANUAL choice the user makes in the Sound screen and confirms with
 * the audio test. OPL3 FM is the reliable default on every CPU tier.
 *
 * Organya as a backend on a slow 486 gets ORG_PRERENDER=1 (the v1.0.8 PCM
 * disk cache) since live Organya synth is sub-real-time there. OPL3 is the
 * safe 486 gameplay default. FIXED_TIMESTEP stays ON everywhere (it fixes
 * game SPEED independent of render fps).
 */

#include <stdio.h>
#include <string.h>

#include "recommend.h"

static void set_key(scfg_t *c, const char *cfg_key, const char *val)
{
  int idx = scfg_index(cfg_key);
  if (idx >= 0) scfg_set(c, idx, val);
}

void recommend_apply(scfg_t *c, const sysprofile_t *p, char *rationale, int cap)
{
  const char *backend;
  const char *perf;
  const char *why;

  scfg_defaults(c);

  /* Backend: OPL3 FM is the reliable default on every CPU tier. WaveBlaster
   * is NEVER auto-recommended -- it is a MANUAL choice (Sound screen + audio
   * test). The blind MPU-401 init cannot confirm a wavetable is present, so
   * auto-selecting "wb" risks silent music on a box without one (operator
   * directive: WB only via SETUP). has_waveblaster is intentionally ignored
   * here. */
  if (p->has_opl3 || p->snd_detected)
  {
    backend = "opl3";
    why     = "OPL3 FM synth (WaveBlaster is a manual choice in Sound setup)";
  }
  else
  {
    backend = "auto";
    why     = "no sound card detected -> auto-detect (OPL3 -> Organya)";
  }
  set_key(c, "AUDIO_BACKEND", backend);

  /* Perf mode scales with CPU budget. */
  switch (p->cpu_class)
  {
    case CPU_586:
      perf = "0";
      break;
    case CPU_486_MID:
      perf = "1"; /* OPL3 default on a DX2-66/Am5x86 -> smooth */
      break;
    case CPU_486_SLOW:
    default:
      perf = "1";
      break;
  }
  set_key(c, "PERF_MODE", perf);

  /* Fixed timestep always on; audio tier2 (11025 mono) always on. */
  set_key(c, "FIXED_TIMESTEP", "1");
  set_key(c, "AUDIO_TIER2", "1");

  /* SB16 mixer: production-validated balance (Bug 6 fix). T76: voice 31->28
   * (operator-validated -- SFX drowned the music at 31). fm stays 28. */
  set_key(c, "SB16_VOICE_VOL", "28");
  set_key(c, "SB16_FM_VOL", "28");

  /* Organya on a slow CPU: enable the v1.0.8 pre-render PCM disk cache.
   * Compare the config's actual AUDIO_BACKEND value (a runtime read) rather
   * than the local `backend` pick: the auto-recommend path above only ever
   * yields wb/opl3/auto, so this fires only if a caller has set the backend
   * to "organya" in `c` before recommend_apply runs (e.g. a future manual-
   * override flow). Reading from the config also avoids the -Wstring-compare
   * provably-unequal warning the local short-literal pick would trip. */
  {
    int bi = scfg_index("AUDIO_BACKEND");
    const char *sel = (bi >= 0) ? scfg_get(c, bi) : "";
    if (sel && strcmp(sel, "organya") == 0 && p->cpu_class != CPU_586)
      set_key(c, "ORG_PRERENDER", "1");
  }

  if (rationale && cap > 0)
  {
    int perf_n = perf[0] - '0';
    const char *perf_name = (perf_n == 0) ? "faithful"
                          : (perf_n == 1) ? "smooth" : "fast";
    snprintf(rationale, (size_t)cap,
             "%s; %s; perf=%s (%s); fixed-timestep on",
             p->cpu_desc[0] ? p->cpu_desc : cpu_class_name(p->cpu_class),
             why, perf, perf_name);
    rationale[cap - 1] = '\0';
  }
}

int recommend_org_prerender(scfg_t *c, const sysprofile_t *p)
{
  int bi = scfg_index("AUDIO_BACKEND");
  int pi = scfg_index("ORG_PRERENDER");
  const char *bk;

  if (bi < 0 || pi < 0)
    return 0;
  bk = scfg_get(c, bi);
  if (strcmp(bk, "organya") != 0)
    return 0;                       /* only the Organya synth path is slow */
  if (p->cpu_class == CPU_586)
    return 0;                       /* Pentium-class handles live Organya  */
  if (strcmp(scfg_get(c, pi), "1") == 0)
    return 0;                       /* already enabled -- nothing to do    */

  set_key(c, "ORG_PRERENDER", "1");
  return 1;
}

/* T67: recommend_wb_risky() removed -- the WaveBlaster freeze it warned about is
 * fixed (default-on cold-init reorder + SDL pacing); WB is now a supported
 * opt-in backend with no CPU-class risk to advise. */
