/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE: the instrument (omni.h). One compilation unit, -O2. The strings are gtr/engine.c, the
 * bass bass.c; this is the arranger: the right hand's patterns, the bass's, the progression's clock,
 * the count-in and click, and the room (FoMni's Dattorro plate). */
#include "omni.h"
#include "fastmath.h"
#include "bass.h"
#include "gtr/engine.h"

#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ============================================================== theory === */
const char *const OM_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static void scat(char *b, const char *s)
{
    while (*b)
        b++;
    while ((*b++ = *s++))
        ;
}

void om_chord_quality(const uint8_t *mn, int n, char *q)
{
    int has_v[32] = {0}, i, s = -1, im, iD, iA, i4, i2;
    q[0] = 0;
    if (n < 3)
        return;
    for (i = 1; i < n; i++) {
        int d = mn[i] - mn[0];
        if (d >= 0 && d < 32)
            has_v[d] = 1;
    }
    im = has_v[3] && has_v[7];
    iD = has_v[3] && has_v[6];
    iA = has_v[4] && has_v[8];
    i4 = has_v[5] && has_v[7];
    i2 = has_v[2] && has_v[7];
    scat(q, iD ? "dim" : iA ? "aug" : im ? "m" : i4 ? "sus4" : i2 ? "sus2" : "");
    if (n >= 4) {
        for (i = 9; i <= 11; i++)
            if (has_v[i]) {
                s = i;
                break;
            }
        if (s == 11) {
            if (!iD)
                scat(q, "maj7");
        } else if (s == 10) {
            if (iD) {
                q[0] = 0;
                scat(q, "m7b5");
            } else {
                scat(q, "7");
            }
        } else if (s == 9 && iD) {
            q[0] = 0;
            scat(q, "dim7");
        }
    }
}

void om_roman(int deg, int minor, char *b)
{
    static const char *const ROM[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    const char *r = ROM[(unsigned)deg < 7u ? deg : 0];
    while (*r) {
        *b++ = minor ? (char)(*r + ('a' - 'A')) : *r;
        r++;
    }
    *b = 0;
}

/* ============================================================ the names === */
/* the right hand. A bar of sixteenths: D a down strum, U up (the top four strings), d down the low
 * three, u up the top three, K all at once, C a chop (muted, short), B the bass string, b the next
 * one up, 1-6 the n-th sounding string from the bottom, . nothing */
const char *const OM_PATTERN_NAME[NPATTERNS] = {"Off",    "Block",    "Down 4s",  "Down 8s", "Down-Up",
                                                "Folk",   "Rock",     "Ballad",   "Reggae",  "Ska",
                                                "Boom-chick", "Travis", "Arp",    "Arp 16s"};
static const char *const PATTERN[NPATTERNS] = {
    "................", "K...K...K...K...", "D...D...D...D...", "D.D.D.D.D.D.D.D.", "D.U.D.U.D.U.D.U.",
    "D...D.U...U.D.U.", "D.D.D.DUD.D.D.DU", "D.......D...D.U.", "....C.......C...", "..C...C...C...C.",
    "B...d.u.b...d.u.", "B.4.b.5.B.4.b.6.", "1.2.3.4.5.6.5.4.", "1234565412345654"};
/* the bass, a bar of sixteenths: R the root, 3 the third, 5 the fifth, O the octave, A a step into the
 * next chord's root, - let go, . the note goes on. Follow: the root under the right hand's strums. */
const char *const OM_BASSPAT_NAME[NBASSPAT] = {"Off", "Follow", "Root 4s", "Root 8s", "Root-5th", "Octaves", "Walk",
                                               "Pump"};
static const char *const BASSPAT[NBASSPAT] = {"................", "", "R...R...R...R...", "R.R.R.R.R.R.R.R.",
                                              "R.......5.......", "R.O.R.O.R.O.R.O.", "R...3...5...A...",
                                              "R-R-R-R-R-R-R-R-"};
const char *const OM_TUNING_NAME[NTUNINGS] = {"Standard", "Drop D", "Open G", "Open D", "DADGAD", "Half down"};
const int8_t OM_TUNING[NTUNINGS][6] = {{40, 45, 50, 55, 59, 64}, {38, 45, 50, 55, 59, 64}, {38, 43, 50, 55, 59, 62},
                                       {38, 45, 50, 54, 57, 62}, {38, 45, 50, 55, 57, 62}, {39, 44, 49, 54, 58, 63}};
int om_ninstr(void) { return gm_tone_count(); }
const char *om_instr_name(int i) { return gm_tone_name(i); }
const char *om_bass_name(int i) { return BASS_SOUND_NAME[(unsigned)i < BASS_NSOUNDS ? i : 0]; }

/* ========================================================== parameters === */
static const om_param_t PARAMS[P_NPARAMS] = {
    {"Pattern", 0, NPATTERNS - 1, 5}, {"Tempo", 40, 220, 96},  {"Strum", 0, 100, 35},     {"Swing", 0, 100, 0},
    {"Instrument", 0, 15, 3},         {"Tone", 0, 100, 50},    {"Room", 0, 100, 25},      {"Level", 0, 100, 80},
    {"Bass", 0, NBASSPAT - 1, 1},     {"Bass sound", 0, BASS_NSOUNDS - 1, 1}, {"Bass vol", 0, 100, 70}, {"Bass tone", 0, 100, 45},
    {"Bars", 1, OM_MAXBARS, 4},       {"Step", 0, 1, 0},       {"Click", 0, 2, 1},        {"Count-in", 0, 2, 1},
    {"Tuning", 0, NTUNINGS - 1, 0},   {"Octave", -1, 1, 0},    {"Key", 0, 23, 0},         {"MIDI in", 0, 1, 1},
};
const om_param_t *om_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void copy_s(char *b, const char *s)
{
    int k = 0;
    while (s[k] && k < 13) {
        b[k] = s[k];
        k++;
    }
    b[k] = 0;
}

void om_param_text(int i, int v, char *b)
{
    static const char *const STEP[2] = {"1 bar", "1/2 bar"};
    static const char *const CLICK[3] = {"Off", "Rec", "On"};
    static const char *const CIN[3] = {"Off", "1 bar", "2 bars"};
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    switch (i) {
    case P_PATTERN: s = OM_PATTERN_NAME[v % NPATTERNS]; break;
    case P_INSTR: s = om_instr_name(v); break;
    case P_BPAT: s = OM_BASSPAT_NAME[v % NBASSPAT]; break;
    case P_BSOUND: s = om_bass_name(v); break;
    case P_STEP: s = STEP[v & 1]; break;
    case P_METRO: s = CLICK[v % 3]; break;
    case P_COUNTIN: s = CIN[v % 3]; break;
    case P_TUNING: s = OM_TUNING_NAME[v % NTUNINGS]; break;
    case P_MIDI: s = ONOFF[v ? 1 : 0]; break;
    case P_KEY:
        copy_s(b, OM_NOTE_NAME[(v / 2) % 12]);
        scat(b, v & 1 ? " minor" : " major");
        return;
    default: break;
    }
    if (s) {
        copy_s(b, s);
        return;
    }
    if (i == P_SPEED) {                          /* the whole strum's sweep, in ms */
        itoa_s(v * 6 / 5, b);
        scat(b, "ms");
        return;
    }
    if (i == P_SWING) {
        itoa_s(50 + v / 4, b);
        scat(b, "%");
        return;
    }
    if (i == P_OCTAVE && v > 0)
        *b++ = '+';
    itoa_s(v, b);
    if (i == P_TONE || i == P_ROOM || i == P_LEVEL || i == P_BLEVEL || i == P_BTONE)
        scat(b, "%");
}

/* ============================================================ commands === */
enum { C_SET, C_DEFS, C_DEFR, C_CHORD, C_PLUCK, C_PLAY, C_REC, C_PANIC, C_VOICES };
#define QN 256u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void om_set(int p, int v) { post(C_SET, p, v); }
void om_chord_def(int code, const uint8_t pitch[6], int root, int third, int fifth)
{
    int s;
    for (s = 0; s < 6; s++)
        post(C_DEFS, code, s << 8 | pitch[s]);
    post(C_DEFR, code, (root & 15) | (third & 15) << 4 | (fifth & 15) << 8);
}
void om_chord(int code, int down) { post(C_CHORD, code, down); }
void om_pluck(int s, int lane) { post(C_PLUCK, s, lane); }
void om_play(int on, int rec, int cin) { post(C_PLAY, on, (rec ? 1 : 0) | (cin & 3) << 1); }
void om_rec(int on) { post(C_REC, on, 0); }
void om_panic(void) { post(C_PANIC, 0, 0); }
void om_voices(int n) { post(C_VOICES, n, 0); }

/* =============================================================== state === */
volatile uint8_t om_prog[OM_SLOTS], om_maxvoices = 14;
volatile uint8_t om_playing, om_recording, om_counting, om_running, om_step, om_code = 255, om_bass_note, om_nvoices;
volatile float om_step_frac, om_string[6], om_level;
volatile uint32_t om_rec_writes;

static int16_t par[P_NPARAMS];
static uint8_t def_pitch[OM_NCODES][6], def_root[OM_NCODES], def_third[OM_NCODES], def_fifth[OM_NCODES];
static uint8_t def_ok[OM_NCODES];
static uint32_t rng = 0x8769ACE1u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? rng % n : 0;
}

/* ================================================== the strings, struck === */
/* a strum: the strings, low to high or back, one after another over the sweep */
#define NHITS 24
static struct {
    int32_t due;                                  /* samples from now */
    uint8_t string, pitch, chop;
    float strength;
} hits[NHITS];
static int nhits;

static void hit_now(int s, int pitch, float strength, int chop)
{
    if (!pitch)
        return;
    gm_damp_string(s);
    if (chop)
        gm_note_on_timed(s, gm_pitch(par[P_INSTR], pitch), strength, par[P_INSTR], (int)(0.05f * OM_SR));
    else
        gm_note_on(s, gm_pitch(par[P_INSTR], pitch), strength, par[P_INSTR]);
    om_string[s] = strength;
}

static void hit_later(int s, int pitch, float strength, int chop, int32_t delay)
{
    if (!pitch)
        return;
    if (delay <= 0) {
        hit_now(s, pitch, strength, chop);
        return;
    }
    if (nhits < NHITS) {
        hits[nhits].due = delay;
        hits[nhits].string = (uint8_t)s;
        hits[nhits].pitch = (uint8_t)pitch;
        hits[nhits].chop = (uint8_t)chop;
        hits[nhits].strength = strength;
        nhits++;
    }
}

static int code = -1;                             /* the chord the hands play */

/* the sounding strings of the chord, low to high: their indices; returns how many */
static int sounding(int *idx)
{
    int s, n = 0;
    if (code < 0 || !def_ok[code])
        return 0;
    for (s = 0; s < 6; s++)
        if (def_pitch[code][s])
            idx[n++] = s;
    return n;
}

static void strum(int from, int to, int up, float strength, int chop, int block)
{
    int idx[6], n = sounding(idx), i, cnt;
    float sweep = (float)par[P_SPEED] * 0.012f * 0.1f * OM_SR;   /* the whole sweep: 0..120 ms */
    if (!n)
        return;
    if (from < 0)
        from = 0;
    if (to > n)
        to = n;
    cnt = to - from;
    if (cnt <= 0)
        return;
    for (i = 0; i < cnt; i++) {
        int k = up ? to - 1 - i : from + i, s = idx[k];
        float h = 1.0f + ((float)rnd(100) - 50.0f) * 0.001f;           /* a little life */
        int32_t at = block ? 0 : (int32_t)(sweep * (float)i / 6.0f) + (int32_t)rnd(40);
        hit_later(s, def_pitch[code][s], fm_clampf(strength * h * (1.0f - 0.04f * (float)i), 0.05f, 1.0f), chop, at);
    }
}

static void right_hand(char t, int step)
{
    int idx[6], n = sounding(idx);
    float acc = step % 4 == 0 ? 0.92f : step % 2 == 0 ? 0.8f : 0.7f;
    if (!n)
        return;
    switch (t) {
    case 'D': strum(0, n, 0, acc, 0, 0); break;
    case 'U': strum(n > 4 ? n - 4 : 0, n, 1, acc * 0.82f, 0, 0); break;
    case 'd': strum(0, 3, 0, acc, 0, 0); break;
    case 'u': strum(n > 3 ? n - 3 : 0, n, 1, acc * 0.8f, 0, 0); break;
    case 'K': strum(0, n, 0, acc, 0, 1); break;
    case 'C': strum(0, n, 0, acc * 0.9f, 1, 0); break;
    case 'B': hit_now(idx[0], def_pitch[code][idx[0]], acc, 0); break;
    case 'b': {
        int s = idx[n > 1 ? 1 : 0];
        hit_now(s, def_pitch[code][s], acc * 0.95f, 0);
        break;
    }
    default:
        if (t >= '1' && t <= '6') {               /* the n-th sounding string (fewer: from the top) */
            int k = t - '1';
            if (k >= n)
                k = n - 1 - (k - n) % n;
            if (k < 0)
                k = 0;
            hit_now(idx[k], def_pitch[code][idx[k]], acc * 0.85f, 0);
        }
        break;
    }
}

/* ================================================================ bass === */
static int bass_code = -1, next_code = -1;
static float bass_gain = 0.6f;

static int bass_pitch(int c, char t)
{
    int r = def_root[c], n = 28 + ((r - 28) % 12 + 12) % 12;    /* the root in E1..D#2 */
    switch (t) {
    case '3': return n + def_third[c];
    case '5': return n + def_fifth[c];
    case 'O': return n + 12;
    case 'A': {                                   /* into the next chord: a half step under its root */
        if (next_code >= 0 && next_code != c && def_ok[next_code]) {
            int r2 = def_root[next_code], n2 = 28 + ((r2 - 28) % 12 + 12) % 12;
            return n2 - 1 < 28 ? n2 + 11 : n2 - 1;
        }
        return n + def_fifth[c];
    }
    default: return n;
    }
}

static void bass_play(char t)
{
    if (code < 0 || !def_ok[code])
        return;
    if (t == '-') {
        bass_release();
        om_bass_note = 0;
        return;
    }
    if (t == '.' || !t)
        return;
    {
        int n = bass_pitch(code, t);
        bass_note(n, 0, 0);
        om_bass_note = (uint8_t)n;
        bass_code = code;
    }
}

static void bass_step(int step, char rh)
{
    int bp = par[P_BPAT];
    if (!bp)
        return;
    if (bp == 1) {                                /* Follow: the root on the right hand's strong strums */
        if (step % 4 == 0 && (rh == 'D' || rh == 'K' || rh == 'B' || rh == 'C' || rh == 'd' || rh == '1'))
            bass_play('R');
        else if (rh == 'b')
            bass_play('5');
        else if (!par[P_PATTERN] && step == 0)
            bass_play('R');
        return;
    }
    bass_play(BASSPAT[bp][step % OM_BAR]);
}

/* ================================================================ room === */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 442
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] OM_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, lfo_s, lfo_c = 1.0f;
static const float PL_DECAY = 0.62f, PL_DD2 = 0.5f;
static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.000128f * lfo_c, c = lfo_c - 0.000128f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + PL_DECAY * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.55f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * PL_DECAY, PL_DD2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + PL_DECAY * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.55f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * PL_DECAY, PL_DD2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}
static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
}

