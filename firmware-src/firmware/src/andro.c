/* SPDX-License-Identifier: GPL-3.0-only
 * ANDROMEDA (an ambient instrument; Dark Funk Durcal design, Polaris Sound Lab), on Felucca 1.8.2 (Hügelton Instruments).
 *
 * HOME's pages after MIXER and CLOCK (params.c PAGES, FAM_TRK: HOME taps go round them):
 *   ALIGN       MOOD, KEY, CHAOS, KEEP; OCT+ ALIGN: one of 13 moods writes the four tracks at once, always in key
 *               (T1 a soft beat or a texture, T2 the bass, T3 the pad's chords, T4 bells or a melody), tempo and space
 *   AUTO        AUTO, AMOUNT, ENERGY, EVERY; OCT+ BACK: the music keeps writing itself from YOUR pattern: every few bars
 *               one part changes a little with its own intervals and rhythms (strong beats on the chord), and the parts
 *               fade in and out with ENERGY. BACK returns to what you wrote (the seed)
 *   KEYS        MODE (RAW / SNAP / SHIFT), KEY, SCALE, ANSWER; OCT+ APPLY. SHIFT: a note played outside the scale moves
 *               the whole song to the nearest scale holding it, on the next beat; ANSWER: what you play, the bells answer
 *   SKY         DRIFT, HAZE, BLOOM, GRAVITY (the macros, live); OCT+ ECLIPSE: the beat sinks into the echoes
 *   SPACE TAPE  MODE 1..12 (the three heads and the spring), RATE, INTENSITY, WOW; OCT+ FREEZE
 *   VISION      CYMATICS, HARMONOGRAPH, ATTRACTOR, ORRERY: mathematics drawn from the music, full strip; OCT+ next
 * Integer maths, bounded loops; pattern writes happen in the main loop (the UI), AUTO's between two steps of a bar
 * (no undo: BACK is its undo), ALIGN's as a pattern or a preset load does them (load_begin / load_end: one undo). */

/* ------------------------------------------------------------------ randomness --- */
static uint32_t an_seed_r = 0x5EED1234u;
static uint32_t an_rnd(void)
{
    uint32_t x = an_seed_r ^ fm1_ms;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    an_seed_r = x ? x : 0x2545F491u;
    return an_seed_r;
}
static uint32_t an_pick(uint32_t n) { return n ? (an_rnd() >> 8) % n : 0u; }
static int an_chance(uint32_t pct) { return (an_rnd() >> 8) % 100u < pct; }
static int32_t an_step(int32_t v, int32_t d, int32_t lo, int32_t hi) { v += d; return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ the moods --- */
/* N_SCALE: 1 MAJ 2 MIN 3 DOR 4 MIX 5 PEN 6 MPEN 7 HARM 8 PHRY 9 LYD 13 WHOLE.
 * Engines (engines.c): 0 ANALOG 2 PHASE 3 LOFI 5 VOICE 6 TRIO 8 GRAIN 9 PHYS 10 DRUM 11 NOISE 12 FM6.
 * ANALOG presets 22.. are ANDROMEDA's (eng_analog.c): 22 POLAR PAD 23 PWM DREAM 24 GLASS PAD 25 WARM PULSE 26 SUB GRAVITY
 * 27 DREAM SAW. FM6 bank voices B1..B32 (dfd_bank.h, the built-in bank): 3 HIDDEN SUB 10 DARK BELL 11 CRYSTAL 17 PREP PIANO 18 VELVET EP
 * 19 MARIMBA 23 FOG PAD 24 EMPTY CHOIR 26 GLASS PAD 32 MUTANT BELL */
enum { BEAT_NONE, BEAT_SOFT, BEAT_BREAK, BEAT_TICK, BEAT_PULSE4, BEAT_CLANK, BEAT_IDM, BEAT_SLOW };
enum { LD_ARP, LD_MELODY, LD_SPARSE, LD_BOX };
typedef struct {
    const char *name, *desc;
    uint8_t bpm_lo, bpm_hi, scale, beat;
    int8_t prog[4];                                  /* the pad's four chords (scale degrees), one a bar */
    uint8_t t1_eng, t1_pre;                          /* T1: DRUM kit, or NOISE texture (BEAT_NONE) */
    uint8_t b_eng, b_pre, b_slot;                    /* T2 bass: engine, preset, FM6 bank voice + 1 (0: none) */
    uint8_t p_eng, p_pre, p_slot;                    /* T3 pad */
    uint8_t l_eng, l_pre, l_slot, lead;              /* T4 bells / lead and how it plays */
    uint8_t tape, wow;                               /* SPACE TAPE mode 1..12, its WOW */
} an_mood_t;
static const an_mood_t AN_MOODS[] = {
    /* name      what it is                        bpm      scl beat       prog              T1       bass        pad         lead               tape wow */
    {"DRIFT",   "FLOATING CLOUDS",       66, 82,  9, BEAT_NONE,   {0, 3, 5, 4},  11, 0,  0, 26, 0,  0, 22, 0,  9, 0, 0, LD_SPARSE, 11, 50},
    {"TIDAL",   "WARM SOFT PULSE",      84, 98,  3, BEAT_SOFT,   {0, 3, 4, 3},  10, 1,  0, 25, 0,  0, 23, 0, 12, 0, 18, LD_ARP, 9, 40},
    {"HAZE",    "NOSTALGIC ARPS",   100, 118, 1, BEAT_BREAK, {0, 5, 3, 4},  10, 0,  6, 0, 0,   0, 24, 0,  0, 5, 0, LD_ARP, 9, 55},
    {"GLASS",   "MUSIC BOX, FROST",        90, 112, 1, BEAT_TICK,   {0, 4, 5, 3},  10, 3,  0, 26, 0,  8, 3, 0, 12, 0, 11, LD_BOX, 7, 30},
    {"PULSE",   "PULSE DREAMS", 104, 124, 3, BEAT_PULSE4, {0, 6, 3, 4}, 10, 2,  0, 25, 0,  0, 23, 0,  0, 27, 0, LD_MELODY, 8, 45},
    {"RUST",    "DRONES, MACHINES",   70, 92,  8, BEAT_CLANK,  {0, 1, 0, 6},  10, 3, 12, 0, 3,   9, 3, 0, 12, 0, 32, LD_SPARSE, 11, 70},
    {"BREATH",  "CHOIRS, SIGHS",           60, 76,  2, BEAT_NONE,   {0, 5, 2, 6},  11, 0,  0, 7, 0,   5, 0, 0,  9, 8, 0, LD_SPARSE, 12, 30},
    {"LULLABY", "LO-FI LULLABY",  96, 116, 2, BEAT_BREAK,  {0, 5, 3, 4},  10, 1,  0, 26, 0, 12, 0, 23, 12, 0, 19, LD_MELODY, 9, 60},
    {"ORBIT",   "POLYRHYTHMS",   112, 132, 3, BEAT_IDM,   {0, 4, 6, 3},  10, 2,  0, 25, 0,  2, 2, 0, 12, 0, 11, LD_ARP, 10, 35},
    {"ABYSS",   "DEEP AND SLOW",           54, 70,  8, BEAT_SLOW,   {0, 6, 5, 1},  10, 1,  0, 26, 0,  9, 7, 0, 12, 0, 10, LD_SPARSE, 11, 65},
    {"CHROME",  "COLD BOWED METAL",      64, 84, 13, BEAT_CLANK,  {0, 2, 4, 1},  10, 3,  6, 0, 0,   9, 3, 0,  6, 3, 0, LD_SPARSE, 7, 25},
    {"DAWN",    "SUNRISE",        88, 104, 9, BEAT_SOFT,   {0, 4, 5, 3},  10, 1,  0, 25, 0,  0, 11, 0,  9, 8, 0, LD_ARP, 9, 35},
    {"STATIC",  "RADIO GHOSTS",       72, 90, 13, BEAT_TICK,   {0, 2, 1, 3},  10, 3,  0, 7, 0,   8, 0, 0,  5, 3, 0, LD_SPARSE, 10, 75},
};
#define AN_NMOODS (sizeof AN_MOODS / sizeof AN_MOODS[0])
static const char *const AN_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const AN_KEEP[4] = {"NONE", "SOUND", "TEMPO", "BOTH"};
static const char *const AN_KMODE[3] = {"RAW", "SNAP", "SHIFT"};
static const char *const AN_EVERY_N[3] = {"2 BARS", "4 BARS", "8 BARS"};
static const uint8_t AN_EVERY[3] = {2, 4, 8};
static const char *const AN_VIS[4] = {"CYMATICS", "HARMONOGRAPH", "ATTRACTOR", "ORRERY"};
static const char *const AN_VIS_K[4] = {"SAND", "LINES", "CHAOS", "ORBIT"};   /* (the card: short) */
/* SHIFT looks among these (the moods' families), in this order of preference */
static const uint8_t AN_FAMILY[7] = {1, 2, 3, 4, 9, 8, 7};

/* the state (not stored: ALIGN writes the music, the music is stored with the project) */
static struct {
    uint8_t mood, root, chaos, keep;                 /* ALIGN */
    uint8_t on, amount, energy, every;               /* AUTO */
    uint8_t kmode, scale, answer;                    /* KEYS (root: above) */
    int8_t drift, haze, bloom, gravity;              /* SKY: 0..100, GRAVITY -100..100 */
    uint8_t tmode, vision, eclipse, freeze;          /* SPACE TAPE mode 0..11, VISION */
    int8_t prog[4];                                  /* the chords of the last ALIGN (degrees) */
    uint32_t flash_ms, bars, auto_ms;                /* a flash; bars played with AUTO; the last change */
    uint8_t beat_prev, last_trk, mutes, seeded;
    int8_t pend_root, pend_scale;                    /* SHIFT: the key waiting for the next beat (-1 none) */
    const char *said;                                /* what AUTO / SHIFT did last (the strip) */
} an = {1, 2, 40, 0, 0, 45, 60, 1, 2, 3, 1, 25, 10, 50, 0, 8, 0, 0, 0, {0, 3, 4, 3}, 0, 0, 0, 0, 0, 0, 0, -1, -1, "READY"};
static void an_apply_fx(void);
static void an_tape_set(void);
#define AN_SEED 32u                                  /* the seed: the first 32 steps of each track */
static step_t an_seed[NTRK][AN_SEED];
static uint8_t an_seed_len[NTRK];
static uint8_t an_motif[8], an_motif_n;              /* ANSWER: the last notes played */
static uint32_t an_motif_ms;

/* ------------------------------------------------------------------ scales and notes --- */
static uint32_t an_mask(void) { return SCALE_MASK[an.scale % (sizeof SCALE_MASK / sizeof SCALE_MASK[0])]; }
static uint32_t an_iv(uint8_t *iv)
{
    uint32_t m = an_mask(), i, n = 0;
    for (i = 0; i < 12u; i++)
        if ((m >> i) & 1u)
            iv[n++] = (uint8_t)i;
    return n ? n : 1u;
}
static uint32_t an_note(int32_t d, int32_t base)    /* scale degree d above base (a MIDI C) in the key */
{
    uint8_t iv[12];
    int32_t n = (int32_t)an_iv(iv), o = d >= 0 ? d / n : -((-d + n - 1) / n), r = d - o * n, v;
    v = base + (int32_t)an.root + 12 * o + iv[r];
    return (uint32_t)(v < 24 ? v + 12 * ((24 - v + 11) / 12) : v > 108 ? v - 12 * ((v - 108 + 11) / 12) : v);
}
static int an_in_scale(uint32_t note, uint32_t root, uint32_t scale)
{
    return (int)((SCALE_MASK[scale % (sizeof SCALE_MASK / sizeof SCALE_MASK[0])] >> ((note + 120u - root) % 12u)) & 1u);
}
static uint32_t an_snap(uint32_t note, uint32_t root, uint32_t scale)   /* the nearest note of the scale (down first) */
{
    int32_t d;
    for (d = 0; d < 6; d++) {
        if (an_in_scale((uint32_t)clamp((int32_t)note - d, 0, 127), root, scale)) return (uint32_t)clamp((int32_t)note - d, 0, 127);
        if (an_in_scale((uint32_t)clamp((int32_t)note + d, 0, 127), root, scale)) return (uint32_t)clamp((int32_t)note + d, 0, 127);
    }
    return note;
}
static int an_melodic(const track_t *t)              /* a track whose notes are pitches (not a kit, slices, a texture) */
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    return e != &ENG_DRUM && e != &ENG_NOISE
#if FELUCCA_SLICE
           && e != &ENG_SLICE
#endif
        ;
}
static void an_keys(track_t *t)                      /* a track's key and how its keys play (KEYS MODE) */
{
    if (!an_melodic(t))
        return;
    t->p[P_ROOT] = (int16_t)(an.root % 12u);
    t->p[P_SCALE] = an.scale;
    t->p[P_QUANT] = an.kmode == 1u ? 1 : 0;          /* SNAP: every key in the scale; RAW and SHIFT: as played */
}

