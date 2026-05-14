/* org2mid -- Cave Story Organya (.org) -> Standard MIDI File (.mid) converter.
 *
 * Linux host-side tool for the doskutsu port. Not a DOS binary; not linked
 * against the engine. C99, no dependencies beyond libc.
 *
 * Design: docs/internal/WAVE-41-CONVERTER-DESIGN.md (gitignored).
 * Ground-truth Organya spec: vendor/nxengine-evo/src/sound/Organya.cpp:135-177
 * (Song::Load).
 *
 * Output target: SMF Format 1, PPQ=48 fixed, 10 MTrks (1 metadata + 8
 * melody + 1 drum). Compatible with the engine's MidiScheduler
 * (vendor/nxengine-evo/src/sound/MidiScheduler.{h,cpp}).
 *
 * Invocation:
 *   org2mid [--loop-strategy=N] [--no-pipi-swap] [--gm-table=v1|v2] [--force] <input.org> <output.mid>
 *
 * Exit codes:
 *   0  on successful conversion
 *   1  on malformed input or other operational failure
 *   2  on CLI argument error
 *
 * Copyright (C) 2026 Micheal Waltz / doskutsu authors.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* ===================================================================== */
/* Constants -- design doc sec. 2.2, 3.1, 3.2, 4.1                       */
/* ===================================================================== */

#define ORG_SIG_LEN   6
#define ORG_HEADER_SZ 18
#define ORG_N_INSTR   16
#define ORG_N_MELODY  8
#define ORG_N_DRUM    8     /* instr 8..15 */
#define ORG_INSTR_REC 6     /* bytes per instrument record */
#define ORG_EVENT_REC 8     /* bytes per event (5 parallel arrays) */

#define SMF_PPQ           48
#define SMF_FORMAT        1
#define SMF_NTRKS         10  /* 1 metadata + 8 melody + 1 drum */
#define ORG_TO_MIDI_NOTE_OFFSET 24

#define DEFAULT_LOOP_REPS 4

/* GM-patch table variants. Selected via --gm-table=v1|v2 CLI flag;
 * default v2 (wave-42; addresses operator wave-41 "thumps and cowbell"
 * aesthetic feedback by replacing the most-likely-blaty patches with
 * mellower alternatives on the DreamBlaster S2 wavetable). v1
 * preserved as the original task #2 design-doc mapping for A/B
 * comparison and as a stable-baseline reference. */
typedef enum {
    GM_TABLE_V1 = 1,
    GM_TABLE_V2 = 2,
    GM_TABLE_DEFAULT = GM_TABLE_V2
} GmTableVariant;

/* === v1 (original; from design doc sec. 3.1) ============================ */

/* Melody instrument -> GM program map (8 entries). */
static const uint8_t MELODY_GM_V1[ORG_N_MELODY] = {
    80,  /* Lead 1 Square      */
    81,  /* Lead 2 Sawtooth    */
    56,  /* Trumpet            */
    73,  /* Flute              */
    88,  /* Pad 1 New Age      */
    25,  /* Steel Guitar       */
    33,  /* Electric Bass      */
    5    /* Electric Piano 2   */
};

/* Pipi-swap (sustained) alternate GM programs. */
static const uint8_t MELODY_GM_V1_PIPI[ORG_N_MELODY] = {
    84, 85, 60, 75, 89, 27, 34, 4
};

/* === v2 (wave-42; aesthetic retune) ==================================== */

/* Per-track rationale for v2 (operator wave-41 "thumps and cowbell"
 * feedback; flush-instr WAVE-41-LOG-ANALYSIS confirmed zero dispatch
 * errors -- bug is purely GM-mapping aesthetic). Each change explicitly
 * documents WHY:
 *
 *   Track 0: 80 -> 80 (KEEP). Square lead is canonical chiptune sound
 *            for the main melody; DreamBlaster S2 renders it well.
 *
 *   Track 1: 81 -> 82 (Lead 2 Saw -> Lead 3 Calliope). Saw is bright
 *            and aggressive; Calliope is softer + sustains better for
 *            Cave Story's harmony role.
 *
 *   Track 2: 56 -> 65 (Trumpet -> Alto Sax). Trumpet on the S2's GM
 *            bank is the most-likely "blaty" culprit per operator
 *            feedback; Alto Sax is in the same wind family but
 *            inherently mellower.
 *
 *   Track 3: 73 -> 73 (KEEP). Flute is reliably soft + works for
 *            Cave Story's airy lead role.
 *
 *   Track 4: 88 -> 89 (Pad 1 New Age -> Pad 2 Warm). Pad 1 has a
 *            metallic-bell-ish attack on some GM banks (likely the
 *            "cowbell" perception); Pad 2 Warm is purely sustained.
 *
 *   Track 5: 25 -> 0 (Steel Guitar -> Acoustic Grand Piano). Steel
 *            Guitar on S2 may render with metallic-percussion
 *            character (second cowbell suspect); acoustic piano is
 *            universal + reliably non-metallic.
 *
 *   Track 6: 33 -> 32 (Electric Bass -> Acoustic Bass). E-bass on
 *            S2 has some synth-percussion edge; acoustic bass is
 *            warmer + better matches Cave Story's chiptune aesthetic.
 *
 *   Track 7: 5 -> 4 (E.Piano 2 -> E.Piano 1). EP1 is the more standard
 *            DX7-style Rhodes; EP2 has a sharper attack that may
 *            contribute to "thump" perception on dense chord stabs.
 *
 * The v2 mapping is a TARGETED conservative retune (4 of 8 tracks
 * keep their v1 patch). If operator listening confirms v2 sounds
 * "better but X is still wrong", v3 can iterate on the specific
 * remaining tracks. Drums NOT changed in v2 -- "thumps" perception
 * on drums is harder to attribute without operator's ear on the
 * specific drum slot affected. */
