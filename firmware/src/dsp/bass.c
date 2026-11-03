/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE: the bass. BASSLINE's monophonic bass voice (github.com/Mighty-stack444/Bassline-fm1, 0.2),
 * unchanged but for this wrapper: ten sounds (808, Finger, Pick, Slap, Muted, Fretless, Upright, Acid,
 * Sub, Reese), played by the arranger in omni.c under the guitar. See bass.h. */
#include <stdint.h>
#include "bass.h"
#include "fastmath.h"

#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif
#define OM_SR 44100.0f

/* BASSLINE's parameters this voice reads, at fixed values but for Tone and Decay */
enum { P_TONE, P_DECAY, P_DIVE, P_PUNCH, P_DIVETIME, P_TAIL, P_WOBBLE, P_WRATE, P_TEMPO, P_NPARAMS };
enum { S_808, S_FINGER, S_PICK, S_SLAP, S_MUTED, S_FRETLESS, S_UPRIGHT, S_ACID, S_SUB, S_REESE };
static int16_t par[P_NPARAMS] = {50, 45, 0, 4, 30, 0, 0, 2, 120};
static float tunefac = 1.0f, bend = 0.0f, kbend = 0.0f, wob_ph;
static uint8_t om_note_on;
static float om_env;
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}

#define TBL 1024
static float sintab[TBL + 1];
static inline float tsin(float ph)
{
    float x = ph * (float)TBL;
    int i = (int)x;
    float f = x - (float)i;
    return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - (float)(int)p : p; }
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static uint32_t nrng = 0x2468ACE1u;
static inline float noise(void)
{
    nrng = nrng * 1664525u + 1013904223u;
    return (float)(int32_t)nrng * (1.0f / 2147483648.0f);
}
static float t60_coef(float t)                 /* per-sample decay that falls 60 dB in t seconds */
{
    return fm_exp2f(-9.9658f / (fm_maxf(t, 0.001f) * OM_SR));
}
static float tc_coef(float t) { return 1.0f - fm_expf(-1.0f / (fm_maxf(t, 0.0005f) * OM_SR)); }

/* ================================================================ voice === */
#define KS_N 4096
#define KS_M (KS_N - 1)
static float ks[KS_N] OM_POOL;

enum { E_OFF, E_ATT, E_HOLD, E_REL };
static struct {
    uint8_t st, note;                          /* the note playing (or last played) */
    float pitch, target, gk;                   /* pitch in semitones; glide toward target */
    float env, att_k;                          /* amplitude */
    float punch, dive;                         /* 808 punch, dive (or scoop) envelopes, 1 -> 0 */
    float tail;                                /* the tail's drop after the note is let go, 0 -> 1 */
    float ph, ph2, ph3;
    float f1, f2, f3, f4, fenv, acc;           /* acid's filter and its envelope */
    float lp, ks_w_f;                          /* the string's loop lowpass */
    int ks_w;
    float pop, prev, mw;                       /* slap's pop; fretless's "mwah" */
    float sv1l, sv1b, sv2l, sv2b, svml, svmb;  /* upright's body, fretless's formant */
    float dt, len;                             /* per tick: phase increment, string length */
    float k_hold, k_rel, k_lp;                 /* per tick: the string's decay and brightness */
} vo;
static int sound;
static float k_punch, k_dive, k_fenv, k_pop, k_mw;

static float note_hz(float p) { return 440.0f * tunefac * fm_exp2f((p - 69.0f) * (1.0f / 12.0f)); }

static void voice_coefs(void)
{
    float dt = (float)par[P_DIVETIME] * 0.01f;
    k_punch = tc_coef(0.012f);
    k_dive = tc_coef((0.02f + 1.5f * dt * dt) * (sound == S_808 ? 0.5f : 0.3f));   /* the time to near 0 */
    k_fenv = tc_coef(0.06f + 1.0f * (float)par[P_DECAY] * (float)par[P_DECAY] * 1e-4f);
    k_pop = tc_coef(0.012f);
    k_mw = tc_coef(0.22f);
}

