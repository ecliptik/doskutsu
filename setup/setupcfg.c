/*
 * setupcfg.c -- DOSKUTSU.CFG in-memory model + load/save for SETUP.EXE.
 * See setupcfg.h. ASCII-only, no SDL.
 */

#include <stdio.h>
#include <string.h>

#include "setupcfg.h"

static void scfg_copy(char *dst, const char *src)
{
  size_t n = strlen(src);
  if (n >= SCFG_VAL_MAX) n = SCFG_VAL_MAX - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void scfg_defaults(scfg_t *c)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    scfg_copy(c->values[i], DKT_KEYS[i].def);
}

int scfg_index(const char *cfg_key)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (dkt_key_ieq(DKT_KEYS[i].cfg_key, cfg_key, (int)strlen(cfg_key)))
      return i;
  return -1;
}

const char *scfg_get(const scfg_t *c, int idx)
{
  if (idx < 0 || idx >= DKT_KEY_COUNT) return "";
  return c->values[idx];
}

int scfg_differs(const scfg_t *a, const scfg_t *b)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (strcmp(a->values[i], b->values[i]) != 0) return 1;
  return 0;
}

void scfg_set(scfg_t *c, int idx, const char *val)
{
  if (idx < 0 || idx >= DKT_KEY_COUNT) return;
  scfg_copy(c->values[idx], val);
}

static int scfg_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

int scfg_load(scfg_t *c, const char *path)
{
  FILE *f;
  char  line[256];
  int   read_count = 0;

  scfg_defaults(c);

  f = fopen(path, "rb"); /* DOS-PORT: always binary mode */
  if (!f) return -1;

  while (fgets(line, (int)sizeof(line), f))
  {
    char *p = line;
    char *eq, *ks, *ke, *vs, *ve;
    const dkt_key_t *entry;
    int   idx;

    while (*p && scfg_is_space(*p)) ++p;
    if (*p == '\0' || *p == ';' || *p == '#') continue;

    eq = strchr(p, '=');
    if (!eq) continue;

    ks = p; ke = eq;
    while (ke > ks && scfg_is_space(ke[-1])) --ke;
    if (ke == ks) continue;

    entry = dkt_lookup(ks, (int)(ke - ks));
    if (!entry) continue;
    idx = (int)(entry - DKT_KEYS);

    vs = eq + 1;
    while (*vs && scfg_is_space(*vs)) ++vs;
    ve = vs + strlen(vs);
    while (ve > vs && scfg_is_space(ve[-1])) --ve;

    {
      char val[SCFG_VAL_MAX];
      int  n = (int)(ve - vs);
      if (n < 0) n = 0;
      if (n >= SCFG_VAL_MAX) n = SCFG_VAL_MAX - 1;
      memcpy(val, vs, (size_t)n);
      val[n] = '\0';
      scfg_copy(c->values[idx], val);
      ++read_count;
    }
  }

  fclose(f);
  return read_count;
}

int scfg_save(const scfg_t *c, const char *path)
{
  FILE *f;
  int   i;

  f = fopen(path, "wb"); /* CRLF written explicitly for DOS-edit friendliness */
  if (!f) return -1;

  fputs("; DOSKUTSU.CFG -- written by SETUP.EXE.\r\n", f);
  fputs("; Edit by hand with care. Precedence: for most keys a real DOS `SET`\r\n", f);
  fputs("; overrides the line here (env > file > default). The one exception is\r\n", f);
  fputs("; BLASTER (sound hardware): its line here overrides an ambient AUTOEXEC\r\n", f);
  fputs("; `SET BLASTER` (file > env), since SETUP's hardware choice is authoritative.\r\n", f);
  fputs("; schema=1\r\n", f);
  fputs(";\r\n", f);

  for (i = 0; i < DKT_KEY_COUNT; ++i)
  {
    const dkt_key_t *k = &DKT_KEYS[i];

    /* "auto" backend -> omit so the engine auto-detect chain runs. */
    if (k->type == DKT_ENUM && k->enum_vals &&
        dkt_key_ieq("auto", c->values[i], (int)strlen(c->values[i])) &&
        dkt_key_ieq(k->cfg_key, "AUDIO_BACKEND", 13))
    {
      fprintf(f, "; %s=auto  (omitted -> engine auto-detect)\r\n", k->cfg_key);
      continue;
    }

    /* Empty free-form string (BLASTER unset) -> omit the line so SETUP does
     * NOT clobber an ambient AUTOEXEC `SET BLASTER`. Only a hardware choice
     * the user actually made (non-empty) is written, and (being
     * DKT_F_AUTHORITATIVE) it then overrides the ambient SET via the loader. */
    if (k->type == DKT_STR && c->values[i][0] == '\0')
    {
      fprintf(f, "; %s  (omitted -> auto-detect / AUTOEXEC SET %s)\r\n",
              k->cfg_key, k->env_name);
      continue;
    }

    /* An optional INT whose range admits a negative sentinel (imin < 0) omits
     * its line while the value is negative, so the engine hint stays ABSENT --
     * "leave unchanged" (A4 WB_MUSIC_VOL default -1). Writing "-1" instead would
     * make the SDL SB16 backend (SDL/0119, guard `wb && wb[0]`) run and CLAMP the
     * negative to 0 -> muting the WaveBlaster Line-In/CD input, the opposite of a
     * no-op. Only WB_MUSIC_VOL has imin < 0, so this is scoped to it. */
    if (k->type == DKT_INT && k->imin < 0 && c->values[i][0] == '-')
    {
      fprintf(f, "; %s  (omitted -> leave the mixer level unchanged)\r\n",
              k->cfg_key);
      continue;
    }

    fprintf(f, "; %s\r\n", k->help);
    fprintf(f, "%s=%s\r\n", k->cfg_key, c->values[i]);
  }

  /* A partial write (disk full / write-protected CF detected mid-stream) sets
   * the stream error indicator; fclose() flushes the stdio buffer, so a
   * flush/close failure surfaces here even when the earlier fprintf()s all
   * returned. Report either as -1 so cfg_write_toast()'s "disk full / read-only"
   * branch fires instead of falsely claiming "Settings saved" over a truncated
   * DOSKUTSU.CFG. */
  if (ferror(f))
  {
    fclose(f);
    return -1;
  }
  if (fclose(f) != 0)
    return -1;
  return 0;
}