/* ================================================================ clock === */
static int playing, recording, counting, running;
static int step, loop_len = 64;
static float step_pos, step_len = 1.0f;
static int count_samples, count_beat;
static float click_env, click_ph, click_dt;
static int held, held_code = -1;                  /* chord keys down; the last one pressed */
static float room_w, lvl = 1.0f;

static float this_step_len(void)                  /* swing: the first 8th of each pair longer */
{
    float sw = (float)par[P_SWING] * 0.01f * 0.33f;
    return step_len * ((step & 2) ? 1.0f - sw : 1.0f + sw);
}

/* the chord the progression has at a half bar (looking back to the last change), -1 none */
static int prog_at(int slot)
{
    int k, n = loop_len / 8;
    for (k = 0; k < n; k++) {
        int i = ((slot - k) % n + n) % n;
        if (om_prog[i])
            return om_prog[i] - 1;
    }
    return -1;
}

static void do_step(int s)
{
    char rh;
    if ((par[P_METRO] == 2 || (par[P_METRO] == 1 && recording)) && playing && (s & 3) == 0) {
        click_env = 1.0f;
        click_dt = (s % OM_BAR == 0 ? 2000.0f : 1400.0f) / OM_SR;
    }
    if (playing && s % 8 == 0) {                  /* the progression: its chord here, and the next */
        int c = prog_at(s / 8), k;
        if (c >= 0 && !held)
            code = c;
        next_code = -1;
        for (k = 1; k <= loop_len / 8; k++) {
            int i = (s / 8 + k) % (loop_len / 8);
            if (om_prog[i]) {
                next_code = om_prog[i] - 1;
                break;
            }
        }
        if (held)
            next_code = -1;
    }
    om_code = (uint8_t)(code < 0 ? 255 : code);
    rh = PATTERN[par[P_PATTERN]][s % OM_BAR];
    if (par[P_PATTERN])
        right_hand(rh, s % OM_BAR);
    bass_step(s % OM_BAR, rh);
}