/* the string's excitation: a burst one period long, shaped per sound, added to what the delay line
 * will play next. The old note is damped (a hand stopping the string). */
static void pluck(float amp)
{
    int L = (int)vo.len, i;
    float e = 0.0f, e2 = 0.0f, ec, tone = (float)par[P_TONE] * 0.01f;
    if (L < 2)
        L = 2;
    if (L > KS_N - 2)
        L = KS_N - 2;
    switch (sound) {
    case S_PICK: ec = 0.45f + 0.5f * tone; break;
    case S_SLAP: ec = 0.75f + 0.25f * tone; break;
    case S_MUTED: ec = 0.15f + 0.2f * tone; break;
    case S_UPRIGHT: ec = 0.08f + 0.15f * tone; break;
    case S_FRETLESS: ec = 0.15f + 0.3f * tone; break;
    default: ec = 0.18f + 0.35f * tone; break;   /* finger */
    }
    for (i = 0; i < L; i++) {
        int j = (vo.ks_w - L + i) & KS_M;
        float x = noise();
        float w = (float)i / (float)L, sh;
        e += ec * (x - e);                          /* the burst's brightness */
        e2 += ec * (e - e2);
        sh = sound == S_PICK || sound == S_SLAP ? e : e2;
        sh *= 1.0f - w * 0.6f;                      /* a little weight at the start */
        if (sound == S_FINGER || sound == S_UPRIGHT || sound == S_MUTED)
            sh += 0.6f * fm_sinf(FM_PI * w);        /* the finger's push: a soft half sine */
        ks[j] = ks[j] * 0.25f + amp * sh;
    }
    vo.lp = 0.0f;
}

static void tick_coefs(void);

/* the note starts: retrig restarts the sound, else it slides there (legato) */
static void voice_go(int note, int retrig, int glide, float gtime, int acc)
{
    float p = (float)note;
    if (vo.st != E_OFF && vo.st != E_REL && vo.note != note)
        midi(0x80, vo.note, 0);
    if (vo.st == E_OFF || !glide)
        vo.pitch = p;                               /* no glide: straight there */
    vo.target = p;
    vo.gk = glide ? 1.0f - fm_expf(-16.0f * 3.0f / (fm_maxf(gtime, 0.003f) * OM_SR)) : 1.0f;
    vo.note = (uint8_t)note;
    vo.acc = acc ? 1.0f : 0.0f;
    midi(0x90, (uint32_t)note, acc ? 120 : 96);
    if (retrig || vo.st == E_OFF || vo.st == E_REL) {
        vo.punch = 1.0f;
        vo.tail = 0.0f;
        vo.dive = 1.0f;
        vo.fenv = 1.0f;
        vo.pop = 1.0f;
        vo.mw = 1.0f;
        if (sound == S_808 || sound == S_SUB || sound == S_REESE || sound == S_ACID) {
            if (sound == S_808)
                vo.ph = 0.0f;                       /* the 808 restarts its cycle: the punch */
            vo.st = E_ATT;
        } else {
            vo.st = E_HOLD;
            vo.env = 1.0f;
            tick_coefs();
            pluck(acc ? 1.0f : 0.8f);
        }
    }
    om_note_on = (uint8_t)note;
}

static void voice_off(void)
{
    if (vo.st == E_OFF || vo.st == E_REL)
        return;
    vo.st = E_REL;
    midi(0x80, vo.note, 0);
    om_note_on = 0;
}

