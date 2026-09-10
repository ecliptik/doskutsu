/*
 * midi_sched.c -- standalone Standard MIDI File parser + tick scheduler (plain C).
 *
 * Shared, reusable scheduler (the canonical SMF playback core for SDL-DOS
 * ports). Plain-C port of vendor/nxengine-evo/src/sound/MidiScheduler.cpp
 * (_parse_smf / _parse_track / _read_vlq / _read_u16_be / _read_u32_be /
 * _recompute_us_per_tick / start / tick). Same parse acceptance, same
 * tempo->us-per-tick math, same loop-from-start semantics -- verified
 * line-for-line equivalent (T56). The engine code is C++ (std::vector,
 * std::stable_sort); this builds as C99, so the event list is a realloc-grown
 * array and the stable order across tracks is preserved by a per-event
 * insertion sequence number used as the sort tie-breaker.
 *
 * See midi_sched.h for the contract. ASCII-only.
 */

#include "midi_sched.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
  EV_NOTE_ON = 0,
  EV_NOTE_OFF,
  EV_CONTROL_CHANGE,
  EV_PROGRAM_CHANGE,
  EV_TEMPO_CHANGE,    /* 24-bit us-per-quarter packed in channel/data1/data2 */
  EV_END_OF_TRACK
};

typedef struct
{
  uint64_t abs_tick;
  uint32_t seq;       /* insertion order -- stable-sort tie-breaker */
  uint8_t  type;
  uint8_t  channel;
  uint8_t  data1;
  uint8_t  data2;
} smf_ev;

struct midi_sched
{
  smf_ev     *events;
  uint32_t    nevents;
  uint32_t    cap;

  uint16_t    division_ppq;
  uint32_t    tempo_us_per_quarter;
  double      us_per_tick;

  uint32_t    next_idx;
  uint64_t    position_us;
  uint64_t    last_tick_now_ms;
  int         playing;
  uint64_t    dispatched;   /* T42: cumulative channel events dispatched (tempo witness) */

  midi_sched_sink sink;
};

/* ---- byte-stream helpers (MidiScheduler big-endian + VLQ) ----------------*/

static int read_vlq(const uint8_t *buf, size_t *cur, size_t end, uint32_t *out)
{
  int i;
  *out = 0;
  for (i = 0; i < 4; ++i)
  {
    uint8_t b;
    if (*cur >= end)
      return 0;
    b = buf[(*cur)++];
    *out = (*out << 7) | (uint32_t)(b & 0x7F);
    if ((b & 0x80) == 0)
      return 1;
  }
  return 0; /* VLQ longer than 4 bytes is invalid */
}

static uint16_t read_u16(const uint8_t *buf, size_t *cur)
{
  uint16_t v = (uint16_t)((buf[*cur] << 8) | buf[*cur + 1]);
  *cur += 2;
  return v;
}

static uint32_t read_u32(const uint8_t *buf, size_t *cur)
{
  uint32_t v = ((uint32_t)buf[*cur]     << 24) |
               ((uint32_t)buf[*cur + 1] << 16) |
               ((uint32_t)buf[*cur + 2] <<  8) |
               ((uint32_t)buf[*cur + 3]);
  *cur += 4;
  return v;
}

/* ---- event list ----------------------------------------------------------*/

static int ev_push(midi_sched *m, uint64_t tick, int type, int ch, int d1, int d2)
{
  smf_ev *e;
  if (m->nevents == m->cap)
  {
    uint32_t ncap = m->cap ? m->cap * 2 : 256;
    smf_ev  *n    = (smf_ev *)realloc(m->events, (size_t)ncap * sizeof(smf_ev));
    if (!n)
      return 0;
    m->events = n;
    m->cap    = ncap;
  }
  e = &m->events[m->nevents];
  e->abs_tick = tick;
  e->seq      = m->nevents;
  e->type     = (uint8_t)type;
  e->channel  = (uint8_t)ch;
  e->data1    = (uint8_t)d1;
  e->data2    = (uint8_t)d2;
  m->nevents++;
  return 1;
}