static void clock_start(void)
{
    step = 0;
    step_pos = 0.0f;
    running = 1;
    do_step(0);
}

static void play_start(int rec, int cin)
{
    loop_len = par[P_BARS] * OM_BAR;
    recording = rec;
    if (cin) {
        counting = 1;
        count_beat = cin * 4;
        count_samples = 0;
        playing = 0;
        running = 0;
    } else {
        playing = 1;
        clock_start();
    }
    om_playing = (uint8_t)playing;
    om_recording = (uint8_t)recording;
}

static void play_stop(void)
{
    playing = recording = counting = 0;
    om_playing = om_recording = om_counting = 0;
    if (!held) {
        running = 0;
        bass_release();
        om_bass_note = 0;
        gm_auto_stop();
        nhits = 0;
    }
}

/* recording: a chord at the nearest bar (or half bar), lasting to the next */
static void rec_chord(int c)
{
    int unit = par[P_STEP] ? 8 : 16, pos;
    pos = (int)(((float)step + step_pos / step_len) / (float)unit + 0.5f) * unit % loop_len;
    om_prog[pos / 8] = (uint8_t)(c + 1);
    if (unit == 16 && pos / 8 + 1 < OM_SLOTS)
        om_prog[pos / 8 + 1] = 0;
    om_rec_writes++;
}