/* every 16 samples: the pitch glides, the string's length and decay follow */
static void tick_coefs(void)
{
    float decay = (float)par[P_DECAY] * 0.01f, tone = (float)par[P_TONE] * 0.01f, p, hz, t_hold, t_rel;
    vo.pitch += (vo.target - vo.pitch) * vo.gk;
    p = vo.pitch + bend + kbend - (float)par[P_TAIL] * vo.tail;
    if (par[P_WOBBLE]) {                            /* the wobble: a vibrato in time with the tempo */
        static const float BEATS[6] = {2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.125f};
        float w = (float)par[P_WOBBLE] * 0.01f;
        p += 2.0f * w * w * tsin(wob_ph);
        wob_ph = wrap1(wob_ph + 16.0f * (float)par[P_TEMPO] / (60.0f * OM_SR * BEATS[par[P_WRATE] % 6]));
    }
    if (sound == S_808)
        p += (float)par[P_PUNCH] * vo.punch + (float)par[P_DIVE] * vo.dive;
    else if (par[P_DIVE])
        p -= (float)par[P_DIVE] * 0.5f * vo.dive;   /* a scoop up into the note */
    hz = note_hz(p);
    vo.dt = hz / OM_SR;
    vo.len = fm_clampf(OM_SR / hz, 2.0f, (float)(KS_N - 4));
    switch (sound) {
    case S_MUTED: t_hold = 0.25f + 0.6f * decay; t_rel = 0.03f; break;
    case S_UPRIGHT: t_hold = 1.0f + 6.0f * decay * decay; t_rel = 0.1f; break;
    case S_FRETLESS: t_hold = 2.0f + 12.0f * decay * decay; t_rel = 0.12f; break;
    case S_SLAP: t_hold = 1.2f + 8.0f * decay * decay; t_rel = 0.06f; break;
    default: t_hold = 1.5f + 12.0f * decay * decay; t_rel = 0.06f; break;
    }
    /* one trip round the loop loses what this many samples of decay would */
    vo.k_hold = fm_exp2f(-9.9658f * vo.len / (t_hold * OM_SR));
    if (par[P_TAIL])                                /* a tail: the note rings on as it drops */
        t_rel = fm_maxf(t_rel, 0.35f);
    vo.k_rel = fm_exp2f(-9.9658f * vo.len / (t_rel * OM_SR));
    switch (sound) {                                /* the loop's brightness */
    case S_MUTED: vo.k_lp = 0.25f + 0.25f * tone; break;
    case S_UPRIGHT: vo.k_lp = 0.35f + 0.3f * tone; break;
    case S_FRETLESS: vo.k_lp = 0.6f + 0.3f * tone; break;
    case S_SLAP: vo.k_lp = 0.75f + 0.22f * tone; break;
    case S_PICK: vo.k_lp = 0.6f + 0.35f * tone; break;
    default: vo.k_lp = 0.4f + 0.4f * tone; break;
    }
}

static inline float ks_read(float delay)
{
    float r = (float)vo.ks_w - delay;
    int i;
    float f;
    while (r < 0.0f)
        r += (float)KS_N;
    i = (int)r;
    f = r - (float)i;
    return ks[i & KS_M] + (ks[(i + 1) & KS_M] - ks[i & KS_M]) * f;
}

static inline float svf_bp(float x, float *l, float *b, float f, float q)
{
    float h = x - *l - q * *b;
    *b = fm_flush(*b + f * h);
    *l = fm_flush(*l + f * *b);
    return *b;
}


#define BLK 256
static float mono[BLK];

