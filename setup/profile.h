#ifndef SETUP_PROFILE_H
#define SETUP_PROFILE_H

/*
 * profile.h -- host-system profile gathered by SETUP.EXE and consumed by
 * the recommendation engine. Detection (profile.c) is DOS-specific (CPUID,
 * DPMI, BLASTER, VBE); the struct itself is plain data so recommend.c is
 * host-testable.
 *
 * ASCII-only.
 */

typedef enum
{
  CPU_486_SLOW = 0, /* 486DX2-50 and below: heaviest fidelity budget */
  CPU_486_MID,      /* 486DX2-66 / Am5x86-class                      */
  CPU_586           /* Pentium / OverDrive and faster                */
} cpu_class_t;

/* Where the effective DMA channel(s) in the profile came from. The SB16
 * self-reports its DMA config in mixer register 0x81; a PicoGUS in SB mode
 * does NOT emulate that register (reads back 0), so its DMA is only knowable
 * from the ambient BLASTER env -- hence the "DMA shown 0" oddity this tracks.
 * The UI shows the source so the operator can tell a card-confirmed channel
 * from one merely seeded off BLASTER. */
typedef enum
{
  SND_DMA_SRC_NONE = 0, /* no DMA known (no BLASTER D/H field, card silent)  */
  SND_DMA_SRC_CARD,     /* read from the SB16 mixer DMA register (0x81)      */
  SND_DMA_SRC_BLASTER   /* seeded from the ambient BLASTER env D/H field     */
} snd_dma_src_t;

typedef struct
{
  /* CPU */
  cpu_class_t cpu_class;
  int         has_fpu;        /* 1 if an x87 FPU is present (required)   */
  int         cpu_mhz_est;    /* rough MHz estimate, 0 if unknown        */
  char        cpu_vendor[16]; /* CPUID vendor string, or "unknown"       */
  char        cpu_desc[40];   /* human label e.g. "486DX2-66"            */

  /* Memory */
  long        xms_free_kb;    /* free extended memory in KB, -1 unknown  */
  long        phys_ram_kb;    /* total PHYSICAL RAM in KB, -1 unknown    */

  /* Sound */
  int         snd_detected;   /* BLASTER parsed / card responded         */
  int         snd_base;       /* e.g. 0x220                              */
  int         snd_irq;        /* e.g. 5                                  */
  int         snd_dma;        /* 8-bit DMA channel                       */
  int         snd_hdma;       /* 16-bit DMA channel                      */
  int         snd_dma_src;    /* snd_dma_src_t: where snd_dma/_hdma came */
  int         snd_type;       /* BLASTER Tn type code, 0 if absent       */
  int         dsp_major;      /* SB DSP version major, 0 if no response  */
  int         dsp_minor;
  int         has_waveblaster;/* MPU-401 + DRR responded                 */
  int         has_opl3;       /* YMF262 timer test passed                */

  /* Video */
  int         vbe_version;     /* BCD, e.g. 0x0200 == VBE 2.0; 0 unknown  */
  int         vbe_lfb;         /* 1 if a linear framebuffer mode exists   */
  char        video_desc[40];  /* OEM / chip string                       */
  int         video_speed_kbs; /* timed video-mem fill, KB/s; 0 unknown   */
  int         video_speed_path;/* 0 none / 1 VBE-LFB / 2 mode13h / 3 B800 */
} sysprofile_t;

/* Fill p by probing the host. Safe to call once at startup. */
void profile_detect(sysprofile_t *p);

/* Human label for a CPU class. */
const char *cpu_class_name(cpu_class_t c);

/* Short tag ("card" / "BLASTER" / "none") for a snd_dma_src_t value. */
const char *snd_dma_src_name(int src);

/* Dump the detected profile to LOGS\PROFILE.LOG (one key=value per line,
 * per-line fsync; creates LOGS\ if absent). Lets a real-HW iter capture the
 * displayed values deterministically. Returns 0 on success, -1 on open error. */
int profile_write_log(const sysprofile_t *p);

#endif /* SETUP_PROFILE_H */