/* ------------------------------------------------------------------ writing --- */
static void an_sound(track_t *t, uint32_t eng, uint32_t pre, uint32_t slot)
{
    set_engine_of(t, eng);
    apply_preset_to(t, pre);
    if (eng == ENGI_FM6 && slot)                     /* a bank voice B1..B32 (fm6_poll loads it) */
        t->p[P_E7] = (int16_t)(FM6_OWN + slot);
}
static void an_clear(track_t *t, uint32_t len, uint32_t div)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = (int16_t)div;                     /* N_DIV: 1 1/8, 2 1/16, 7 1/1 */
}
static void an_put(step_t *s, uint32_t note, uint32_t vel)
{
    s->time = ST_NOTE;
    s->note[0] = (uint8_t)note;
    s->n = 1;
    s->vel = (uint8_t)vel;
    s->flags = 0;
}
static void an_hold(track_t *t, uint32_t from, uint32_t len)
{
    uint32_t k;
    for (k = 1; k < len && from + k < (uint32_t)t->p[P_SLEN] && from + k < NSTEP; k++)
        if (t->step[from + k].time != ST_NOTE)
            t->step[from + k].time = ST_TIE;
}
#define LN_KICK 0
#define LN_SNARE 1
#define LN_CLAP 2
#define LN_CH 3
#define LN_OH 4
#define LN_TOM 5
#define LN_RIM 6
#define LN_BELL 7
static void an_hit(step_t *s, uint32_t lane, uint32_t chance)
{
    s->time = ST_NOTE;
    s->hit |= (uint8_t)(1u << lane);
    if (chance < 100u && s->hit == (1u << lane))
        step_set_chance(s, chance);
}
/* T1: a soft beat (16 steps), or a texture (long notes, one a bar) */
static void an_beat(track_t *t, const an_mood_t *m)
{
    uint32_t k, c = an.chaos;
    if (m->beat == BEAT_NONE) {
        an_clear(t, 4, 7);
        for (k = 0; k < 4u; k++) {
            an_put(&t->step[k], an_note(m->prog[k] - 7, 60), 70u + an_pick(30));
            if (k && an_chance(30)) t->step[k].time = ST_TIE;
        }
        return;
    }
    an_clear(t, 16, 2);
    for (k = 0; k < 16u; k++) {
        step_t *s = &t->step[k];
        switch (m->beat) {
        case BEAT_SOFT:
            if (k == 0u || (k == 10u && an_chance(70))) an_hit(s, LN_KICK, 100);
            if (k == 8u) an_hit(s, LN_RIM, 100);
            if ((k & 3u) == 2u) an_hit(s, LN_CH, 60u + c / 3u);
            break;
        case BEAT_BREAK:
            if (k == 0u || k == 7u || (k == 10u && an_chance(60))) an_hit(s, LN_KICK, 100);
            if (k == 4u || k == 12u) an_hit(s, LN_SNARE, 100);
            if ((k == 9u || k == 14u) && an_chance(40 + c / 3u)) an_hit(s, LN_RIM, 70);
            if (!(k & 1u)) an_hit(s, LN_CH, k & 2u ? 70u : 100u);
            break;
        case BEAT_TICK:
            if (k == 0u) an_hit(s, LN_KICK, 100);
            if (k == 4u || k == 12u) an_hit(s, LN_RIM, 100);
            if (an_chance(45u + c / 3u)) an_hit(s, LN_CH, 40u + an_pick(50));
            break;
        case BEAT_PULSE4:
            if (!(k & 3u)) an_hit(s, LN_KICK, 100);
            if ((k & 3u) == 2u) an_hit(s, LN_OH, 100);
            if (k == 4u || k == 12u) an_hit(s, LN_CLAP, 100);
            break;
        case BEAT_CLANK:
            if (k == 0u || (k == 11u && an_chance(70))) an_hit(s, LN_KICK, 100);
            if (k == 3u || k == 9u || (k == 14u && an_chance(50))) an_hit(s, LN_BELL, 80);
            if (k == 6u && an_chance(60)) an_hit(s, LN_TOM, 100);
            break;
        case BEAT_IDM:
            if (k == 0u || k == 3u || k == 10u) an_hit(s, LN_KICK, 100);
            if (k == 6u || k == 13u) an_hit(s, LN_SNARE, 100);
            if (an_chance(60)) an_hit(s, LN_CH, 70u + an_pick(30));
            if (s->hit & (1u << LN_CH) && an_chance(15u + c / 4u)) step_set_ratchet(s, 2u + an_pick(2));
            break;
        default:                                     /* BEAT_SLOW: a heartbeat */
            if (k == 0u || (k == 3u && an_chance(60))) an_hit(s, LN_KICK, 100);
            if (k == 8u && an_chance(40)) an_hit(s, LN_RIM, 80);
            break;
        }
        if (s->time == ST_NOTE && an_chance(c / 10u))
            step_set_chance(s, 60u + an_pick(30));
    }
}
/* T2: the bass, 1/8, four bars: the chord's root on the bar, held; now and then the fifth */
static void an_bass(track_t *t)
{
    uint32_t b, k;
    an_clear(t, 32, 1);
    for (b = 0; b < 4u; b++) {
        int32_t d = an.prog[b];
        an_put(&t->step[b * 8u], an_note(d, 36), 100);
        an_hold(t, b * 8u, an.chaos > 55u ? 4u : 6u);
        if (an_chance(25u + an.chaos / 3u)) {
            k = b * 8u + 6u - (an_chance(50) ? 2u : 0u);
            an_put(&t->step[k], an_note(d + (an_chance(60) ? 4 : 0), 36), 84);
            an_hold(t, k, 2);
        }
    }
}
/* T3: the pad, a chord a bar (1/1, four steps), POLY */
static void an_pad(track_t *t)
{
    uint32_t b;
    an_clear(t, 4, 7);
    for (b = 0; b < 4u; b++) {
        step_t *s = &t->step[b];
        int32_t d = an.prog[b];
        s->time = ST_NOTE;
        s->note[0] = (uint8_t)an_note(d, 48);
        s->note[1] = (uint8_t)an_note(d + 2, 48);
        s->note[2] = (uint8_t)an_note(d + 4, 48);
        s->n = 3;
        if (an.chaos > 40u) { s->note[3] = (uint8_t)an_note(d + (an.chaos > 70u ? 8 : 6), 48); s->n = 4; }
        s->vel = 88;
    }
    t->p[P_VOICE] = V_POLY;
}
/* T4: bells or a melody, 1/8, four bars, the chord of each bar under it */
static int32_t an_chord_tone(int32_t bar, uint32_t pick) { return an.prog[bar & 3] + (int32_t)(pick % 3u) * 2; }
static void an_lead(track_t *t, uint32_t kind)
{
    uint32_t k, prev = 4;
    an_clear(t, 32, 1);
    for (k = 0; k < 32u; k++) {
        int32_t bar = (int32_t)(k / 8u), d;
        uint32_t pos = k & 7u, on;
        switch (kind) {
        case LD_ARP: on = an_chance(pos & 1u ? 55u + an.chaos / 4u : 92u); d = an_chord_tone(bar, pos + an_pick(2)) + (pos >= 4u ? 7 : 0); break;
        case LD_BOX: on = an_chance(pos & 1u ? 45u : 80u); d = an_chord_tone(bar, pos * 2u + 1u) + 7 + (an_chance(25) ? 7 : 0); break;
        case LD_MELODY:
            on = pos == 0u || an_chance(pos & 1u ? 18u + an.chaos / 5u : 45u);
            d = pos == 0u || pos == 4u ? an_chord_tone(bar, an_pick(3)) + 7 : (int32_t)prev + (int32_t)an_pick(5) - 2;
            break;
        default: on = (pos == 0u && an_chance(80)) || an_chance(8u + an.chaos / 8u); d = an_chord_tone(bar, an_pick(3)) + 7; break;
        }
        if (!on)
            continue;
        d = d < 2 ? 2 : d > 16 ? 16 : d;
        prev = (uint32_t)d;
        an_put(&t->step[k], an_note(d, 48), pos == 0u ? 100 : 76u + an_pick(16));
        if (kind == LD_MELODY || kind == LD_SPARSE)
            an_hold(t, k, kind == LD_SPARSE ? 6u : 2u);
    }
}
static void an_snapshot(void)                        /* the seed AUTO grows from, and BACK returns to */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        memcpy(an_seed[i], trk[i].step, sizeof an_seed[i]);
        an_seed_len[i] = (uint8_t)clamp(trk[i].p[P_SLEN], 1, AN_SEED);
    }
    an.seeded = 1;
}
static void an_align(void)
{
    const an_mood_t *m = &AN_MOODS[an.mood % AN_NMOODS];
    uint32_t i, sound = !(an.keep & 1u);
    an.scale = m->scale;
    for (i = 0; i < 4u; i++)
        an.prog[i] = m->prog[i];
    if (an_chance(an.chaos)) {                       /* another order of the mood's chords now and then */
        int8_t x = an.prog[1];
        an.prog[1] = an.prog[2];
        an.prog[2] = x;
    }
    if (!(an.keep & 2u))
        song.g[G_BPM] = (int16_t)clamp((int32_t)(m->bpm_lo + an_pick(m->bpm_hi - m->bpm_lo + 1u)), 40, 240);
    for (i = 0; i < NTRK && i < 4u; i++) {
        track_t *t = &trk[i];
        load_begin(t, UNDO_SOUND | UNDO_PAT);
        if (sound) {
            if (i == 0u) an_sound(t, m->t1_eng, m->t1_pre, 0);
            if (i == 1u) an_sound(t, m->b_eng, m->b_pre, m->b_slot);
            if (i == 2u) an_sound(t, m->p_eng, m->p_pre, m->p_slot);
            if (i == 3u) an_sound(t, m->l_eng, m->l_pre, m->l_slot);
        }
        if (i == 0u) {
            an_beat(t, m);
        }
        if (i == 1u) an_bass(t);
        if (i == 2u) an_pad(t);
        if (i == 3u) an_lead(t, m->lead);
        an_keys(t);
        t->p[P_DLY] = (int16_t)(i == 0u ? 12 : i == 1u ? 6 : i == 2u ? 40 : 70);
        t->p[P_REV] = (int16_t)(i == 0u ? 30 : i == 1u ? 18 : i == 2u ? 80 : 70);
        t->p[P_SSWING] = 0;
        load_end(t);
    }
    an.tmode = (uint8_t)(m->tape - 1u);
    song.g[G_DRREV] = (int16_t)(m->wow * 127 / 100);
    an_tape_set();
    an.drift = (int8_t)m->wow;
    an_snapshot();
    an.bars = 0;
    an.flash_ms = fm1_ms;
    an.said = m->name;
    ui.force = 1;
    ui_say("ALIGNED ", m->name);
}

