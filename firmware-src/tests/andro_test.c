/* SPDX-License-Identifier: GPL-3.0-only */
/* ANDROMEDA's pages (src/andro.c) on the real UI sources (with the stubs of tests/ui_test.c):
 *   ALIGN    every mood writes four tracks: their engines, every melodic note in the key, the pad a chord a bar, the
 *            tempo in the mood's range; KEEP SOUND keeps the engines
 *   SHIFT    a note outside the scale finds the nearest scale holding it (C MAJ + F# -> G MAJ), on the next beat every
 *            melodic note moves onto it; stopped: at once
 *   AUTO     a hundred variations grown from the seed: every melodic note still in the key, the pad's chords keep their
 *            notes (an inversion), the beat keeps its kick on 1; BACK returns the seed exactly
 *   SKY      the macros and ECLIPSE / FREEZE in the FX layer's knobs and held effects, all off again
 *   TAPE     the twelve modes: heads and spring as the selector
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int all_in_key(uint32_t root, uint32_t scale)
{
    uint32_t i, k, j;
    for (i = 0; i < NTRK; i++) {
        if (!an_melodic(&trk[i])) continue;
        for (k = 0; k < (uint32_t)trk[i].p[P_SLEN] && k < NSTEP; k++)
            if (trk[i].step[k].time == ST_NOTE)
                for (j = 0; j < trk[i].step[k].n && j < 4u; j++)
                    if (!an_in_scale(trk[i].step[k].note[j], root, scale)) {
                        printf("  T%u step %u note %u out of %u/%u\n", (unsigned)i + 1u, (unsigned)k, trk[i].step[k].note[j], (unsigned)root, (unsigned)scale);
                        return 0;
                    }
    }
    return 1;
}

static int align_all(void)
{
    int bad = 0, ok = 1;
    uint32_t m, k;
    for (m = 0; m < AN_NMOODS; m++) {
        const an_mood_t *md = &AN_MOODS[m];
        an.mood = (uint8_t)m; an.keep = 0; an.root = (uint8_t)(m * 5u % 12u); an.chaos = (uint8_t)(m * 8u);
        an_align();
        ok &= trk[1].eng_req == md->b_eng && trk[2].eng_req == md->p_eng && trk[3].eng_req == md->l_eng && trk[0].eng_req == md->t1_eng;
        ok &= song.g[G_BPM] >= md->bpm_lo && song.g[G_BPM] <= md->bpm_hi;
        ok &= all_in_key(an.root, an.scale);
        for (k = 0; k < 4u; k++)
            ok &= trk[2].step[k].time == ST_NOTE && trk[2].step[k].n >= 3u;
        ok &= trk[2].p[P_SLEN] == 4 && trk[2].p[P_SDIV] == 7 && trk[1].p[P_SLEN] == 32 && trk[3].p[P_SLEN] == 32;
        ok &= an.seeded;
        if (!ok) { printf("  mood %s\n", md->name); break; }
    }
    bad += check("ALIGN: every mood writes T1..T4 (engines, tempo range, every note in the key, a chord a bar, seeded)", ok);
    an.mood = 0; an_align();
    k = trk[3].eng_req;
    an.mood = 3; an.keep = 1; an_align();
    ok = trk[3].eng_req == k && all_in_key(an.root, an.scale);
    an.keep = 0;
    bad += check("  KEEP SOUND: the engines stay, the notes are written in the new mood's key", ok);
    return bad;
}

static int shift(void)
{
    int bad = 0, ok;
    an.mood = 1; an_align();                            /* (TIDAL: melodic tracks) */
    an.root = 0; an.scale = 1;                          /* C MAJ */
    an_rekey(0, 1);
    an.kmode = 2; an.pend_root = an.pend_scale = -1;
    an_find_shift(66);                                  /* F#4 */
    ok = an.pend_root == 7 && an.pend_scale == 1;
    bad += check("SHIFT: C MAJ + F# -> G MAJ (one note changed, the nearest)", ok);
    song.playing = 0;
    an_heard = 0;
    andro_poll();                                       /* stopped: at once */
    ok = an.root == 7 && an.scale == 1 && an.pend_root < 0 && all_in_key(7, 1) && trk[3].p[P_ROOT] == 7;
    bad += check("  stopped: at once, every melodic note onto G MAJ, the tracks' ROOT", ok);
    an_heard = (uint8_t)(61u + 1u);                     /* C#: in G MAJ? no (F# yes) -> D MAJ */
    andro_poll();
    ok = an.root == 2 && an.scale == 1 && all_in_key(2, 1);
    bad += check("  played C#: D MAJ next (the circle of fifths, one step)", ok);
    an_heard = (uint8_t)(64u + 1u);                     /* E is in D MAJ: nothing */
    andro_poll();
    ok = an.root == 2 && an.scale == 1;
    bad += check("  a note in the scale: nothing moves", ok);
    an.kmode = 1;
    return bad;
}