static void chord_cmd(int c, int down)
{
    if (c < 0 || c >= OM_NCODES)
        return;
    if (down) {
        held++;
        held_code = c;
        code = c;
        om_code = (uint8_t)c;
        if (recording && playing)
            rec_chord(c);
        if (!running && !counting && (par[P_PATTERN] || par[P_BPAT])) {
            if (par[P_PATTERN] || par[P_BPAT] > 1)
                clock_start();                   /* from the top of a bar */
            else
                bass_play('R');                  /* Follow, no pattern: the root, held */
        } else if (!par[P_PATTERN] && par[P_BPAT] == 1) {
            bass_play('R');
        }
    } else {
        if (held)
            held--;
        if (!held) {
            held_code = -1;
            if (!playing && !counting) {        /* the hands let go: the rhythm stops, the strings ring */
                running = 0;
                bass_release();
                om_bass_note = 0;
            }
        }
    }
}

static void apply(int p, int v)
{
    const om_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_TEMPO: step_len = OM_SR * 60.0f / ((float)v * 4.0f); break;
    case P_TONE: gm_set_character(v); break;
    case P_ROOM: room_w = (float)v * 0.01f * 0.6f; break;
    case P_LEVEL: lvl = (float)v * (1.0f / 80.0f); break;
    case P_BSOUND: bass_sound(v); break;
    case P_BTONE: bass_tone(v, 45); break;
    case P_BLEVEL: {
        float g = (float)v * (1.0f / 70.0f);
        bass_gain = 0.55f * g * g;
        break;
    }
    case P_BARS:
        loop_len = v * OM_BAR;
        if (step >= loop_len)
            step %= loop_len;
        break;
    case P_BPAT:
        if (!v) {
            bass_release();
            om_bass_note = 0;
        }
        break;
    default: break;
    }
}