/* ------------------------------------------------------------------ AUTO --- */
static uint32_t an_bar_chord_note(uint32_t bar, uint32_t pick)   /* a note of the pad's chord playing at that bar */
{
    const track_t *p = &trk[2];
    uint32_t len = (uint32_t)clamp(p->p[P_SLEN], 1, NSTEP), k = bar % len;
    while (k && p->step[k].time != ST_NOTE) k--;
    if (p->step[k].time == ST_NOTE && p->step[k].n)
        return p->step[k].note[pick % p->step[k].n];
    return an_note(an.prog[bar & 3u] + (int32_t)(pick % 3u) * 2, 48);
}
static uint32_t an_near(uint32_t note, uint32_t target)    /* target's pitch class, in the octave nearest note */
{
    int32_t t = (int32_t)target % 12, n = (int32_t)note, best = n, bd = 99, o;
    for (o = 2; o <= 8; o++) {
        int32_t y = o * 12 + t, e = y > n ? y - n : n - y;
        if (e < bd) { bd = e; best = y; }
    }
    return (uint32_t)best;
}
/* one variation of track i, grown from its seed with the seed's own steps and intervals */
static void an_vary(uint32_t i)
{
    track_t *t = &trk[i];
    uint32_t len = an_seed_len[i], k, amt = an.amount, div = (uint32_t)t->p[P_SDIV];
    int32_t ivs[AN_SEED], niv = 0, last = -1;
    if (!an.seeded || !len)
        return;
    if (!an_melodic(t) || i == 0u) {                 /* the beat: a hit moved, a ghost, a hat's chance; never the kick on 1 */
        for (k = 0; k < len && k < AN_SEED; k++) {
            step_t s = an_seed[i][k];
            if (k && an_chance(amt / 4u)) {
                if (s.time == ST_NOTE && !(s.hit & 1u)) { s.time = ST_REST; s.hit = 0; }
                else if (s.time != ST_NOTE && t->step[k].time != ST_NOTE && drum_track(t)) { s.time = ST_NOTE; s.hit = 1u << (an_chance(50) ? LN_CH : LN_RIM); s.vel = 70; step_set_chance(&s, 70); }
            }
            t->step[k] = s;
        }
        return;
    }
    for (k = 0; k < len && k < AN_SEED; k++)         /* the seed's intervals */
        if (an_seed[i][k].time == ST_NOTE && an_seed[i][k].n) {
            if (last >= 0 && niv < (int32_t)AN_SEED) ivs[niv++] = (int32_t)an_seed[i][k].note[0] - last;
            last = an_seed[i][k].note[0];
        }
    for (k = 0; k < len && k < AN_SEED; k++) {
        step_t s = an_seed[i][k];
        uint32_t steps_per_bar = div == 7u ? 1u : div == 1u ? 8u : div == 0u ? 4u : 16u, bar = (an.bars / 1u + k / steps_per_bar);
        if (i == 2u) {                               /* the pad: the chord's voicing turns (an inversion), never new notes */
            if (s.time == ST_NOTE && s.n >= 3u && an_chance(amt / 2u)) {
                uint32_t lo = 0, j;
                for (j = 1; j < s.n; j++) if (s.note[j] < s.note[lo]) lo = j;
                if (s.note[lo] + 12u <= 96u) s.note[lo] = (uint8_t)(s.note[lo] + 12u);
            }
            t->step[k] = s;
            continue;
        }
        if (s.time == ST_NOTE && s.n && an_chance(amt * 6u / 10u)) {   /* a note moved by one of the seed's own intervals */
            int32_t d = niv ? ivs[an_pick((uint32_t)niv)] : (an_chance(50) ? 2 : -2), nn = (int32_t)s.note[0] + (an_chance(50) ? d : -d);
            nn = clamp(nn, 30, 100);
            s.note[0] = (uint8_t)an_snap((uint32_t)nn, an.root, an.scale);
            if (!(k % (steps_per_bar > 4u ? steps_per_bar / 2u : 1u)))   /* strong beats: on the chord */
                s.note[0] = (uint8_t)an_near(s.note[0], an_bar_chord_note(bar, an_pick(3)));
        }
        if (s.time != ST_NOTE && an_chance(amt / 6u) && i == 3u) {     /* a new note, from the chord, now and then */
            s.time = ST_NOTE; s.n = 1; s.vel = 70; s.flags = 0;
            s.note[0] = (uint8_t)an_near(60u + an.root, an_bar_chord_note(bar, an_pick(3)));
        } else if (s.time == ST_NOTE && k && an_chance(amt / 8u)) {
            s.time = ST_REST; s.n = 0;               /* a rest where there was a note: space */
        }
        t->step[k] = s;
    }
}
static void an_energy(void)                          /* which parts play: ENERGY (the pad always; ramped mutes) */
{
    uint32_t want = 1u + (an.energy * 3u + 50u) / 100u, m = 0, k, on = 1u;   /* 1..4 parts */
    static const uint8_t ORDER[2][3] = {{3, 1, 0}, {1, 3, 0}};
    const uint8_t *o = ORDER[an_pick(2)];
    for (k = 0; k < 3u; k++) {
        if (on < want) on++;
        else m |= 1u << o[k];
    }
    an.mutes = (uint8_t)m;
    an_apply_fx();
}
static void an_back(void)                            /* BACK: the seed again, every part in */
{
    uint32_t i;
    if (!an.seeded) { ui_message("ALIGN OR PLAY FIRST"); return; }
    for (i = 0; i < NTRK; i++) {
        load_begin(&trk[i], UNDO_PAT);
        memcpy(trk[i].step, an_seed[i], sizeof an_seed[i]);
        load_end(&trk[i]);
    }
    an.mutes = 0;
    an_apply_fx();
    an.flash_ms = fm1_ms;
    an.said = "YOUR PATTERN";
    ui_say("BACK TO ", "YOUR PATTERN");
}

