#ifndef SETUP_RECOMMEND_H
#define SETUP_RECOMMEND_H

/*
 * recommend.h -- map a detected system profile to recommended DOSKUTSU
 * settings. Pure logic (no DOS / SDL deps) so it is host-unit-testable.
 * Seeded from the canonical cross-CPU benchmarks + the v1.0.8 Organya
 * findings (docs/internal/CURRENT-STATE.md, v1_0_8_shipped).
 */

#include "setupcfg.h"
#include "profile.h"

/* Overwrite c with the recommended values for profile p. The caller shows
 * these for review/override before saving (recommend-and-confirm). A short
 * human-readable rationale is written into rationale[] (cap chars). */
void recommend_apply(scfg_t *c, const sysprofile_t *p, char *rationale, int cap);

/* Auto-suggest convenience: if c's AUDIO_BACKEND is "organya" AND the CPU is
 * slower than Pentium-class (cpu_class != CPU_586), enable ORG_PRERENDER=1 --
 * the v1.0.8 PCM disk cache that makes Organya real-time on a slow 486.
 * Recommend-and-confirm: the user can still toggle ORG_PRERENDER back off, and
 * this never forces it OFF (a deliberate "prerender on a fast CPU" choice is
 * preserved). Returns 1 iff it just enabled prerender; 0 otherwise (backend
 * not organya, Pentium-class CPU, or prerender already on). Pure logic, so the
 * SETUP Sound editor and the host unit tests both call it. */
int recommend_org_prerender(scfg_t *c, const sysprofile_t *p);

/* T67: the WaveBlaster freeze-risk advisory (recommend_wb_risky +
 * RECOMMEND_WB_RISK_WARNING) was removed -- the hot-MPU ISA stall it warned
 * about is fixed by the default-on cold-init reorder (nx 0218/0220) + SDL
 * pacing (SDL/0097/0098). WaveBlaster is now a supported opt-in backend, no
 * longer CPU-class-risky, so there is nothing to advise. */

#endif /* SETUP_RECOMMEND_H */