static int ev_cmp(const void *a, const void *b)
{
  const smf_ev *x = (const smf_ev *)a;
  const smf_ev *y = (const smf_ev *)b;
  if (x->abs_tick < y->abs_tick) return -1;
  if (x->abs_tick > y->abs_tick) return 1;
  if (x->seq < y->seq) return -1;
  if (x->seq > y->seq) return 1;
  return 0;
}

/* ---- tempo ---------------------------------------------------------------*/

static void recompute_us_per_tick(midi_sched *m)
{
  if (m->division_ppq == 0)
  {
    m->us_per_tick = 500000.0 / 480.0;
    return;
  }
  m->us_per_tick = (double)m->tempo_us_per_quarter / (double)m->division_ppq;
}

/* ---- track parse (MidiScheduler::_parse_track) ---------------------------*/

static int parse_track(midi_sched *m, const uint8_t *buf, size_t track_start, size_t track_end)
{
  size_t   cursor         = track_start;
  uint64_t current_tick   = 0;
  uint8_t  running_status = 0;
  int      saw_end        = 0;

  while (cursor < track_end)
  {
    uint32_t delta = 0;
    uint8_t  status;
    if (!read_vlq(buf, &cursor, track_end, &delta))
      return 0;
    current_tick += delta;

    if (cursor >= track_end)
      return 0;

    status = buf[cursor];
    if (status < 0x80)
    {
      if (running_status == 0)
        return 0; /* running status with no prior status byte */
      status = running_status;
    }
    else
    {
      ++cursor;
      running_status = (status >= 0xF0) ? 0 : status;
    }

    if ((status & 0xF0) == 0xF0)
    {
      if (status == 0xFF)
      {
        uint8_t  meta_type;
        uint32_t meta_len = 0;
        if (cursor >= track_end)
          return 0;
        meta_type = buf[cursor++];
        if (!read_vlq(buf, &cursor, track_end, &meta_len))
          return 0;
        if (cursor + meta_len > track_end)
          return 0;

        if (meta_type == 0x51 && meta_len == 3)
        {
          uint32_t tempo = ((uint32_t)buf[cursor]     << 16) |
                           ((uint32_t)buf[cursor + 1] <<  8) |
                           ((uint32_t)buf[cursor + 2]);
          if (!ev_push(m, current_tick, EV_TEMPO_CHANGE,
                       (int)((tempo >> 16) & 0xFF),
                       (int)((tempo >>  8) & 0xFF),
                       (int)( tempo        & 0xFF)))
            return 0;
        }
        else if (meta_type == 0x2F)
        {
          if (!ev_push(m, current_tick, EV_END_OF_TRACK, 0, 0, 0))
            return 0;
          saw_end = 1;
        }
        /* other meta events: parsed-and-skipped */
        cursor += meta_len;
      }
      else if (status == 0xF0 || status == 0xF7)
      {
        uint32_t sysex_len = 0;
        if (!read_vlq(buf, &cursor, track_end, &sysex_len))
          return 0;
        if (cursor + sysex_len > track_end)
          return 0;
        cursor += sysex_len;
      }
      else
      {
        return 0; /* unknown system status */
      }
    }
    else
    {
      uint8_t channel = status & 0x0F;
      uint8_t kind    = status & 0xF0;
      int     arity;
      uint8_t d1, d2;
      switch (kind)
      {
        case 0x80: arity = 2; break; /* note off            */
        case 0x90: arity = 2; break; /* note on             */
        case 0xA0: arity = 2; break; /* poly aftertouch  -> skip */
        case 0xB0: arity = 2; break; /* control change      */
        case 0xC0: arity = 1; break; /* program change      */
        case 0xD0: arity = 1; break; /* channel pressure -> skip */
        case 0xE0: arity = 2; break; /* pitch bend       -> skip */
        default:   return 0;
      }
      if (cursor + (size_t)arity > track_end)
        return 0;
      d1 = buf[cursor++];
      d2 = (arity == 2) ? buf[cursor++] : 0;

      if (kind == 0x80 || (kind == 0x90 && d2 == 0))
      {
        if (!ev_push(m, current_tick, EV_NOTE_OFF, channel, d1, d2)) return 0;
      }
      else if (kind == 0x90)
      {
        if (!ev_push(m, current_tick, EV_NOTE_ON, channel, d1, d2)) return 0;
      }
      else if (kind == 0xB0)
      {
        if (!ev_push(m, current_tick, EV_CONTROL_CHANGE, channel, d1, d2)) return 0;
      }
      else if (kind == 0xC0)
      {
        if (!ev_push(m, current_tick, EV_PROGRAM_CHANGE, channel, d1, 0)) return 0;
      }
      /* 0xA0 / 0xD0 / 0xE0 silently skipped */
    }
  }

  if (!saw_end)
    if (!ev_push(m, current_tick, EV_END_OF_TRACK, 0, 0, 0))
      return 0;
  return 1;
}