/* ------------------------------------------------------------------ SHIFT --- */
static void an_rekey(uint32_t root, uint32_t scale)  /* every melodic track into the new key: its notes onto the scale */
{
    uint32_t i, k, j;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (!an_melodic(t))
            continue;
        for (k = 0; k < NSTEP; k++) {
            step_t s = t->step[k];
            if (s.time != ST_NOTE) continue;
            for (j = 0; j < s.n && j < 4u; j++)
                s.note[j] = (uint8_t)an_snap(s.note[j], root, scale);
            t->step[k] = s;
        }
        for (k = 0; k < AN_SEED && an.seeded; k++)
            for (j = 0; j < an_seed[i][k].n && j < 4u; j++)
                an_seed[i][k].note[j] = (uint8_t)an_snap(an_seed[i][k].note[j], root, scale);
        t->p[P_ROOT] = (int16_t)root;
        t->p[P_SCALE] = (int16_t)scale;
    }
    an.root = (uint8_t)root;
    an.scale = (uint8_t)scale;
}
static void an_find_shift(uint32_t note)              /* the nearest scale holding the note: most notes shared */
{
    uint32_t cur = an_mask(), best = 0, bs = 0, f, r;
    int32_t bestv = -1;
    for (f = 0; f < 7u; f++)
        for (r = 0; r < 12u; r++) {
            uint32_t sc = AN_FAMILY[f], m = SCALE_MASK[sc], rot = ((m << r) | (m >> (12u - r))) & 0xFFFu, c = ((cur << an.root) | (cur >> (12u - an.root))) & 0xFFFu, sh = 0, b;
            int32_t v;
            if (!((rot >> (note % 12u)) & 1u)) continue;
            for (b = 0; b < 12u; b++) sh += ((rot & c) >> b) & 1u;
            v = (int32_t)sh * 16 + (sc == an.scale ? 8 : 0) + (r == an.root ? 4 : 0) - (int32_t)f;
            if (v > bestv) { bestv = v; best = r; bs = sc; }
        }
    if (bestv >= 0) {
        an.pend_root = (int8_t)best;
        an.pend_scale = (int8_t)bs;
    }
}

