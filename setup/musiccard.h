/* musiccard.h -- the music TYPE + music CARD model (SETUP UX v2).
 *
 * The single source of the music-selection model, extracted from main.c so it
 * can be unit-tested on the host (main.c is a TUI binary and is NOT linked into
 * the test). Pure data + pure functions: no TUI, no config object, no globals --
 * every entry point takes the two cfg VALUES it needs and returns a literal.
 *
 * THE MODEL. SETUP asks two questions that the old flat card list conflated:
 *
 *   1. Music TYPE  -- what KIND of music?   Organya / MIDI / No Music
 *   2. Music CARD  -- WHICH card plays it?  Auto-detect / Sound Blaster / AdLib /
 *                     WaveBlaster / General MIDI / Gravis UltraSound
 *
 * Both are DERIVED from the existing AUDIO_BACKEND key (+ the SETUP-only MIDI_DEV
 * discriminator, which splits the two cards that share AUDIO_BACKEND=wb). There
 * is NO new cfg key: writing runs the same mapping in reverse. Keep the
 * derivation (music_type_of / music_card_index) and the write path (the table's
 * `value` / `midi_dev`) in agreement -- setup/tests/test_config.c round-trips
 * every backend through both directions to hold them together.
 *
 * Note Auto-detect is a CARD, not a type: "let the machine choose the card"
 * answers the card question. AUDIO_BACKEND=auto derives to Type=MIDI +
 * Card=Auto-detect.
 */
#ifndef SETUP_MUSICCARD_H
#define SETUP_MUSICCARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* The inline param sub-screen a pick walks after committing the backend. */
enum { MCARD_SUB_NONE = 0, MCARD_SUB_SB, MCARD_SUB_GUS, MCARD_SUB_ORGANYA };

/* Music TYPE. Enum order == the Sound-menu cycle order (Organya -> MIDI -> No
 * Music -> Organya) and doubles as the MUSIC_TYPES index. Organya FIRST: it is
 * the no-sound-card-needed choice and must not read as an afterthought. */
enum { MTYPE_ORGANYA = 0, MTYPE_MIDI, MTYPE_NONE, MTYPE_NTYPES };

typedef struct
{
  const char *value; /* AUDIO_BACKEND written; NULL for MIDI (the CARD writes it) */
  int         sub;   /* inline sub-screen walked on selection                     */
  const char *label;
  const char *desc;
} music_type_t;

typedef struct
{
  const char *value;    /* AUDIO_BACKEND cfg value                        */
  const char *midi_dev; /* MIDI_DEV discriminator; NULL unless value=="wb" */
  int         sub;
  const char *name;     /* SHORT name (width-constrained banner / profile) */
  const char *label;    /* picker row (hardware, + parenthetical if it adds signal) */
  const char *desc;
} music_card_t;

extern const music_type_t MUSIC_TYPES[];
extern const music_card_t MUSIC_CARDS[];
extern const int MUSIC_NCARDS;

/* The TYPE a stored AUDIO_BACKEND belongs to. Everything that is not
 * organya/none is a MIDI card (auto included; unknown/empty lands here too). */
int music_type_of(const char *backend);

/* Index of a (backend, midi_dev) pair in MUSIC_CARDS, or -1 when the backend is
 * not a card at all (organya / none). A "wb" backend whose MIDI_DEV is unset or
 * unrecognized resolves to WaveBlaster (the documented default); an unknown
 * backend resolves to Auto-detect. So a hand-edited CFG never lands on "no
 * current card". */
int music_card_index(const char *backend, const char *midi_dev);

/* The CARD's short name, or "" when the backend is not a card. */
const char *music_card_short(const char *backend, const char *midi_dev);

/* THE resolver used by the banner + the SYSTEM PROFILE panel: "Organya" /
 * "No Music", or the card's short name for a MIDI backend. Every literal comes
 * from the two tables, so the wording cannot drift between screens. */
const char *music_card_label(const char *backend, const char *midi_dev);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SETUP_MUSICCARD_H */