static void render_voice(uint32_t n)
{
    uint32_t j;
    float decay = (float)par[P_DECAY] * 0.01f, tone = (float)par[P_TONE] * 0.01f;
    const float rel_osc = t60_coef(par[P_TAIL] ? 0.45f : sound == S_808 ? 0.09f : 0.06f);
    float t808 = 0.15f + 4.0f * decay * decay, k808 = t60_coef(t808);
    float sus = sound == S_ACID ? 1.0f : 0.25f + 0.75f * decay, k_sus = tc_coef(0.4f);
    for (j = 0; j < n; j++)
        mono[j] = 0.0f;
    if (vo.st == E_OFF) {
        om_env = 0.0f;
        return;
    }
    for (j = 0; j < n; j++) {
        float o = 0.0f;
        if (!(j & 15u)) {
            tick_coefs();
            vo.punch -= vo.punch * k_punch * 16.0f;
            vo.dive -= vo.dive * fm_minf(k_dive * 16.0f, 1.0f);
            if (vo.st == E_REL)                     /* the tail drops over about 0.15 s */
                vo.tail += (1.0f - vo.tail) * 0.004f;
            vo.fenv -= vo.fenv * fm_minf(k_fenv * 16.0f, 1.0f);
            vo.pop -= vo.pop * fm_minf(k_pop * 16.0f, 1.0f);
            vo.mw -= vo.mw * fm_minf(k_mw * 16.0f, 1.0f);
        }
        if (sound >= S_FINGER && sound <= S_UPRIGHT) {   /* ---- the strings */
            float s = ks_read(vo.len), y;
            vo.lp += vo.k_lp * (s - vo.lp);
            y = vo.lp * (vo.st == E_REL ? vo.k_rel : vo.k_hold);
            ks[vo.ks_w] = fm_flush(y);
            vo.ks_w = (vo.ks_w + 1) & KS_M;
            o = s;
            switch (sound) {
            case S_FINGER: case S_PICK: case S_SLAP: case S_MUTED:   /* the pickup near the bridge */
                o = s - 0.55f * ks_read(vo.len * 0.13f + 1.0f);
                if (sound == S_SLAP) {
                    o += vo.pop * 3.0f * (s - vo.prev); /* the thumb's snap: the string's edge */
                    vo.prev = s;
                }
                o *= 0.9f;
                break;
            case S_FRETLESS: {                      /* a formant that sweeps down: the "mwah" */
                float fc = fm_minf(vo.dt * (2.5f + 7.0f * vo.mw), 0.12f) * FM_PI * 2.0f;
                o = 0.55f * s + 1.1f * svf_bp(s, &vo.svml, &vo.svmb, fc, 0.6f);
                break;
            }
            case S_UPRIGHT: {                       /* the wooden body: two resonances */
                float b1 = svf_bp(s, &vo.sv1l, &vo.sv1b, 0.0142f, 0.35f);   /* ~100 Hz */
                float b2 = svf_bp(s, &vo.sv2l, &vo.sv2b, 0.0313f, 0.3f);    /* ~220 Hz */
                o = 0.7f * s + 0.35f * b1 + 0.3f * b2;
                break;
            }
            default: break;
            }
            if (vo.st == E_REL && fm_fabsf(s) < 1e-4f && fm_fabsf(vo.lp) < 1e-4f) {
                int k;
                vo.st = E_OFF;                      /* rung out: the line is cleared */
                for (k = 0; k < KS_N; k++)
                    ks[k] = 0.0f;
                break;
            }
            o *= sound == S_MUTED ? 3.0f : 2.2f;       /* the strings up to the others' level */
            om_env = fm_fabsf(vo.lp);
        } else {                                    /* ---- the oscillators: their envelope */
            if (vo.st == E_ATT) {
                vo.env += 1.0f / (0.002f * OM_SR);
                if (vo.env >= 1.0f) {
                    vo.env = 1.0f;
                    vo.st = E_HOLD;
                }
            } else if (vo.st == E_HOLD) {
                if (sound == S_808)
                    vo.env *= k808;                 /* the 808 rings down while held */
                else
                    vo.env += (sus - vo.env) * k_sus;
            } else {
                vo.env *= rel_osc;
                if (vo.env < 1e-4f) {
                    vo.st = E_OFF;
                    break;
                }
            }
            switch (sound) {
            case S_808: {
                float x = tsin(vo.ph), g = 1.0f + tone * 3.0f;
                vo.ph = wrap1(vo.ph + vo.dt);
                o = fm_tanhf(x * g) / fm_tanhf(g) * 0.95f;
                break;
            }
            case S_ACID: {
                float saw = 2.0f * vo.ph - 1.0f - blep(vo.ph, vo.dt), fc, g, k = 3.4f, x;
                vo.ph = wrap1(vo.ph + vo.dt);
                if (!(j & 15u)) {                   /* the cutoff: Tone, plus the envelope (accent: more) */
                    fc = 70.0f * fm_exp2f(tone * 6.5f + vo.fenv * (3.0f + 1.5f * vo.acc));
                    vo.ks_w_f = fm_minf(1.0f - fm_expf(-FM_TWO_PI * fc / OM_SR), 0.85f);
                }
                g = vo.ks_w_f;
                x = saw - k * fm_tanhf(vo.f4);
                vo.f1 += g * (x - vo.f1);
                vo.f2 += g * (vo.f1 - vo.f2);
                vo.f3 += g * (vo.f2 - vo.f3);
                vo.f4 = fm_flush(vo.f4 + g * (vo.f3 - vo.f4));
                o = vo.f4 * 1.9f * (1.0f + 0.3f * vo.acc);
                break;
            }
            case S_SUB: {
                float p1 = vo.ph, p2 = p1 + p1, p3 = p2 + p1;
                p2 -= (float)(int)p2;
                p3 -= (float)(int)p3;
                o = tsin(p1) + tone * (0.45f * tsin(p2) + 0.25f * tsin(p3));
                vo.ph = wrap1(p1 + vo.dt);
                o = fm_tanhf(o * 1.2f) * 0.9f;
                break;
            }
            default: {                              /* S_REESE */
                float dc = fm_exp2f((6.0f + 22.0f * tone) * (1.0f / 1200.0f)), d1 = vo.dt * dc, d2 = vo.dt / dc,
                      x, g;
                x = (2.0f * vo.ph2 - 1.0f - blep(vo.ph2, d1)) + (2.0f * vo.ph3 - 1.0f - blep(vo.ph3, d2));
                vo.ph2 = wrap1(vo.ph2 + d1);
                vo.ph3 = wrap1(vo.ph3 + d2);
                x = 0.4f * x + 0.5f * tsin(vo.ph);
                vo.ph = wrap1(vo.ph + vo.dt);
                g = fm_minf(1.0f - fm_expf(-FM_TWO_PI * (180.0f * fm_exp2f(tone * 4.5f)) / OM_SR), 0.9f);
                vo.f1 += g * (x - vo.f1);
                vo.f2 = fm_flush(vo.f2 + g * (vo.f1 - vo.f2));
                o = vo.f2 * 0.9f;
                break;
            }
            }
            o *= vo.env;
            om_env = vo.env;
        }
        mono[j] = o;
    }
}