/* ------------------------------------------------------------------ SKY, SPACE TAPE --- */
/* the macros on the FX layer's knobs (perform.c perf_k: FILTER, CRUSH, THROW); ECLIPSE and FREEZE as held effects */
static void an_apply_fx(void)
{
    uint32_t b = (uint32_t)an.mutes << PF_M1;
    if (an.eclipse) b |= (1u << PF_M1) | (2u << PF_M1) | PF_BIT(PF_ETHR) | PF_BIT(PF_RTHR);   /* drums and bass sink into the echoes */
    if (an.freeze) b |= PF_BIT(PF_FRZ);
    an_live_fx = b;
    perf_k[0] = an.gravity;                          /* - LPF (underwater) .. + HPF (far away) */
    perf_k[1] = (int8_t)(an.haze * 55 / 100);        /* the haze: a little grit */
    perf_k[2] = an.bloom;                            /* everything into the echo and the reverb */
    song.g[G_CDEPTH] = (int16_t)(40 + an.drift * 87 / 100);
}
/* SPACE TAPE: modes 1..12 as the classic selector: 1..4 echo only, 5..11 echo and the spring, 12 the spring alone */
static const uint8_t AN_TAPE_HEADS[12] = {1, 2, 3, 5, 1, 2, 3, 4, 5, 6, 7, 0};   /* N_HEADS: H1 H2 H3 H2+3 .. ALL, OFF */
static void an_tape_set(void)
{
    uint32_t m = an.tmode % 12u;
    song.g[G_DRLVL] = AN_TAPE_HEADS[m];
    song.g[G_RTYPE] = m >= 4u ? 1 : 2;               /* the spring (5..12), or the hall */
}

/* ------------------------------------------------------------------ the main loop (main.c) --- */
static void andro_poll(void)
{
    uint32_t h = an_heard, beat = beat_n;
    lang_es = (uint8_t)(ui_idle >> 7);               /* LANG (VISION K4), as the settings hold it */
    if (h) {
        an_heard = 0;
        h--;
        if (an.kmode == 2u && !an_in_scale(h, an.root, an.scale) && an.pend_root < 0)
            an_find_shift(h);
        if (an.answer) {                             /* the motif: the last eight notes played */
            if (fm1_ms - an_motif_ms > 2500u) an_motif_n = 0;
            if (an_motif_n < 8u) an_motif[an_motif_n++] = (uint8_t)h;
            an_motif_ms = fm1_ms;
        }
    }
    if (!song.playing || chain_busy()) {
        an.beat_prev = (uint8_t)beat;
        if (an.pend_root >= 0 && !song.playing) {    /* stopped: at once */
            an_rekey((uint32_t)an.pend_root, (uint32_t)an.pend_scale);
            an.pend_root = an.pend_scale = -1;
            an.flash_ms = fm1_ms;
            an.said = "SHIFTED";
            ui_say("SHIFT TO ", AN_NOTE[an.root % 12u]);
        }
        return;
    }
    if (beat == an.beat_prev)
        return;
    an.beat_prev = (uint8_t)beat;
    if (an.pend_root >= 0) {                         /* SHIFT: on the beat */
        an_rekey((uint32_t)an.pend_root, (uint32_t)an.pend_scale);
        an.pend_root = an.pend_scale = -1;
        an.flash_ms = fm1_ms;
        an.said = "SHIFTED";
        ui_say("SHIFT TO ", AN_NOTE[an.root % 12u]);
        ui.force = 1;
    }
    if (beat != 0u)
        return;
    an.bars++;                                       /* a new bar */
    if (an.answer && an_motif_n >= 3u && fm1_ms - an_motif_ms > 1200u && an_melodic(&trk[3])) {
        track_t *t = &trk[3];                        /* the bells answer: the motif, turned around, on the chord */
        uint32_t k, n = an_motif_n, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), start = len > 16u ? len / 2u : 0u;
        for (k = 0; k < n && start + k * 2u < len; k++) {
            uint32_t src = an_motif[n - 1u - k], nn = an_snap(src + (k == n - 1u ? 0u : 12u), an.root, an.scale);
            step_t s = t->step[start + k * 2u];
            s.time = ST_NOTE; s.n = 1; s.note[0] = (uint8_t)nn; s.vel = 84; s.flags = 0; s.probability = 0;
            t->step[start + k * 2u] = s;
        }
        an_motif_n = 0;
        an.said = "ANSWERED";
        an.flash_ms = fm1_ms;
        ui.force = 1;
    }
    if (!an.on)
        return;
    if (!an.seeded)
        an_snapshot();
    if (an.bars % AN_EVERY[an.every % 3u] == 0u) {
        uint32_t i = 1u + an_pick(3);
        if (an_chance(20)) i = 0;
        an.last_trk = (uint8_t)i;
        an_vary(i);
        if (an_chance(35)) an_vary(3);
        an.auto_ms = fm1_ms;
        an.said = i == 0u ? "BEAT GROWS" : i == 1u ? "BASS MOVES" : i == 2u ? "PAD TURNS" : "BELLS WANDER";
        ui.force = 1;
    }
    if (an.bars % 4u == 0u)
        an_energy();
}

