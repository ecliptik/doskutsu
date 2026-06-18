/*
 * joyport.c -- one-shot PC gameport sampler (see joyport.h).
 *
 * AXES are read with a bounded DIRECT-PORT (0x201) discharge-count loop -- the
 * EXACT primitive input-sdl's SDL3-DOS driver uses in-game (SDL patch 0103,
 * ReadGameportAxesDirect). Sampling through the identical loop/cap/bit-masks
 * means the JOY_CAL 6-tuple SETUP stores is in the SAME poll-count domain the
 * game reads at runtime -- any divergence desyncs the domain and the in-game
 * axis collapses to center. (This SUPERSEDES the earlier BIOS INT 15h/AH=84h
 * axis read: on a 2-axis stick the BIOS read times out polling the 2 open axes,
 * ~82 ms/flip -- the fps showstopper p3-sdl's direct read fixes. team-lead
 * DECISION 1 + p3-sdl primitive, 2026-06-17.)
 *
 * Values are raw poll COUNTS (~0..GAMEPORT_DIRECT_CAP), NOT BIOS AX/BX. A -1 on
 * either axis means the one-shot never discharged within the cap = that axis is
 * open / no stick attached; joyport_sample reports "not present" so the caller
 * aborts calibration instead of storing -1.
 *
 * cli/sti around the count loop is REQUIRED: under PIXTONE_IRQ_MIX the SB16 ISR
 * runs the mixer and would preempt mid-count, corrupting the discharge timing.
 * Protected-mode cli/sti work because CWSDPMI grants IOPL (the same reason the
 * port I/O already works). The primitive is kept byte-for-byte identical to the
 * SDL side -- do NOT "tidy" the loop, cap, or masks without re-syncing both.
 *
 * BUTTONS are the active-low high nibble (bits 4-7) of port 0x201, matching the
 * driver's button read (level comparators, not one-shots -- no trigger needed).
 *
 * On a non-DJGPP host build this is a deterministic stub (mid-domain axes, no
 * buttons, "present") so the screen flow stays exercisable off-target.
 * ASCII-only.
 */

#include "joyport.h"

#if defined(__DJGPP__)

#include <pc.h>    /* inportb / outportb -- 0x201 direct port I/O */

#define GAMEPORT             0x201
#define GAMEPORT_AXIS_X      0x01   /* bit 0: joystick-1 X one-shot */
#define GAMEPORT_AXIS_Y      0x02   /* bit 1: joystick-1 Y one-shot */
#define GAMEPORT_DIRECT_CAP  3000   /* hard cap; open/stuck axis -> -1 */

/* VERBATIM from SDL patch 0103 (ReadGameportAxesDirect) -- the in-game read.
 * Keep identical to preserve the shared poll-count domain (task #24). */
static void ReadGameportAxesDirect(int *axis_x, int *axis_y)
{
    unsigned long cx = 0, cy = 0;
    unsigned long guard = 0;
    unsigned char bits;

    __asm__ __volatile__("cli");
    outportb(GAMEPORT, 0); /* re-trigger all one-shots */
    do {
        bits = (unsigned char)inportb(GAMEPORT);
        if (bits & GAMEPORT_AXIS_X) { cx++; }
        if (bits & GAMEPORT_AXIS_Y) { cy++; }
        ++guard;
    } while ((bits & (GAMEPORT_AXIS_X | GAMEPORT_AXIS_Y)) && guard < GAMEPORT_DIRECT_CAP);
    __asm__ __volatile__("sti");

    *axis_x = (bits & GAMEPORT_AXIS_X) ? -1 : (int)cx;
    *axis_y = (bits & GAMEPORT_AXIS_Y) ? -1 : (int)cy;
}

int joyport_sample(int *ax, int *ay, unsigned *buttons)
{
  int x = -1, y = -1;
  unsigned char s;

  ReadGameportAxesDirect(&x, &y);
  if (ax) *ax = x;
  if (ay) *ay = y;

  /* Buttons: a plain level read of the high nibble (active low). The axis read
   * above re-triggered the one-shots, but the button comparators are unaffected,
   * so a fresh inportb here gives the current button state. */
  s = inportb(GAMEPORT);
  if (buttons) *buttons = (unsigned)((~(s >> 4)) & 0x0f); /* active low, bits 4-7 */

  /* "present" = both axes discharged within the cap (a real 2-axis stick). A -1
   * on either axis = open one-shot = no stick; the cal screen must abort, not
   * store -1 (which input-sdl's validity gate would reject anyway). */
  return (x >= 0 && y >= 0);
}

#else /* host stub: mid-domain axes (count domain ~0..3000), no buttons, present */

int joyport_sample(int *ax, int *ay, unsigned *buttons)
{
  if (ax) *ax = 1500;
  if (ay) *ay = 1500;
  if (buttons) *buttons = 0;
  return 1;
}

#endif