/* ============================================================ the API === */
void bass_init(void)
{
    int i;
    for (i = 0; i <= TBL; i++)
        sintab[i] = fm_sinf((float)i * (FM_TWO_PI / (float)TBL));
    for (i = 0; i < KS_N; i++)
        ks[i] = 0.0f;
    vo.st = E_OFF;
    sound = 1;
    voice_coefs();
}

void bass_sound(int s)
{
    int k;
    if (s < 0 || s >= BASS_NSOUNDS || s == sound)
        return;
    vo.st = E_OFF;
    om_note_on = 0;
    sound = s;
    for (k = 0; k < KS_N; k++)
        ks[k] = 0.0f;
    vo.f1 = vo.f2 = vo.f3 = vo.f4 = 0.0f;
    voice_coefs();
}

void bass_tone(int tone, int decay)
{
    par[P_TONE] = (int16_t)(tone < 0 ? 0 : tone > 100 ? 100 : tone);
    par[P_DECAY] = (int16_t)(decay < 0 ? 0 : decay > 100 ? 100 : decay);
    voice_coefs();
}

void bass_tune(float factor) { tunefac = factor; }

void bass_note(int note, int legato, int accent)
{
    int sounding = vo.st == E_ATT || vo.st == E_HOLD;
    if (legato && sounding)
        voice_go(note, 0, 1, 0.06f, accent);       /* a slide into it */
    else
        voice_go(note, 1, 0, 0.0f, accent);
}

void bass_release(void) { voice_off(); }
int bass_playing(void) { return om_note_on; }

void bass_render(float *out, uint32_t n, float gain)
{
    uint32_t j;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        render_voice(k);
        for (j = 0; j < k; j++)
            out[j] += mono[j] * gain;
        out += k;
        n -= k;
    }
}

const char *const BASS_SOUND_NAME[BASS_NSOUNDS] = {"808", "Finger", "Pick", "Slap", "Muted",
                                                   "Fretless", "Upright", "Acid", "Sub", "Reese"};
