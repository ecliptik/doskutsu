#ifndef SETUP_JOYPORT_H
#define SETUP_JOYPORT_H

/*
 * joyport.h -- PC gameport sampler for SETUP's joystick calibrate screen
 * (Phase 3, plan 3.5c). Standalone, no SDL. AXES are read with the bounded
 * DIRECT-PORT (0x201) discharge-count primitive that input-sdl's SDL3-DOS
 * driver uses in-game (SDL patch 0103) -- reproduced byte-for-byte so the
 * stored JOY_CAL 6-tuple is in the IDENTICAL poll-count domain the game reads
 * at runtime. (Supersedes the earlier BIOS INT 15h read, which timed out on a
 * 2-axis stick's open axes = the per-flip fps cost. team-lead DECISION 1.)
 * BUTTONS come from port 0x201 (bits 4-7, active low).
 *
 * On a non-DJGPP host build this is a deterministic stub (mid-domain axes, no
 * buttons, "present") so the screen flow stays exercisable off-target.
 * ASCII-only.
 */

/* Sample the gameport once. *ax / *ay receive the per-axis DIRECT-PORT poll
 * COUNTS (~0..3000; smaller/larger = pot toward one end or the other). A count
 * of -1 means that axis's one-shot never discharged within the cap = open / no
 * stick. *buttons receives a 4-bit mask, bit b set = button b pressed (0-based;
 * the UI shows 1-based). Returns 1 when BOTH axes read a valid count (a 2-axis
 * stick is attached), 0 otherwise -- the caller aborts calibration rather than
 * storing a -1. Any pointer may be NULL. */
int joyport_sample(int *ax, int *ay, unsigned *buttons);

#endif /* SETUP_JOYPORT_H */