/* ---- header parse (MidiScheduler::_parse_smf) ----------------------------*/

static int parse_smf(midi_sched *m, const uint8_t *buf, size_t size)
{
  size_t   cursor = 0;
  uint32_t header_size;
  uint16_t format, ntracks, division;
  int      t;

  if (size < 14)
    return 0;
  if (buf[0] != 'M' || buf[1] != 'T' || buf[2] != 'h' || buf[3] != 'd')
    return 0;
  cursor = 4;

  header_size = read_u32(buf, &cursor);
  if (header_size != 6)
    return 0;
  format = read_u16(buf, &cursor);
  if (format > 1)
    return 0;
  ntracks = read_u16(buf, &cursor);
  if (ntracks == 0 || ntracks > 64)
    return 0;
  division = read_u16(buf, &cursor);
  if ((division & 0x8000) != 0)
    return 0; /* SMPTE division unsupported */
  m->division_ppq = division ? division : 480;

  for (t = 0; t < ntracks; ++t)
  {
    uint32_t track_size;
    size_t   track_end;
    if (cursor + 8 > size)
      return 0;
    if (buf[cursor] != 'M' || buf[cursor + 1] != 'T' ||
        buf[cursor + 2] != 'r' || buf[cursor + 3] != 'k')
      return 0;
    cursor += 4;
    track_size = read_u32(buf, &cursor);
    if (cursor + track_size > size)
      return 0;
    track_end = cursor + track_size;
    if (!parse_track(m, buf, cursor, track_end))
      return 0;
    cursor = track_end;
  }

  /* stable sort by abs_tick (seq tie-break == stable across tracks) */
  if (m->nevents > 1)
    qsort(m->events, m->nevents, sizeof(smf_ev), ev_cmp);

  recompute_us_per_tick(m);
  return 1;
}

/* ---- public API ----------------------------------------------------------*/

midi_sched *midi_sched_open(const char *path)
{
  midi_sched  *m;
  FILE    *fp;
  long     sz;
  uint8_t *buf;
  size_t   got;

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
  sz = ftell(fp);
  if (sz <= 0 || sz > (16L * 1024L * 1024L)) { fclose(fp); return NULL; }
  if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

  buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) { fclose(fp); return NULL; }
  got = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  if (got != (size_t)sz) { free(buf); return NULL; }

  m = (midi_sched *)calloc(1, sizeof(*m));
  if (!m) { free(buf); return NULL; }
  m->tempo_us_per_quarter = 500000;  /* SMF default = 120 BPM */
  m->division_ppq         = 480;
  m->us_per_tick          = 500000.0 / 480.0;

  if (!parse_smf(m, buf, (size_t)sz) || m->nevents == 0)
  {
    free(buf);
    midi_sched_close(m);
    return NULL;
  }
  free(buf);
  return m;
}

void midi_sched_close(midi_sched *m)
{
  if (!m)
    return;
  free(m->events);
  free(m);
}