void om_init(void)
{
    int i;
    gm_reset();
    bass_init();
    plate_init();
    q_r = q_w = 0;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, (int)(int16_t)b);
            break;
        case C_DEFS:
            if (a < OM_NCODES)
                def_pitch[a][(b >> 8) % 6] = (uint8_t)(b & 0x7F);
            break;
        case C_DEFR:
            if (a < OM_NCODES) {
                def_root[a] = (uint8_t)(b & 15);
                def_third[a] = (uint8_t)(b >> 4 & 15);
                def_fifth[a] = (uint8_t)(b >> 8 & 15);
                def_ok[a] = 1;
            }
            break;
        case C_CHORD:
            if (b == 2) {                        /* select only (the UI at start): no sound */
                if (a < OM_NCODES)
                    code = a;
                om_code = (uint8_t)a;
            } else {
                chord_cmd(a, b);
            }
            break;
        case C_PLUCK:                            /* a white key: the string of the chord sounding */
            if (code >= 0 && def_ok[code] && a < 6) {
                static const float LANE[3] = {0.45f, 0.75f, 1.0f};
                hit_now(a, def_pitch[code][a], LANE[b % 3], 0);
            }
            break;
        case C_PLAY:
            if (a)
                play_start(b & 1, (b >> 1) & 3);
            else
                play_stop();
            break;
        case C_REC:
            recording = a && playing;
            om_recording = (uint8_t)recording;
            break;
        case C_VOICES:
            gm_max_voices(a);
            om_maxvoices = (uint8_t)a;
            break;
        case C_PANIC:
            play_stop();
            held = 0;
            running = 0;
            gm_reset();
            gm_set_character(par[P_TONE]);
            bass_release();
            nhits = 0;
            break;
        }
    }
}