static const uint8_t MELODY_GM_V2[ORG_N_MELODY] = {
    80,  /* Lead 1 Square              (UNCHANGED from v1)        */
    82,  /* Lead 3 Calliope            (was 81 Lead 2 Sawtooth)   */
    65,  /* Alto Sax                   (was 56 Trumpet)           */
    73,  /* Flute                      (UNCHANGED from v1)        */
    89,  /* Pad 2 (Warm)               (was 88 Pad 1 New Age)     */
    0,   /* Acoustic Grand Piano       (was 25 Steel Guitar)      */
    32,  /* Acoustic Bass              (was 33 Electric Bass)     */
    4    /* Electric Piano 1           (was 5 Electric Piano 2)   */
};

/* v2 pipi-swap: mirrors v2 base table but biases toward MORE sustain.
 *   80 (Square Lead)        -> 84 (Lead 5 Charang) more pad-ish lead
 *   82 (Calliope)           -> 82 KEEP (already sustained)
 *   65 (Alto Sax)           -> 67 (Tenor Sax) more sustained breath
 *   73 (Flute)              -> 75 (Pan Flute) more reverb-y
 *   89 (Pad Warm)           -> 91 (Pad Choir) more vocal sustain
 *   0  (Acoustic Grand)     -> 1  (Bright Acoustic) similar but with
 *                                  longer decay-tail on S2
 *   32 (Acoustic Bass)      -> 39 (Synth Bass 2) more pad-ish bass
 *   4  (E.Piano 1)          -> 5  (E.Piano 2) the reverse of base table swap;
 *                                  EP2 on pipi gives sustained held chord */
static const uint8_t MELODY_GM_V2_PIPI[ORG_N_MELODY] = {
    84, 82, 67, 75, 91, 1, 39, 5
};

/* Org drum slot -> GM percussion key (channel 9). UNCHANGED v1 -> v2.
 * Slots 6 + 7 are SND_NULL across the entire Cave Story corpus
 * (verified by parsing all 42 .org files at design time). We still
 * provide a stub mapping in case future content uses them. Drum
 * retune deferred to v3 if operator feedback specifies a drum slot. */
static const uint8_t DRUM_GM_KEY[ORG_N_DRUM] = {
    35,  /* BASS       */
    38,  /* SNARE      */
    42,  /* HI-CLOSE   */
    46,  /* HI-OPEN    */
    45,  /* TOM        */
    39,  /* PERCUSSION */
    37,  /* SND_NULL (slot 6) -- side stick stub for forward compat */
    49   /* SND_NULL (slot 7) -- crash cymbal stub */
};

/* === Variant selection ================================================== */

/* Set by --gm-table=v1|v2 CLI option; defaults to v2 per GM_TABLE_DEFAULT. */
static GmTableVariant g_gm_table = GM_TABLE_DEFAULT;

/* Lookup helper -- returns base or pipi-swap table per current
 * g_gm_table setting. */
static const uint8_t *_current_melody_gm(void) {
    return (g_gm_table == GM_TABLE_V1) ? MELODY_GM_V1 : MELODY_GM_V2;
}
static const uint8_t *_current_melody_gm_pipi(void) {
    return (g_gm_table == GM_TABLE_V1) ? MELODY_GM_V1_PIPI : MELODY_GM_V2_PIPI;
}

/* ===================================================================== */
/* Growable byte buffer                                                  */
/* ===================================================================== */

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} Buf;