void midi_sched_set_sink(midi_sched *m, const midi_sched_sink *sink)
{
  if (!m)
    return;
  if (sink)
    m->sink = *sink;
  else
    memset(&m->sink, 0, sizeof(m->sink));
}

void midi_sched_start(midi_sched *m, uint64_t now_ms)
{
  if (!m || m->nevents == 0)
    return;
  m->next_idx              = 0;
  m->position_us           = 0;
  m->last_tick_now_ms      = 0;       /* first tick seeds this; advances 0 */
  m->tempo_us_per_quarter  = 500000;  /* reset to SMF default */
  recompute_us_per_tick(m);
  m->dispatched = 0;
  m->playing = 1;
  (void)now_ms;
}

/* T42 tempo witness accessors. */
uint64_t midi_sched_dispatched(const midi_sched *m)  { return m ? m->dispatched : 0; }
uint64_t midi_sched_position_ms(const midi_sched *m) { return m ? (m->position_us / 1000ULL) : 0; }

/* T56 tempo witness accessors. */
uint64_t midi_sched_us_per_tick_x1000(const midi_sched *m)
{
  return m ? (uint64_t)(m->us_per_tick * 1000.0 + 0.5) : 0;
}
uint32_t midi_sched_tempo_us(const midi_sched *m)  { return m ? m->tempo_us_per_quarter : 0; }
uint16_t midi_sched_division(const midi_sched *m)  { return m ? m->division_ppq : 0; }

void midi_sched_tick(midi_sched *m, uint64_t now_ms)
{
  uint64_t delta_ms, delta_us;

  if (!m || !m->playing || m->nevents == 0)
    return;

  if (m->last_tick_now_ms == 0)
  {
    m->last_tick_now_ms = now_ms;
    return;
  }

  delta_ms = (now_ms > m->last_tick_now_ms) ? (now_ms - m->last_tick_now_ms) : 0;
  if (delta_ms > 10000)
    delta_ms = 10000;
  m->last_tick_now_ms = now_ms;

  delta_us = delta_ms * 1000ULL;
  m->position_us += delta_us;

  for (;;)
  {
    const smf_ev *ev;
    uint64_t      event_us;

    if (m->next_idx >= m->nevents)
    {
      /* loop from start; tempo state retained */
      m->next_idx    = 0;
      m->position_us = 0;
      break;
    }

    ev       = &m->events[m->next_idx];
    event_us = (uint64_t)((double)ev->abs_tick * m->us_per_tick);
    if (event_us > m->position_us)
      break;

    switch (ev->type)
    {
      case EV_NOTE_ON:
        if (m->sink.note_on) m->sink.note_on(m->sink.user, ev->channel, ev->data1, ev->data2);
        m->dispatched++;
        break;
      case EV_NOTE_OFF:
        if (m->sink.note_off) m->sink.note_off(m->sink.user, ev->channel, ev->data1, ev->data2);
        m->dispatched++;
        break;
      case EV_CONTROL_CHANGE:
        if (m->sink.control_change) m->sink.control_change(m->sink.user, ev->channel, ev->data1, ev->data2);
        m->dispatched++;
        break;
      case EV_PROGRAM_CHANGE:
        if (m->sink.program_change) m->sink.program_change(m->sink.user, ev->channel, ev->data1);
        m->dispatched++;
        break;
      case EV_TEMPO_CHANGE:
        m->tempo_us_per_quarter = ((uint32_t)ev->channel << 16) |
                                  ((uint32_t)ev->data1   <<  8) |
                                  ((uint32_t)ev->data2);
        if (m->tempo_us_per_quarter == 0)
          m->tempo_us_per_quarter = 500000;
        recompute_us_per_tick(m);
        break;
      case EV_END_OF_TRACK:
      default:
        break;
    }
    m->next_idx++;
  }
}

uint32_t midi_sched_event_count(const midi_sched *m)
{
  return m ? m->nevents : 0;
}