/* ------------------------------------------------------------------ the pages' knobs and OCT+ --- */
static int an_page(uint32_t g) { return g >= GR_AN_ALIGN && g <= GR_AN_VISION; }
static void an_knob(uint32_t g, uint32_t slot, int32_t steps)
{
    int32_t one = steps > 0 ? 1 : -1;
    if (g == GR_AN_ALIGN) {
        if (slot == 0u) an.mood = (uint8_t)an_step(an.mood, one, 0, AN_NMOODS - 1);
        if (slot == 1u) an.root = (uint8_t)((an.root + 12u + (steps > 0 ? 1u : 11u)) % 12u);
        if (slot == 2u) an.chaos = (uint8_t)an_step(an.chaos, steps * 2, 0, 100);
        if (slot == 3u) an.keep = (uint8_t)an_step(an.keep, one, 0, 3);
    } else if (g == GR_AN_AUTO) {
        if (slot == 0u) { uint32_t was = an.on; an.on = (uint8_t)an_step(an.on, one, 0, 1); if (an.on && !was) { an_snapshot(); an.bars = 0; } if (!an.on) { an.mutes = 0; an_apply_fx(); } }
        if (slot == 1u) an.amount = (uint8_t)an_step(an.amount, steps * 2, 0, 100);
        if (slot == 2u) { an.energy = (uint8_t)an_step(an.energy, steps * 2, 0, 100); if (an.on) an_energy(); }
        if (slot == 3u) an.every = (uint8_t)an_step(an.every, one, 0, 2);
    } else if (g == GR_AN_KEYS) {
        if (slot == 0u) { uint32_t i; an.kmode = (uint8_t)an_step(an.kmode, one, 0, 2); for (i = 0; i < NTRK; i++) if (an_melodic(&trk[i])) trk[i].p[P_QUANT] = an.kmode == 1u ? 1 : 0; }
        if (slot == 1u) an.root = (uint8_t)((an.root + 12u + (steps > 0 ? 1u : 11u)) % 12u);
        if (slot == 2u) { uint32_t f = 0, k; for (k = 0; k < 7u; k++) if (AN_FAMILY[k] == an.scale) f = k; an.scale = AN_FAMILY[(f + 7u + (steps > 0 ? 1u : 6u)) % 7u]; }
        if (slot == 3u) an.answer = (uint8_t)an_step(an.answer, one, 0, 1);
    } else if (g == GR_AN_SKY) {
        if (slot == 0u) an.drift = (int8_t)an_step(an.drift, steps * 2, 0, 100);
        if (slot == 1u) an.haze = (int8_t)an_step(an.haze, steps * 2, 0, 100);
        if (slot == 2u) an.bloom = (int8_t)an_step(an.bloom, steps * 2, 0, 100);
        if (slot == 3u) an.gravity = (int8_t)an_step(an.gravity, steps * 3, -100, 100);
        if (slot == 0u) song.g[G_DRREV] = (int16_t)(an.drift * 127 / 100);
        an_apply_fx();
    } else if (g == GR_AN_TAPE) {
        if (slot == 0u) { an.tmode = (uint8_t)an_step(an.tmode, one, 0, 11); an_tape_set(); }
        if (slot == 1u) song.g[G_DTIME] = (int16_t)an_step(song.g[G_DTIME], one, 0, 12);
        if (slot == 2u) song.g[G_DFDBK] = (int16_t)an_step(song.g[G_DFDBK], steps * 2, 0, 120);
        if (slot == 3u) { song.g[G_DRREV] = (int16_t)an_step(song.g[G_DRREV], steps * 2, 0, 127); an.drift = (int8_t)(song.g[G_DRREV] * 100 / 127); }
    } else {
        if (slot == 0u) an.vision = (uint8_t)an_step(an.vision, one, 0, 3);
        if (slot == 3u) {                            /* LANG: EN / ES, the whole screen (lang.c), kept in the settings */
            uint32_t es = steps > 0 ? 1u : 0u;
            if (es != lang_es) {
                lang_es = (uint8_t)es;
                ui_idle = (uint8_t)(es ? ui_idle | 0x80u : ui_idle & 0x7Fu);
                settings_later();
            }
        }
    }
    ui.force = 1;
}
static void an_oct(void)
{
    uint32_t g = cur_page()->graph;
    if (chain_busy() && g < GR_AN_SKY) { ui_message("STOP TO EDIT"); return; }
    if (g == GR_AN_ALIGN) {
        an_align();
    } else if (g == GR_AN_AUTO) {
        an_back();
    } else if (g == GR_AN_KEYS) {                    /* APPLY: every track into this key and scale */
        uint32_t i;
        for (i = 0; i < NTRK; i++) load_begin(&trk[i], UNDO_PAT);
        an_rekey(an.root % 12u, an.scale);
        for (i = 0; i < NTRK; i++) { an_keys(&trk[i]); load_end(&trk[i]); }
        an.flash_ms = fm1_ms;
        ui_say("KEY ", AN_NOTE[an.root % 12u]);
    } else if (g == GR_AN_SKY) {
        an.eclipse ^= 1u;
        an_apply_fx();
        an.flash_ms = fm1_ms;
        ui_say(an.eclipse ? "ECLIPSE" : "THE BEAT RETURNS", "");
    } else if (g == GR_AN_TAPE) {
        an.freeze ^= 1u;
        an_apply_fx();
        an.flash_ms = fm1_ms;
        ui_say(an.freeze ? "FREEZE" : "THAW", "");
    } else {
        an.vision = (uint8_t)((an.vision + 1u) % 4u);
    }
    ui.force = 1;
}
static const char *an_act_name(void)
{
    uint32_t g = cur_page()->graph;
    return g == GR_AN_ALIGN ? "ALIGN" : g == GR_AN_AUTO ? "BACK" : g == GR_AN_KEYS ? "APPLY" : g == GR_AN_SKY ? (an.eclipse ? "RETURN" : "ECLIPSE") :
           g == GR_AN_TAPE ? (an.freeze ? "THAW" : "FREEZE") : "NEXT";
}

/* ------------------------------------------------------------------ the visions --- */
static int32_t an_level(void)                         /* 0..256: how loud it plays */
{
    int32_t m = 0, i;
    for (i = 0; i < NTRK; i++)
        m = trk[i].peak > m ? trk[i].peak : m;
    m >>= 7;
    return song.playing ? (m > 256 ? 256 : m) : 0;
}
static uint32_t an_chord_deg(void)                   /* the pad's chord now: a number to shape the figures with */
{
    const track_t *p = &trk[2];
    uint32_t len = (uint32_t)clamp(p->p[P_SLEN], 1, NSTEP), k = (uint32_t)p->seq_idx % len;
    while (k && p->step[k].time != ST_NOTE) k--;
    return p->step[k].time == ST_NOTE ? p->step[k].note[0] % 12u : 0u;
}
/* CYMATICS: sand on a plate, cos(n pi x) cos(m pi y) - cos(m pi x) cos(n pi y) = 0, (n, m) from the chord; the grains
 * sit where the plate is still, trembling with the level */
static void an_cymatics(int32_t x0, int32_t y0, int32_t w, int32_t h, uint16_t c, uint16_t faint)
{
    static const uint8_t NM[12][2] = {{1, 2}, {2, 3}, {2, 5}, {3, 4}, {1, 4}, {4, 5}, {3, 8}, {2, 7}, {1, 6}, {4, 7}, {5, 6}, {3, 2}};   /* (one odd, one even: no blots) */
    uint32_t ch = an_chord_deg(), n = NM[ch][0], m = NM[ch][1], z = (fm1_ms / 60u) * 2654435761u + 7u;
    int32_t gx, gy, lv = an_level(), side = w < h ? w : h, ox = x0 + (w - side) / 2, oy = y0 + (h - side) / 2, step = side > 80 ? 2 : 3;
    for (gy = 0; gy < side; gy += step)
        for (gx = 0; gx < side; gx += step) {
            int32_t ax = gx * 128 / side, ay = gy * 128 / side;   /* 0..128: half a turn of AN_S (256) */
            int32_t v = (AN_C((int32_t)n * ax) * AN_C((int32_t)m * ay) - AN_C((int32_t)m * ax) * AN_C((int32_t)n * ay)) >> 14;
            v = v < 0 ? -v : v;
            if (v < 900 + lv * 3) {
                int32_t j = (int32_t)((z >> 13) % 3u) - 1;
                z = z * 1664525u + 1013904223u;
                cv_pset(ox + gx + (lv > 60 ? j : 0), oy + gy, v < 450 ? c : faint);
            }
        }
    cv_rect(ox - 2, oy - 2, side + 4, 1, faint);              /* the plate */
    cv_rect(ox - 2, oy + side + 1, side + 4, 1, faint);
    cv_rect(ox - 2, oy - 2, 1, side + 4, faint);
    cv_rect(ox + side + 1, oy - 2, 1, side + 4, faint);
}
/* HARMONOGRAPH: two damped pendulums, their ratio from the chord's interval, drifting slowly */
static void an_harm(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint16_t faint)
{
    static const uint8_t RA[12][2] = {{1, 2}, {2, 3}, {3, 4}, {4, 5}, {2, 3}, {3, 5}, {5, 8}, {2, 3}, {5, 6}, {3, 4}, {4, 7}, {5, 7}};
    uint32_t ch = an_chord_deg();
    int32_t a = RA[ch][0], b = RA[ch][1], k, px = 0, py = 0, drift = (int32_t)(fm1_ms / 90u), lv = an_level();
    for (k = 0; k <= 240; k++) {
        int32_t t = k * 4, e = 256 - k * 190 / 240;
        int32_t x = (AN_S(a * t + drift) + AN_S(b * t + 64)) * r / 32768 * e / 256;
        int32_t y = (AN_S(b * t) + AN_S(a * t + drift / 2 + 32)) * r / 32768 * e / 256;
        if (k) an_line(cx + px, cy + py, cx + x, cy + y, 1, k < 60 ? c : faint);
        px = x; py = y;
    }
    an_disc(cx, cy, 2 + lv / 80, T_THEME);
}
/* ATTRACTOR: x' = sin(a y) + c cos(a x), y' = sin(b x) + d cos(b y), its parameters breathing with the bar */
static void an_attr(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t k, x = 400, y = 0, ph = (int32_t)(fm1_ms / 40u) & 255;
    int32_t a = -57 + AN_S(ph) / 2000, b = 65 + AN_C(ph) / 2400;      /* -1.4 and 1.6 rad (256 a turn), breathing */
    for (k = 0; k < 900; k++) {                                     /* x, y in Q12; the sines Q14 */
        int32_t nx = AN_S(a * y / 4096) + AN_C(a * x / 4096);
        int32_t ny = AN_S(b * x / 4096) + AN_C(b * y / 4096) * 7 / 10;
        x = nx / 4;
        y = ny / 4;
        if (k > 8) cv_pset(cx + x * r / 8192, cy + y * r / 8192, c);
    }
}
/* ORRERY: the four tracks as planets round the key, each bright when it plays */
static void an_orrery(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint16_t faint)
{
    int32_t i;
    an_disc(cx, cy, 3, T_THEME);
    for (i = 0; i < 4; i++) {
        int32_t rr = r * (i + 2) / 5, sp = 9 + i * 7, a = (int32_t)(fm1_ms / (uint32_t)sp) + i * 64, pk = trk[i].peak >> 9;
        an_ring(cx, cy, rr, 256, 0, 0, 1, faint);
        an_disc(cx + rr * AN_C(a) / 16384, cy + rr * AN_S(a) / 16384, 2 + (pk > 4 ? 4 : pk), song.playing && pk ? c : faint);
    }
}
static void an_vision(uint32_t v, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    uint16_t c = T_TEXT, faint = ux_mix(cv_bg, T_TEXT, 34);
    int32_t r = (w < h ? w : h) / 2 - 2;
    if (v == 0u) an_cymatics(x0, y0, w, h, c, faint);
    else if (v == 1u) an_harm(x0 + w / 2, y0 + h / 2, r, c, faint);
    else if (v == 2u) an_attr(x0 + w / 2, y0 + h / 2, r, c);
    else an_orrery(x0 + w / 2, y0 + h / 2, r, c, faint);
}
static void an_home_vision(void)                     /* HOME: a small harmonograph, top right */
{
    uint16_t faint = ux_mix(cv_bg, T_TEXT, 34);
    an_harm(212, 30, 20, T_TEXT, faint);
}