static void buf_init(Buf *b) {
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_free(Buf *b) {
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(Buf *b, size_t want) {
    if (b->cap >= want) return;
    size_t newcap = b->cap ? b->cap : 256;
    while (newcap < want) newcap *= 2;
    uint8_t *p = (uint8_t *)realloc(b->buf, newcap);
    if (!p) {
        fprintf(stderr, "org2mid: out of memory (wanted %zu bytes)\n", newcap);
        exit(1);
    }
    b->buf = p;
    b->cap = newcap;
}

static void buf_put_u8(Buf *b, uint8_t v) {
    buf_grow(b, b->len + 1);
    b->buf[b->len++] = v;
}

static void buf_put_u16be(Buf *b, uint16_t v) {
    buf_grow(b, b->len + 2);
    b->buf[b->len++] = (uint8_t)(v >> 8);
    b->buf[b->len++] = (uint8_t)(v & 0xFF);
}

static void buf_put_u32be(Buf *b, uint32_t v) {
    buf_grow(b, b->len + 4);
    b->buf[b->len++] = (uint8_t)((v >> 24) & 0xFF);
    b->buf[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->buf[b->len++] = (uint8_t)((v >>  8) & 0xFF);
    b->buf[b->len++] = (uint8_t)( v        & 0xFF);
}

static void buf_put_bytes(Buf *b, const void *src, size_t n) {
    buf_grow(b, b->len + n);
    memcpy(b->buf + b->len, src, n);
    b->len += n;
}

/* SMF variable-length quantity encoder. 1..4 bytes per the SMF spec. */
static void buf_put_vlq(Buf *b, uint32_t v) {
    if (v >= (1u << 28)) {
        /* SMF spec caps VLQ at 28 bits / 0x0FFFFFFF. Our tick math
         * can't reach this (max ~110k ticks per the design's sec. 5.4
         * analysis) but the check is defensive. */
        fprintf(stderr, "org2mid: VLQ value %u out of range\n", v);
        exit(1);
    }
    uint8_t bytes[4];
    int n = 0;
    bytes[n++] = (uint8_t)(v & 0x7F);
    v >>= 7;
    while (v) {
        bytes[n++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    /* Emit MSB-first (high-bit set on all but last). */
    for (int i = n - 1; i >= 0; --i) {
        buf_put_u8(b, bytes[i]);
    }
}

/* ===================================================================== */
/* Little-endian readers for .org input                                  */
/* ===================================================================== */

static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ===================================================================== */
/* Organya in-memory representation                                      */
/* ===================================================================== */

typedef struct {
    uint16_t  tuning;
    uint8_t   wave;
    uint8_t   pipi;
    uint16_t  n_events;
    uint32_t *positions;  /* length n_events */
    uint8_t  *notes;
    uint8_t  *lengths;
    uint8_t  *volumes;
    uint8_t  *pannings;
} OrgInstr;

typedef struct {
    uint16_t ms_per_beat;
    uint8_t  steps_per_bar;
    uint8_t  beats_per_step;
    uint32_t loop_start;
    uint32_t loop_end;
    OrgInstr instr[ORG_N_INSTR];
} OrgSong;

static void song_free(OrgSong *s) {
    for (int i = 0; i < ORG_N_INSTR; ++i) {
        free(s->instr[i].positions);
        free(s->instr[i].notes);
        free(s->instr[i].lengths);
        free(s->instr[i].volumes);
        free(s->instr[i].pannings);
        memset(&s->instr[i], 0, sizeof(OrgInstr));
    }
}

/* Returns 0 on success, 1 on parse failure (caller exits). */
static int parse_org(const uint8_t *buf, size_t buflen, const char *path,
                     OrgSong *out)
{
    memset(out, 0, sizeof(*out));

    if (buflen < ORG_HEADER_SZ) {
        fprintf(stderr, "org2mid: %s: truncated header (only %zu bytes; "
                        "expected >= %d)\n", path, buflen, ORG_HEADER_SZ);
        return 1;
    }
    if (memcmp(buf, "Org-02", ORG_SIG_LEN) != 0) {
        fprintf(stderr, "org2mid: %s: wrong signature; expected 'Org-02' "
                        "got '%c%c%c%c%c%c'\n", path,
                buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
        return 1;
    }

    out->ms_per_beat    = rd_u16le(buf + 6);
    out->steps_per_bar  = buf[8];
    out->beats_per_step = buf[9];
    out->loop_start     = rd_u32le(buf + 10);
    out->loop_end       = rd_u32le(buf + 14);

    if (out->ms_per_beat == 0) {
        fprintf(stderr, "org2mid: %s: ms_per_beat is zero (div-by-zero)\n", path);
        return 1;
    }
    if (out->beats_per_step == 0) {
        fprintf(stderr, "org2mid: %s: beats_per_step is zero (div-by-zero "
                        "in PPQ scaler)\n", path);
        return 1;
    }

    /* Instrument records: 16 records * 6 bytes at offset 18. */
    size_t cursor = ORG_HEADER_SZ;
    if (buflen < cursor + ORG_N_INSTR * ORG_INSTR_REC) {
        fprintf(stderr, "org2mid: %s: truncated at instrument records "
                        "(have %zu bytes; need %zu)\n",
                path, buflen, cursor + ORG_N_INSTR * ORG_INSTR_REC);
        return 1;
    }

    /* Per-record layout (6 bytes), per Organya.cpp:157:
     *   [0..1] tuning  (u16 LE)
     *   [2]    wave    (u8)
     *   [3]    pipi    (u8)         -- note: the in-memory C++ struct has
     *                                  constant fields (wave_step=1,
     *                                  last_note=255, playing=false) sitting
     *                                  BETWEEN wave and pipi, but those are
     *                                  initialized in-code, NOT read from
     *                                  disk. The on-disk record is dense.
     *   [4..5] n_events (u16 LE)
     */
    for (int i = 0; i < ORG_N_INSTR; ++i) {
        OrgInstr *ins = &out->instr[i];
        ins->tuning   = rd_u16le(buf + cursor + 0);
        ins->wave     = buf[cursor + 2];
        ins->pipi     = buf[cursor + 3];
        ins->n_events = rd_u16le(buf + cursor + 4);
        cursor += ORG_INSTR_REC;
    }

    /* Per-instrument event arrays (5 parallel arrays of n_events each). */
    for (int i = 0; i < ORG_N_INSTR; ++i) {
        OrgInstr *ins = &out->instr[i];
        if (ins->n_events == 0) continue;
        size_t need = (size_t)ins->n_events * ORG_EVENT_REC;
        if (buflen < cursor + need) {
            fprintf(stderr, "org2mid: %s: truncated event data at "
                            "instrument %d (have %zu bytes; need %zu)\n",
                    path, i, buflen, cursor + need);
            return 1;
        }
        ins->positions = (uint32_t *)malloc(sizeof(uint32_t) * ins->n_events);
        ins->notes     = (uint8_t  *)malloc(sizeof(uint8_t ) * ins->n_events);
        ins->lengths   = (uint8_t  *)malloc(sizeof(uint8_t ) * ins->n_events);
        ins->volumes   = (uint8_t  *)malloc(sizeof(uint8_t ) * ins->n_events);
        ins->pannings  = (uint8_t  *)malloc(sizeof(uint8_t ) * ins->n_events);
        if (!ins->positions || !ins->notes || !ins->lengths ||
            !ins->volumes || !ins->pannings) {
            fprintf(stderr, "org2mid: out of memory parsing %s\n", path);
            return 1;
        }
        for (uint16_t e = 0; e < ins->n_events; ++e) {
            ins->positions[e] = rd_u32le(buf + cursor + e * 4);
        }
        cursor += (size_t)ins->n_events * 4;
        memcpy(ins->notes,    buf + cursor, ins->n_events); cursor += ins->n_events;
        memcpy(ins->lengths,  buf + cursor, ins->n_events); cursor += ins->n_events;
        memcpy(ins->volumes,  buf + cursor, ins->n_events); cursor += ins->n_events;
        memcpy(ins->pannings, buf + cursor, ins->n_events); cursor += ins->n_events;
    }

    return 0;
}

/* ===================================================================== */
/* SMF event-list-per-track + sort                                       */
/* ===================================================================== */

/* Per-track event during emission. We build an array, sort by abs_tick
 * (ties broken by `order`), then emit deltas. */
typedef struct {
    uint32_t abs_tick;
    uint8_t  order;     /* tiebreaker for same-tick events */
    uint8_t  status;    /* MIDI status byte (incl. channel), or 0xFF for meta */
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  flags;     /* bit 0: has data2 (3-byte channel event)
                         * bit 1: meta event (use meta_type + meta_data) */
    uint8_t  meta_type;
    const uint8_t *meta_data;
    uint32_t meta_len;
} TrkEvent;

#define FLG_DATA2 0x01
#define FLG_META  0x02

#define ORDER_TRACKNAME 0
#define ORDER_PROGRAM   1
#define ORDER_LOOPSTART 2
#define ORDER_LOOPEND   2
#define ORDER_CC        3
#define ORDER_NOTE_OFF  4
#define ORDER_NOTE_ON   5
#define ORDER_EOT       7

typedef struct {
    TrkEvent *ev;
    size_t    len;
    size_t    cap;
} TrkEvents;

static void trk_init(TrkEvents *t) {
    t->ev = NULL;
    t->len = 0;
    t->cap = 0;
}

static void trk_free(TrkEvents *t) {
    free(t->ev);
    t->ev = NULL;
    t->len = 0;
    t->cap = 0;
}

static TrkEvent *trk_alloc(TrkEvents *t) {
    if (t->len == t->cap) {
        size_t newcap = t->cap ? t->cap * 2 : 64;
        TrkEvent *p = (TrkEvent *)realloc(t->ev, newcap * sizeof(TrkEvent));
        if (!p) { fprintf(stderr, "org2mid: out of memory\n"); exit(1); }
        t->ev  = p;
        t->cap = newcap;
    }
    TrkEvent *e = &t->ev[t->len++];
    memset(e, 0, sizeof(*e));
    return e;
}

/* qsort comparator: (abs_tick, order). */
static int trk_cmp(const void *a, const void *b) {
    const TrkEvent *x = (const TrkEvent *)a;
    const TrkEvent *y = (const TrkEvent *)b;
    if (x->abs_tick != y->abs_tick) {
        return (x->abs_tick < y->abs_tick) ? -1 : 1;
    }
    if (x->order != y->order) {
        return (x->order < y->order) ? -1 : 1;
    }
    return 0;
}

/* Emit one TrkEvent's bytes into the buffer (status + data; no delta time). */
static void emit_event_bytes(Buf *b, const TrkEvent *e) {
    if (e->flags & FLG_META) {
        buf_put_u8(b, 0xFF);
        buf_put_u8(b, e->meta_type);
        buf_put_vlq(b, e->meta_len);
        buf_put_bytes(b, e->meta_data, e->meta_len);
    } else {
        buf_put_u8(b, e->status);
        buf_put_u8(b, e->data1);
        if (e->flags & FLG_DATA2) {
            buf_put_u8(b, e->data2);
        }
    }
}

/* Sort the track's events, convert to delta-time encoding, write MTrk chunk
 * (header + length + payload) into `out`. */
static void emit_mtrk(Buf *out, TrkEvents *t) {
    qsort(t->ev, t->len, sizeof(TrkEvent), trk_cmp);

    /* Build the payload into a temp buffer first so we know its length. */
    Buf payload;
    buf_init(&payload);

    uint32_t last_tick = 0;
    for (size_t i = 0; i < t->len; ++i) {
        const TrkEvent *e = &t->ev[i];
        uint32_t delta = e->abs_tick - last_tick;
        last_tick = e->abs_tick;
        buf_put_vlq(&payload, delta);
        emit_event_bytes(&payload, e);
    }

    /* "MTrk" + length-BE32 + payload. */
    buf_put_bytes(out, "MTrk", 4);
    buf_put_u32be(out, (uint32_t)payload.len);
    buf_put_bytes(out, payload.buf, payload.len);

    buf_free(&payload);
}

/* ===================================================================== */
/* Conversion: Organya tick -> MIDI tick                                 */
/* ===================================================================== */

static uint32_t midi_tick_of(uint32_t org_tick, uint8_t bps) {
    /* MIDI ticks per Organya tick = 48 / bps. Integer for bps in {3,4,6}. */
    return org_tick * (SMF_PPQ / (uint32_t)bps);
}

/* ===================================================================== */
/* Per-track emission                                                    */
/* ===================================================================== */

/* Append one melody event (note_on + note_off + pan-CC-on-change) into
 * the per-channel TrkEvents for a single iteration through the event list.
 * `tick_offset` shifts all events by a fixed Organya-tick amount (used
 * for loop-body replication). `last_pan` is the per-channel pan cache;
 * pass a pointer so we can update across calls. */
static void append_melody_event(TrkEvents *t, uint8_t channel,
                                uint32_t org_pos, uint8_t note,
                                uint8_t length, uint8_t volume, uint8_t panning,
                                uint8_t bps, uint32_t tick_offset,
                                int *last_pan)
{
    /* Note 255 sentinel: "no note this tick". Skip note on/off; pan still
     * applies if encoded -- but in practice Cave Story .orgs don't emit
     * pan-only events. Skip everything for note==255 to match Organya
     * playback semantics. */
    if (note == 255) return;

    uint32_t abs_org_on  = org_pos + tick_offset;
    uint32_t abs_org_off = abs_org_on + length;
    uint32_t midi_on  = midi_tick_of(abs_org_on,  bps);
    uint32_t midi_off = midi_tick_of(abs_org_off, bps);

    /* Pan CC 10 on change. */
    int new_pan = (panning <= 12) ? ((int)panning * 127) / 12 : 64;
    if (*last_pan != new_pan) {
        TrkEvent *p = trk_alloc(t);
        p->abs_tick = midi_on;
        p->order    = ORDER_CC;
        p->status   = (uint8_t)(0xB0 | (channel & 0x0F));
        p->data1    = 0x0A;  /* CC 10 = Pan */
        p->data2    = (uint8_t)new_pan;
        p->flags    = FLG_DATA2;
        *last_pan = new_pan;
    }

    uint8_t midi_note = (uint8_t)(note + ORG_TO_MIDI_NOTE_OFFSET);
    /* Velocity: linear scale 0..255 -> 0..127, clamp to [1,127] for non-zero
     * volume; if volume is exactly 0, suppress the note entirely (it's
     * inaudible and 0 would alias to note-off in MIDI's running-status). */
    int velocity = ((int)volume * 127) / 255;
    if (volume == 0) return;
    if (velocity < 1)   velocity = 1;
    if (velocity > 127) velocity = 127;

    TrkEvent *on = trk_alloc(t);
    on->abs_tick = midi_on;
    on->order    = ORDER_NOTE_ON;
    on->status   = (uint8_t)(0x90 | (channel & 0x0F));
    on->data1    = midi_note;
    on->data2    = (uint8_t)velocity;
    on->flags    = FLG_DATA2;

    TrkEvent *off = trk_alloc(t);
    off->abs_tick = midi_off;
    off->order    = ORDER_NOTE_OFF;
    off->status   = (uint8_t)(0x80 | (channel & 0x0F));
    off->data1    = midi_note;
    off->data2    = 64;  /* note-off release velocity; conventional 64. */
    off->flags    = FLG_DATA2;
}

/* Append one drum event. Drum-channel = MIDI 9. Org drum slots 0..7 map
 * to fixed GM percussion keys per DRUM_GM_KEY[]. Note-off emitted at
 * same tick as note-on (zero delta) per design sec. 4.6. */
static void append_drum_event(TrkEvents *t, uint8_t drum_slot,
                              uint32_t org_pos, uint8_t note, uint8_t volume,
                              uint8_t bps, uint32_t tick_offset)
{
    if (note == 255) return;
    if (volume == 0) return;

    uint32_t abs_org_on = org_pos + tick_offset;
    uint32_t midi_on    = midi_tick_of(abs_org_on, bps);

    uint8_t drum_key = DRUM_GM_KEY[drum_slot & 0x07];

    int velocity = ((int)volume * 127) / 255;
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;

    TrkEvent *on = trk_alloc(t);
    on->abs_tick = midi_on;
    on->order    = ORDER_NOTE_ON;
    on->status   = 0x99;  /* note_on channel 9 */
    on->data1    = drum_key;
    on->data2    = (uint8_t)velocity;
    on->flags    = FLG_DATA2;

    TrkEvent *off = trk_alloc(t);
    off->abs_tick = midi_on;  /* same tick (zero delta) */
    off->order    = ORDER_NOTE_OFF;
    off->status   = 0x89;
    off->data1    = drum_key;
    off->data2    = 64;
    off->flags    = FLG_DATA2;
}

/* Build the metadata MTrk (track 0): tempo, time-sig, optional loop
 * markers, EOT. */
static void build_metadata_track(TrkEvents *t, const OrgSong *s)
{
    uint32_t tempo_us = (uint32_t)s->beats_per_step *
                        (uint32_t)s->ms_per_beat * 1000u;

    /* Tempo meta at tick 0. Payload = 3 bytes BE u24. */
    static uint8_t tempo_buf[3];
    tempo_buf[0] = (uint8_t)((tempo_us >> 16) & 0xFF);
    tempo_buf[1] = (uint8_t)((tempo_us >>  8) & 0xFF);
    tempo_buf[2] = (uint8_t)( tempo_us        & 0xFF);
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_PROGRAM;
        e->flags    = FLG_META;
        e->meta_type = 0x51;
        e->meta_data = tempo_buf;
        e->meta_len  = 3;
    }

    /* Time signature: numerator, log2(denom), MIDI-clocks-per-click,
     * 32nds-per-quarter-note. */
    static uint8_t tsig_buf[4];
    tsig_buf[0] = s->steps_per_bar;  /* numerator */
    tsig_buf[1] = 2;                 /* denominator log2 (= 4) */
    tsig_buf[2] = 24;                /* MIDI clocks per metronome click */
    tsig_buf[3] = 8;                 /* 32nds per quarter */
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_PROGRAM;
        e->flags    = FLG_META;
        e->meta_type = 0x58;
        e->meta_data = tsig_buf;
        e->meta_len  = 4;
    }

    /* Track name. */
    static const uint8_t name[] = "Metadata";
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_TRACKNAME;
        e->flags    = FLG_META;
        e->meta_type = 0x03;
        e->meta_data = name;
        e->meta_len  = sizeof(name) - 1;
    }

    /* loopStart / loopEnd markers (meta text type 0x06). These are silent
     * to current MidiScheduler; v2 scheduler patch can honor them to
     * implement proper Organya looping. Design sec. 4.4. */
    if (s->loop_end > s->loop_start) {
        static const uint8_t loopStart[] = "loopStart";
        static const uint8_t loopEnd[]   = "loopEnd";
        {
            TrkEvent *e = trk_alloc(t);
            e->abs_tick = midi_tick_of(s->loop_start, s->beats_per_step);
            e->order    = ORDER_LOOPSTART;
            e->flags    = FLG_META;
            e->meta_type = 0x06;
            e->meta_data = loopStart;
            e->meta_len  = sizeof(loopStart) - 1;
        }
        {
            TrkEvent *e = trk_alloc(t);
            e->abs_tick = midi_tick_of(s->loop_end, s->beats_per_step);
            e->order    = ORDER_LOOPEND;
            e->flags    = FLG_META;
            e->meta_type = 0x06;
            e->meta_data = loopEnd;
            e->meta_len  = sizeof(loopEnd) - 1;
        }
    }
}

/* Compute the largest MIDI tick across all events of a track, then add
 * EOT meta event just after it. */
static void append_eot(TrkEvents *t) {
    uint32_t maxtick = 0;
    for (size_t i = 0; i < t->len; ++i) {
        if (t->ev[i].abs_tick > maxtick) maxtick = t->ev[i].abs_tick;
    }
    TrkEvent *e = trk_alloc(t);
    e->abs_tick = maxtick;
    e->order    = ORDER_EOT;
    e->flags    = FLG_META;
    e->meta_type = 0x2F;
    e->meta_data = NULL;
    e->meta_len  = 0;
}

/* Emit a melody track. instr_idx in 0..7. */
static void build_melody_track(TrkEvents *t, const OrgSong *s,
                               int instr_idx, int loop_reps, int use_pipi_swap)
{
    const OrgInstr *ins = &s->instr[instr_idx];

    /* Track name. */
    static const uint8_t tn0[] = "Org Melody 0";
    static uint8_t tn[16];
    memcpy(tn, tn0, sizeof(tn0));
    tn[sizeof(tn0) - 2] = (uint8_t)('0' + instr_idx);
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_TRACKNAME;
        e->flags    = FLG_META;
        e->meta_type = 0x03;
        e->meta_data = tn;
        e->meta_len  = sizeof(tn0) - 1;
    }

    /* Program change at tick 0. */
    uint8_t gm = _current_melody_gm()[instr_idx];
    if (use_pipi_swap && ins->pipi) gm = _current_melody_gm_pipi()[instr_idx];
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_PROGRAM;
        e->status   = (uint8_t)(0xC0 | (uint8_t)instr_idx);
        e->data1    = gm;
        e->flags    = 0;  /* program-change is 2 bytes (status + data1) */
    }

    if (ins->n_events == 0) {
        return;
    }

    int last_pan = -1;

    /* Iteration 0: emit events from 0 to loop_end (the intro + first body). */
    uint32_t loop_len_ticks =
        (s->loop_end > s->loop_start) ? (s->loop_end - s->loop_start) : 0;
    uint32_t end_tick0 =
        (s->loop_end > 0) ? s->loop_end : 0xFFFFFFFFu;

    for (uint16_t e = 0; e < ins->n_events; ++e) {
        if (ins->positions[e] >= end_tick0) break;
        append_melody_event(t, (uint8_t)instr_idx,
                            ins->positions[e],
                            ins->notes[e], ins->lengths[e],
                            ins->volumes[e], ins->pannings[e],
                            s->beats_per_step, 0, &last_pan);
    }

    /* Iterations 1..loop_reps: replicate the loop body. Only events
     * with position in [loop_start, loop_end). Each replica is shifted
     * by rep * loop_len_ticks (in Organya ticks).
     *
     * Worked example with loop_start=128, loop_end=256, rep=1:
     *   event at org pos 200 -> replica at org pos 200 + 1*(256-128) = 328
     *   event at org pos 130 -> replica at org pos 130 + 1*128       = 258
     * which is exactly the loop body shifted to start right after end_tick0. */
    if (loop_len_ticks > 0) {
        for (int rep = 1; rep <= loop_reps; ++rep) {
            uint32_t tick_offset = (uint32_t)rep * loop_len_ticks;
            for (uint16_t e = 0; e < ins->n_events; ++e) {
                if (ins->positions[e] <  s->loop_start) continue;
                if (ins->positions[e] >= s->loop_end)   break;
                append_melody_event(t, (uint8_t)instr_idx,
                                    ins->positions[e],
                                    ins->notes[e], ins->lengths[e],
                                    ins->volumes[e], ins->pannings[e],
                                    s->beats_per_step,
                                    tick_offset, &last_pan);
            }
        }
    }
}

/* Emit the drum MTrk. Merges Organya instruments 8..15 onto MIDI channel 9. */
static void build_drum_track(TrkEvents *t, const OrgSong *s, int loop_reps)
{
    static const uint8_t tn[] = "Org Drums";
    {
        TrkEvent *e = trk_alloc(t);
        e->abs_tick = 0;
        e->order    = ORDER_TRACKNAME;
        e->flags    = FLG_META;
        e->meta_type = 0x03;
        e->meta_data = tn;
        e->meta_len  = sizeof(tn) - 1;
    }

    uint32_t loop_len_ticks =
        (s->loop_end > s->loop_start) ? (s->loop_end - s->loop_start) : 0;
    uint32_t end_tick0 =
        (s->loop_end > 0) ? s->loop_end : 0xFFFFFFFFu;

    for (int drum = 0; drum < ORG_N_DRUM; ++drum) {
        const OrgInstr *ins = &s->instr[ORG_N_MELODY + drum];
        if (ins->n_events == 0) continue;

        /* Iter 0: intro + first body. */
        for (uint16_t e = 0; e < ins->n_events; ++e) {
            if (ins->positions[e] >= end_tick0) break;
            append_drum_event(t, (uint8_t)drum,
                              ins->positions[e],
                              ins->notes[e], ins->volumes[e],
                              s->beats_per_step, 0);
        }

        /* Iter 1..loop_reps: loop body replication. */
        if (loop_len_ticks > 0) {
            for (int rep = 1; rep <= loop_reps; ++rep) {
                uint32_t tick_offset = (uint32_t)rep * loop_len_ticks;
                for (uint16_t e = 0; e < ins->n_events; ++e) {
                    if (ins->positions[e] <  s->loop_start) continue;
                    if (ins->positions[e] >= s->loop_end)   break;
                    append_drum_event(t, (uint8_t)drum,
                                      ins->positions[e],
                                      ins->notes[e], ins->volumes[e],
                                      s->beats_per_step,
                                      tick_offset);
                }
            }
        }
    }
}

/* ===================================================================== */
/* Top-level convert                                                     */
/* ===================================================================== */

static int convert(const char *in_path, const char *out_path,
                   int loop_reps, int use_pipi_swap, int force)
{
    /* Refuse overwrite without --force. */
    if (!force) {
        struct stat st;
        if (stat(out_path, &st) == 0) {
            fprintf(stderr, "org2mid: %s: already exists; use --force to "
                            "overwrite\n", out_path);
            return 1;
        }
    }

    /* Slurp input. */
    FILE *fp = fopen(in_path, "rb");
    if (!fp) {
        fprintf(stderr, "org2mid: %s: %s\n", in_path, strerror(errno));
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "org2mid: %s: fseek: %s\n", in_path, strerror(errno));
        fclose(fp);
        return 1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fprintf(stderr, "org2mid: %s: ftell: %s\n", in_path, strerror(errno));
        fclose(fp);
        return 1;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "org2mid: out of memory reading %s\n", in_path);
        fclose(fp);
        return 1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fprintf(stderr, "org2mid: %s: short read\n", in_path);
        free(buf);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    /* Parse. */
    OrgSong song;
    if (parse_org(buf, (size_t)sz, in_path, &song) != 0) {
        free(buf);
        return 1;
    }
    free(buf);

    /* Build all 10 tracks. */
    TrkEvents tracks[SMF_NTRKS];
    for (int i = 0; i < SMF_NTRKS; ++i) trk_init(&tracks[i]);

    build_metadata_track(&tracks[0], &song);
    for (int m = 0; m < ORG_N_MELODY; ++m) {
        build_melody_track(&tracks[1 + m], &song, m, loop_reps, use_pipi_swap);
    }
    build_drum_track(&tracks[9], &song, loop_reps);

    /* Append EOT to every track. */
    for (int i = 0; i < SMF_NTRKS; ++i) append_eot(&tracks[i]);

    /* Build output: MThd + MTrk0..MTrk9. */
    Buf out;
    buf_init(&out);

    /* MThd: 14 bytes total. */
    buf_put_bytes(&out, "MThd", 4);
    buf_put_u32be(&out, 6);
    buf_put_u16be(&out, SMF_FORMAT);
    buf_put_u16be(&out, SMF_NTRKS);
    buf_put_u16be(&out, SMF_PPQ);

    /* MTrks. */
    for (int i = 0; i < SMF_NTRKS; ++i) emit_mtrk(&out, &tracks[i]);

    /* Write output file. */
    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "org2mid: %s: %s\n", out_path, strerror(errno));
        for (int i = 0; i < SMF_NTRKS; ++i) trk_free(&tracks[i]);
        buf_free(&out);
        song_free(&song);
        return 1;
    }
    if (fwrite(out.buf, 1, out.len, fp) != out.len) {
        fprintf(stderr, "org2mid: %s: short write\n", out_path);
        fclose(fp);
        for (int i = 0; i < SMF_NTRKS; ++i) trk_free(&tracks[i]);
        buf_free(&out);
        song_free(&song);
        return 1;
    }
    fclose(fp);

    fprintf(stderr, "org2mid: %s -> %s (%zu bytes; loop_reps=%d, "
                    "ms_per_beat=%u, bps=%u, loop_start=%u, loop_end=%u)\n",
            in_path, out_path, out.len, loop_reps,
            song.ms_per_beat, song.beats_per_step,
            song.loop_start, song.loop_end);

    for (int i = 0; i < SMF_NTRKS; ++i) trk_free(&tracks[i]);
    buf_free(&out);
    song_free(&song);
    return 0;
}