static int autov(void)
{
    int bad = 0, ok = 1;
    uint32_t r, k, j;
    step_t pad0[4];
    an.mood = 7; an.root = 9; an_align();               /* LULLABY in A */
    memcpy(pad0, trk[2].step, sizeof pad0);
    an.on = 1; an.amount = 100; an_snapshot();
    for (r = 0; r < 100u && ok; r++) {
        an.bars = r;
        an_vary(r & 3u);
        ok &= all_in_key(an.root, an.scale);
        ok &= (trk[0].step[0].hit & 1u) || trk[0].step[0].time == ST_NOTE;
        for (k = 0; k < 4u; k++)                        /* the pad: the same pitch classes */
            for (j = 0; j < pad0[k].n; j++)
                ok &= trk[2].step[k].note[j] % 12u == pad0[k].note[j] % 12u;
    }
    bad += check("AUTO: 100 variations at AMOUNT 100: every note in the key, the pad's chords the same notes, the kick on 1", ok);
    an_back();
    ok = !memcmp(trk[1].step, an_seed[1], sizeof an_seed[1]) && !memcmp(trk[3].step, an_seed[3], sizeof an_seed[3]) && an.mutes == 0;
    bad += check("  BACK: the seed exactly, every part in", ok);
    for (r = 0; r < 20u; r++) { an.energy = (uint8_t)(r * 5u); an_energy(); ok &= !((an.mutes >> 2) & 1u); }
    an.energy = 0; an_energy();
    ok &= __builtin_popcount(an.mutes) == 3;
    an.energy = 100; an_energy();
    ok &= an.mutes == 0;
    bad += check("  ENERGY: the pad always in; 0 leaves the pad alone, 100 everything", ok);
    an.on = 0; an.mutes = 0; an_apply_fx();
    return bad;
}

static int sky_tape(void)
{
    int bad = 0, ok;
    uint32_t m;
    an.gravity = -40; an.haze = 50; an.bloom = 80; an.eclipse = 1; an.freeze = 1;
    an_apply_fx();
    ok = perf_k[0] == -40 && perf_k[1] == 27 && perf_k[2] == 80 && (an_live_fx & PF_BIT(PF_FRZ)) && (an_live_fx & PF_BIT(PF_ETHR)) &&
         ((an_live_fx >> PF_M1) & 3u) == 3u;
    an.gravity = an.haze = an.bloom = 0; an.eclipse = an.freeze = 0;
    an_apply_fx();
    ok &= perf_k[0] == 0 && perf_k[1] == 0 && perf_k[2] == 0 && an_live_fx == 0;
    bad += check("SKY: GRAVITY HAZE BLOOM in the FX layer's knobs, ECLIPSE (drums, bass out, throws) and FREEZE held; all off again", ok);
    ok = 1;
    for (m = 0; m < 12u; m++) {
        an.tmode = (uint8_t)m; an_tape_set();
        ok &= song.g[G_DRLVL] == AN_TAPE_HEADS[m] && song.g[G_RTYPE] == (m >= 4u ? 1 : 2);
    }
    ok &= AN_TAPE_HEADS[11] == 0 && AN_TAPE_HEADS[10] == 7;
    bad += check("SPACE TAPE: 12 modes, the heads and the spring as the selector (12: the spring alone)", ok);
    return bad;
}

static int lang(void)
{
    int bad = 0, ok;
    uint32_t i;
    for (i = 1; i < LANG_N; i++)                          /* (sorted, one entry a key: lang_tr's binary search) */
        if (lang_cmp(LANG_ES[i - 1u].en, LANG_ES[i].en) >= 0) break;
    ok = i == LANG_N && lang_es == 0 && !strcmp(lang_tr("SAVE"), "SAVE");
    an_knob(GR_AN_VISION, 3, 1);                          /* VISION > K4 LANG: ES */
    ok &= lang_es == 1 && (ui_idle & 0x80u) && !strcmp(lang_tr("SAVE"), "SALVA") && !strcmp(lang_tr("MY LONG BASS"), "MY LONG BASS");
    ui_say("SHIFT TO ", "D");
    ok &= !strcmp(ui.msg, "CAMBIO A D") && text_w(&AF_S, "ALIGN") == text_w(&AF_S, "ALINEAR");
    andro_poll();
    ok &= lang_es == 1;                                   /* (the settings' bit read back) */
    an_knob(GR_AN_VISION, 3, -1);                         /* EN again */
    ok &= lang_es == 0 && !(ui_idle & 0x80u) && !strcmp(lang_tr("SAVE"), "SAVE");
    bad += check("LANG: VISION K4 EN / ES, kept in the settings; the screen and messages in Spanish, names untouched", ok);
    return bad;
}

int main(void)
{
    int bad = align_all() + shift() + autov() + sky_tape() + lang();
    printf("%s\n", bad ? "ANDROMEDA TEST FAILED" : "andromeda tests passed");
    return bad != 0;
}