/* ------------------------------------------------------------------ the graph strip of the pages --- */
static void an_bar(int32_t x, int32_t y, int32_t w, uint32_t pct)
{
    cv_rect(x, y, w, 4, ux_mix(cv_bg, T_TEXT, 20));
    cv_rect(x, y, w * (int32_t)pct / 100, 4, T_THEME);
}
static void an_tape_draw(int32_t x0, int32_t y0, int32_t w, int32_t h)   /* the loop of tape, the heads lit */
{
    static const uint8_t HB[8] = {0, 1, 2, 4, 3, 6, 5, 7};
    uint32_t m = an.tmode % 12u, heads = HB[AN_TAPE_HEADS[m] & 7u], k;
    int32_t cx = x0 + w / 2, cy = y0 + h / 2 - 4, rx = w / 2 - 8, ry = h / 2 - 12, moving = song.playing && !an.freeze;
    uint16_t tape = ux_mix(cv_bg, T_THEME, 70), faint = ux_mix(cv_bg, T_TEXT, 30);
    int32_t ph = moving ? (int32_t)(fm1_ms / (uint32_t)(14 + song.g[G_DTIME] * 2)) : 0;
    for (k = 0; k < 64u; k++) {                       /* the loop: an ellipse of dashes moving round */
        int32_t a = (int32_t)k * 4, x = cx + rx * AN_C(a) / 16384, y = cy + ry * AN_S(a) / 16384;
        cv_rect(x, y, 2, 2, ((k + (uint32_t)ph) & 3u) ? tape : faint);
    }
    for (k = 0; k < 4u; k++) {                        /* REC, then the three playback heads under the loop */
        int32_t hx = cx - rx / 2 + (int32_t)k * rx / 3, lit = k == 0u ? song.playing : (int32_t)((heads >> (k - 1u)) & 1u);
        cv_rrect(hx - 6, cy + ry - 3, 12, 9, 2, lit ? (k ? T_ACCENT : T_THEME) : faint, cv_bg);
    }
    if (m >= 4u)                                      /* the spring, trembling */
        for (k = 0; k < 6u; k++) {
            int32_t sx = cx - 18 + (int32_t)k * 7, j = song.playing ? (int32_t)((fm1_ms / 50u + k) % 3u) - 1 : 0;
            an_line(sx, cy - 6 + j, sx + 4, cy + 6 - j, 1, T_TEXT);
        }
}
static void an_graph(uint32_t g)
{
    int32_t h = (int32_t)graph_h(), small = h < 90, vw = small ? 56 : 92, tx = vw + 10;
    uint32_t fl = fm1_ms - an.flash_ms < 700u;
    char b[28];
    const an_mood_t *m = &AN_MOODS[an.mood % AN_NMOODS];
    if (g == GR_AN_VISION) {                          /* the whole strip */
        an_vision(an.vision, 4, 2, (int32_t)cv_w - 8, h - 4);
        if (!small) cv_text_on(6, 4, &AF_S, AN_VIS[an.vision & 3u], T_MID, cv_bg);
        return;
    }
    if (g == GR_AN_TAPE) {
        an_tape_draw(0, 0, (int32_t)cv_w, h);
        str_cpy(b, "MODE ", sizeof b);
        fmt_int(b + 5, an.tmode + 1u);
        cv_text_on(6, 4, &AF_S, b, T_TEXT, cv_bg);
        cv_text_on((int32_t)cv_w - 6 - text_w(&AF_S, an.freeze ? "FROZEN" : an.tmode >= 4u ? "+ SPRING" : "ECHO"), 4, &AF_S,
                   an.freeze ? "FROZEN" : an.tmode >= 4u ? "+ SPRING" : "ECHO", an.freeze ? T_ACCENT : T_MID, cv_bg);
        return;
    }
    an_vision(g == GR_AN_SKY ? 3u : g == GR_AN_KEYS ? 0u : g == GR_AN_AUTO ? 2u : 1u, 2, 2, vw, h - 4);
    if (g == GR_AN_ALIGN) {
        cv_text_on(tx, small ? 4 : 12, &AF_M, m->name, fl ? T_ACCENT : T_TEXT, cv_bg);
        if (!small) {
            cv_text_on(tx, 36, &AF_S, m->desc, T_MID, cv_bg);
            str_cpy(b, AN_NOTE[an.root % 12u], sizeof b);
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), N_SCALE[m->scale % 16u], 8);
            cv_text_on(tx, 56, &AF_M, b, T_THEME, cv_bg);
            cv_text_on(tx, 88, &AF_S, "[OCT+] ALIGN", T_MID, cv_bg);
        } else {
            cv_text_on(tx, 30, &AF_S, AN_NOTE[an.root % 12u], T_THEME, cv_bg);
        }
    } else if (g == GR_AN_AUTO) {
        uint32_t k, every = AN_EVERY[an.every % 3u], left = every - an.bars % every;
        cv_text_on(tx, small ? 4 : 12, &AF_M, an.on ? an.said : "AUTO IS OFF", an.on ? (fl || fm1_ms - an.auto_ms < 900u ? T_ACCENT : T_TEXT) : T_MID, cv_bg);
        if (!small) {
            static const char *const TN[4] = {"BEAT", "BASS", "PAD", "BELL"};
            for (k = 0; k < 4u; k++) {                /* the four parts: lit in, dim out (ENERGY) */
                int32_t x = tx + (int32_t)k * 32, in = !((an.mutes >> k) & 1u);
                cv_rrect(x, 40, 28, 12, 3, in ? (an.last_trk == k && fm1_ms - an.auto_ms < 900u ? T_ACCENT : T_TEXT) : ux_mix(cv_bg, T_TEXT, 20), cv_bg);
                cv_text_on(x + 1, 56, &AF_S, TN[k], T_MID, cv_bg);
            }
            if (an.on && song.playing) {
                str_cpy(b, "NEXT IN ", sizeof b);
                fmt_int(b + 8, left);
                cv_text_on(tx, 74, &AF_S, b, T_THEME, cv_bg);
            }
            cv_text_on(tx, 88, &AF_S, "[OCT+] BACK", T_MID, cv_bg);
        }
    } else if (g == GR_AN_KEYS) {
        str_cpy(b, AN_NOTE[an.root % 12u], sizeof b);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), N_SCALE[an.scale % 16u], 8);
        cv_text_on(tx, small ? 4 : 12, &AF_M, b, fl ? T_ACCENT : T_TEXT, cv_bg);
        if (!small) {
            uint32_t k, msk = an_mask();
            for (k = 0; k < 12u; k++) {               /* the twelve notes, the scale's lit */
                int32_t on = (int32_t)((msk >> k) & 1u), x = tx + (int32_t)k * 10;
                cv_rect(x, 40, 8, on ? 14 : 6, on ? (k == 0u ? T_ACCENT : T_THEME) : ux_mix(cv_bg, T_TEXT, 20));
            }
            cv_text_on(tx, 62, &AF_S, an.pend_root >= 0 ? "ON THE NEXT BEAT" : an.kmode == 2u ? "PLAY OFF-SCALE" :
                                      an.kmode == 1u ? "ALWAYS IN KEY" : "AS PLAYED", T_MID, cv_bg);
            cv_text_on(tx, 88, &AF_S, "[OCT+] APPLY", T_MID, cv_bg);
        }
    } else {                                          /* SKY */
        cv_text_on(tx, small ? 4 : 12, &AF_M, an.eclipse ? "ECLIPSE" : "THE SKY", an.eclipse ? T_ACCENT : T_TEXT, cv_bg);
        if (!small) {
            static const char *const SL[4] = {"D", "H", "B", "G"};
            uint32_t k;
            for (k = 0; k < 4u; k++)
                cv_text_on(tx, 36 + (int32_t)k * 11, &AF_S, SL[k], T_MID, cv_bg);
            an_bar(tx + 14, 40, 100, (uint32_t)an.drift);
            an_bar(tx + 14, 51, 100, (uint32_t)an.haze);
            an_bar(tx + 14, 62, 100, (uint32_t)an.bloom);
            cv_rect(tx + 14, 73, 100, 4, ux_mix(cv_bg, T_TEXT, 20));
            cv_rect(tx + 64 + (an.gravity < 0 ? an.gravity * 50 / 100 : 0), 73, (an.gravity < 0 ? -an.gravity : an.gravity) * 50 / 100 + 1, 4, T_THEME);
            cv_text_on(tx, 88, &AF_S, an.eclipse ? "[OCT+] RETURN" : "[OCT+] ECLIPSE", T_MID, cv_bg);
        }
    }
}
/* the four knobs' cards */
static void an_columns(uint32_t g)
{
    char v[12];
    if (g == GR_AN_ALIGN) {
        draw_column(0, "MOOD", AN_MOODS[an.mood % AN_NMOODS].name, "", VAL(0u), (int32_t)an.mood * 1000 / (int32_t)(AN_NMOODS - 1), ICON_AUTO);
        draw_column(1, "KEY", AN_NOTE[an.root % 12u], "", VAL(1u), (int32_t)an.root * 1000 / 11, ICON_AUTO);
        fmt_int(v, an.chaos);
        draw_column(2, "CHAOS", v, "%", VAL(2u), an.chaos * 10, ICON_AUTO);
        draw_column(3, "KEEP", AN_KEEP[an.keep & 3u], "", VAL(3u), (int32_t)an.keep * 333, ICON_AUTO);
    } else if (g == GR_AN_AUTO) {
        draw_column(0, "AUTO", an.on ? "ON" : "OFF", "", VAL(0u), an.on ? 1000 : 0, ICON_AUTO);
        fmt_int(v, an.amount);
        draw_column(1, "AMOUNT", v, "%", VAL(1u), an.amount * 10, ICON_AUTO);
        fmt_int(v, an.energy);
        draw_column(2, "ENERGY", v, "%", VAL(2u), an.energy * 10, ICON_AUTO);
        draw_column(3, "EVERY", AN_EVERY_N[an.every % 3u], "", VAL(3u), (int32_t)an.every * 500, ICON_AUTO);
    } else if (g == GR_AN_KEYS) {
        draw_column(0, "MODE", AN_KMODE[an.kmode % 3u], "", VAL(0u), (int32_t)an.kmode * 500, ICON_AUTO);
        draw_column(1, "KEY", AN_NOTE[an.root % 12u], "", VAL(1u), (int32_t)an.root * 1000 / 11, ICON_AUTO);
        draw_column(2, "SCALE", N_SCALE[an.scale % 16u], "", VAL(2u), -1, ICON_AUTO);
        draw_column(3, "ANSWER", an.answer ? "ON" : "OFF", "", VAL(3u), an.answer ? 1000 : 0, ICON_AUTO);
    } else if (g == GR_AN_SKY) {
        fmt_int(v, (uint32_t)an.drift);
        draw_column(0, "DRIFT", v, "%", VAL(0u), an.drift * 10, ICON_AUTO);
        fmt_int(v, (uint32_t)an.haze);
        draw_column(1, "HAZE", v, "%", VAL(1u), an.haze * 10, ICON_AUTO);
        fmt_int(v, (uint32_t)an.bloom);
        draw_column(2, "BLOOM", v, "%", VAL(2u), an.bloom * 10, ICON_AUTO);
        draw_column(3, "GRAVITY", an.gravity < -2 ? "LOW" : an.gravity > 2 ? "HIGH" : "-", "", VAL(3u), (an.gravity + 100) * 5, ICON_AUTO);
    } else if (g == GR_AN_TAPE) {
        fmt_int(v, an.tmode + 1u);
        draw_column(0, "MODE", v, "", VAL(0u), (int32_t)an.tmode * 1000 / 11, ICON_AUTO);
        draw_column(1, "RATE", N_DTIME[clamp(song.g[G_DTIME], 0, 12)], "", VAL(1u), song.g[G_DTIME] * 1000 / 12, ICON_AUTO);
        fmt_int(v, (uint32_t)(song.g[G_DFDBK] * 100 / 120));
        draw_column(2, "INTENS", v, "%", VAL(2u), song.g[G_DFDBK] * 1000 / 120, ICON_AUTO);
        fmt_int(v, (uint32_t)(song.g[G_DRREV] * 100 / 127));
        draw_column(3, "WOW", v, "%", VAL(3u), song.g[G_DRREV] * 1000 / 127, ICON_AUTO);
    } else {
        draw_column(0, "VISION", AN_VIS_K[an.vision & 3u], "", VAL(0u), (int32_t)an.vision * 333, ICON_AUTO);
        draw_column(1, "-", "", "", VAL(1u), -1, ICON_AUTO);
        draw_column(2, "-", "", "", VAL(2u), -1, ICON_AUTO);
        draw_column(3, "LANG", lang_es ? "ES" : "EN", "", VAL(3u), lang_es ? 1000 : 0, ICON_AUTO);
    }
}
static uint32_t an_sig(void)
{
    return (uint32_t)an.mood * 3u + an.root * 37u + an.chaos * 101u + an.keep * 1009u + an.on * 7u + an.amount * 17u + an.energy * 131u +
           an.every * 1301u + an.kmode * 13u + an.scale * 19u + an.answer * 23u + (uint32_t)an.drift * 29u + (uint32_t)an.haze * 41u +
           (uint32_t)an.bloom * 43u + (uint32_t)(an.gravity + 100) * 47u + an.tmode * 53u + an.vision * 59u + an.eclipse * 61u + an.freeze * 67u +
           an.flash_ms + an.auto_ms + an.mutes * 71u + (uint32_t)(an.pend_root + 1) * 73u + an.bars * 79u;
}