/* ===================================================================== */
/* main                                                                  */
/* ===================================================================== */

static void usage(FILE *fp) {
    fprintf(fp,
        "Usage: org2mid [OPTIONS] <input.org> <output.mid>\n"
        "\n"
        "Convert a Cave Story Organya (.org) file to a Standard MIDI File.\n"
        "Target: SMF Format 1, PPQ=48, 10 MTrks (1 metadata + 8 melody +\n"
        "1 drum). Compatible with doskutsu's MidiScheduler.\n"
        "\n"
        "Options:\n"
        "  --loop-strategy=N   Number of loop-body repetitions appended\n"
        "                      after the intro (default %d). Higher = longer\n"
        "                      gap before the intro re-plays at SMF end-of-\n"
        "                      track loop-from-start.\n"
        "  --no-pipi-swap      Disable the pipi-flag GM-program swap\n"
        "                      (use the base melody GM table for every\n"
        "                      instrument regardless of pipi).\n"
        "  --gm-table=v1|v2    GM-patch mapping variant. Default v2\n"
        "                      (wave-42; addresses operator wave-41\n"
        "                      'thumps and cowbell' aesthetic feedback\n"
        "                      by replacing the most-likely-blaty\n"
        "                      patches with mellower alternatives).\n"
        "                      Use v1 to reproduce the original task #2\n"
        "                      design-doc GM mapping for A/B comparison.\n"
        "  --force             Overwrite <output.mid> if it exists.\n"
        "  -h, --help          Show this help and exit.\n",
        DEFAULT_LOOP_REPS);
}