/* every 16 samples: the strums' later strings, the count-in, the steps */
static void clocks(void)
{
    int i;
    for (i = 0; i < nhits;) {
        hits[i].due -= 16;
        if (hits[i].due <= 0) {
            hit_now(hits[i].string, hits[i].pitch, hits[i].strength, hits[i].chop);
            hits[i] = hits[--nhits];
        } else {
            i++;
        }
    }
    if (counting) {
        if (count_samples <= 0) {
            if (count_beat == 0) {
                counting = 0;
                om_counting = 0;
                playing = 1;
                om_playing = 1;
                clock_start();
                return;
            }
            click_env = 1.0f;
            click_dt = ((count_beat & 3) == 0 ? 2000.0f : 1400.0f) / OM_SR;
            count_beat--;
            count_samples += (int)(step_len * 4.0f);
        }
        count_samples -= 16;
        om_counting = (uint8_t)(count_beat + 1);
    }
    if (running) {
        float len = this_step_len();
        step_pos += 16.0f;
        if (step_pos >= len) {
            step_pos -= len;
            step = playing ? (step + 1) % loop_len : (step + 1) % OM_BAR;
            do_step(step);
        }
        om_step = (uint8_t)step;
        om_step_frac = step_pos / this_step_len();
    }
    om_running = (uint8_t)running;
    for (i = 0; i < 6; i++)
        om_string[i] *= 0.996f;
}

#define BLK 16
void om_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.2f;
    float pk = om_level * 0.9f, sbuf[32];
    uint32_t j0 = 0, done = 0;
    drain();
    while (done < n) {                            /* 32 samples at a time: the strums' strings land within 1 ms */
        uint32_t k = n - done > 32u ? 32u : n - done, j;
        float *s, bb[32];
        for (j0 = 0; j0 < k; j0 += BLK)
            clocks();                             /* the clocks, every 16 samples of the block */
        s = gm_render((int)k);
        for (j = 0; j < k; j++) {
            sbuf[j] = s ? s[j] : 0.0f;
            bb[j] = 0.0f;
        }
        bass_render(bb, k, bass_gain);
        for (j = 0; j < k; j++) {
            float x = sbuf[j] * lvl, wl = 0.0f, wr = 0.0f, l, r;
            if (room_w > 0.0f)
                reverb(x * 0.5f, &wl, &wr);
            l = x + wl * room_w + bb[j];
            r = x + wr * room_w + bb[j];
            if (click_env > 1e-3f) {
                float c = 0.2f * click_env * fm_sinf(click_ph * FM_TWO_PI);
                click_ph += click_dt;
                if (click_ph >= 1.0f)
                    click_ph -= 1.0f;
                click_env *= 0.9975f;
                l += c;
                r += c;
            }
            l *= gain;
            r *= gain;
            pk = fm_maxf(pk, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
            out[2 * (done + j)] = (int32_t)(fm_tanhf(l) * 8300000.0f);
            out[2 * (done + j) + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
        }
        done += k;
    }
    om_level = fm_minf(pk, 1.0f);
    om_nvoices = (uint8_t)gm_active();
}