int main(int argc, char **argv)
{
    int loop_reps = DEFAULT_LOOP_REPS;
    int use_pipi_swap = 1;
    int force = 0;
    const char *in_path = NULL;
    const char *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strncmp(a, "--loop-strategy=", 16) == 0) {
            const char *v = a + 16;
            char *end;
            long n = strtol(v, &end, 10);
            if (*v == '\0' || *end != '\0' || n < 0 || n > 32) {
                fprintf(stderr, "org2mid: --loop-strategy value must be "
                                "an integer 0..32 (got '%s')\n", v);
                return 2;
            }
            loop_reps = (int)n;
        } else if (strcmp(a, "--no-pipi-swap") == 0) {
            use_pipi_swap = 0;
        } else if (strncmp(a, "--gm-table=", 11) == 0) {
            const char *v = a + 11;
            if (strcmp(v, "v1") == 0) {
                g_gm_table = GM_TABLE_V1;
            } else if (strcmp(v, "v2") == 0) {
                g_gm_table = GM_TABLE_V2;
            } else {
                fprintf(stderr, "org2mid: --gm-table value must be "
                                "v1 or v2 (got '%s')\n", v);
                return 2;
            }
        } else if (strcmp(a, "--force") == 0) {
            force = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "org2mid: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else if (!in_path) {
            in_path = a;
        } else if (!out_path) {
            out_path = a;
        } else {
            fprintf(stderr, "org2mid: extra positional argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }

    if (!in_path || !out_path) {
        usage(stderr);
        return 2;
    }

    return convert(in_path, out_path, loop_reps, use_pipi_swap, force);
}
