/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   black keys     the key's chords: I ii iii IV V vi vii, then V7, iv, bVII, II7 (minor keys: i ii III iv
 *                  v VI VII, then V, V7, IV, bII). Hold ENV for the 7th
 *   white keys     the strings of the chord, low to high, three times over: soft, normal, accent
 *   PLAY           the progression on / off (from step entry: record it live, after the count-in)
 *   REC            playing: record chord changes; stopped: step entry (a chord key writes the next bar)
 *   LFO            undo (and redo); held 1.5 s: clear the progression. Step entry: hold the chord on
 *   ARP            the right hand's pattern on / off
 *   OCT- / OCT+    the shapes an octave down / up     SELECT tempo (step entry: the cursor)
 *   ALGORITHM      instrument       PRESETS   the key
 *   HOME FX SEL SEQ GLO   pages PLAY, SOUND, BASS, SONG, SETUP; EDIT steps through them
 *   SAVE           saves (it also saves by itself when stopped and quiet) */

enum { V_PLAY, V_SOUND, V_BASS, V_SONG, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Play", "Sound", "Bass", "Song", "Setup"};
enum { K_PRESET = 100, K_LEDS, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_PATTERN, P_TEMPO, P_SPEED, P_SWING},
    {P_INSTR, P_TONE, P_ROOM, P_LEVEL},
    {P_BPAT, P_BSOUND, P_BLEVEL, P_BTONE},
    {P_BARS, K_PRESET, P_STEP, P_METRO},
    {P_TUNING, P_COUNTIN, K_LEDS, P_OCTAVE},
};

#define NKEYS_ 27
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
/* the white keys: E A D G B e, E A D G B e, D G B e (Guitchordi's three lanes) */
static const uint8_t WHITE_STRING[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 2, 3, 4, 5};

/* colours: a warm dark wood, brass for the strings, a sky blue for the chords */
#define K_BG RGB(16, 13, 11)
#define K_PANEL RGB(32, 27, 23)
#define K_HDR RGB(22, 18, 15)
#define K_LINE RGB(66, 56, 48)
#define K_DIM RGB(140, 126, 112)
#define K_LABEL RGB(190, 176, 160)
#define K_TEXT RGB(246, 238, 226)
#define K_ACC RGB(236, 178, 92)
#define K_ACC_T RGB(70, 50, 26)
#define K_SKY RGB(120, 196, 236)
#define K_SKY_T RGB(26, 46, 60)
#define K_REC RGB(255, 92, 84)

static void scpy_(char *b, const char *s)
{
    while ((*b++ = *s++))
        ;
}
static void scat__(char *b, const char *s)
{
    while (*b)
        b++;
    scpy_(b, s);
}
static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* ------------------------------------------------------------- the key --- */
/* the eleven chord keys of a major and a minor key: root (semitones over the key), the triad's and
 * the 7th chord's intervals, the numeral */
typedef struct {
    int8_t root;
    int8_t tri[3], sev[4];
    const char *num;
} slot_t;
static const slot_t SLOTS[2][11] = {
    {{0, {0, 4, 7}, {0, 4, 7, 11}, "I"},       {2, {0, 3, 7}, {0, 3, 7, 10}, "ii"},
     {4, {0, 3, 7}, {0, 3, 7, 10}, "iii"},     {5, {0, 4, 7}, {0, 4, 7, 11}, "IV"},
     {7, {0, 4, 7}, {0, 4, 7, 10}, "V"},       {9, {0, 3, 7}, {0, 3, 7, 10}, "vi"},
     {11, {0, 3, 6}, {0, 3, 6, 10}, "vii"},    {7, {0, 4, 10}, {0, 4, 7, 10}, "V7"},
     {5, {0, 3, 7}, {0, 3, 7, 10}, "iv"},      {10, {0, 4, 7}, {0, 4, 7, 10}, "bVII"},
     {2, {0, 4, 10}, {0, 4, 7, 10}, "II7"}},
    {{0, {0, 3, 7}, {0, 3, 7, 10}, "i"},       {2, {0, 3, 6}, {0, 3, 6, 10}, "ii"},
     {3, {0, 4, 7}, {0, 4, 7, 11}, "III"},     {5, {0, 3, 7}, {0, 3, 7, 10}, "iv"},
     {7, {0, 3, 7}, {0, 3, 7, 10}, "v"},       {8, {0, 4, 7}, {0, 4, 7, 11}, "VI"},
     {10, {0, 4, 7}, {0, 4, 7, 10}, "VII"},    {7, {0, 4, 7}, {0, 4, 7, 10}, "V"},
     {7, {0, 4, 10}, {0, 4, 7, 10}, "V7"},     {5, {0, 4, 7}, {0, 4, 7, 10}, "IV"},
     {1, {0, 4, 7}, {0, 4, 7, 11}, "bII"}},
};
static int key_root(void) { return proj.par[P_KEY] / 2 % 12; }
static int key_minor(void) { return proj.par[P_KEY] & 1; }
static const slot_t *slot_of(int code) { return &SLOTS[key_minor()][code % 11]; }
/* a chord's notes, ascending from its root (in octave 4), and how many */
static int chord_notes(int code, uint8_t *n)
{
    const slot_t *s = slot_of(code);
    int sev = code >= 11, k, cnt = sev ? 4 : 3, r = 60 + (key_root() + s->root) % 12;
    for (k = 0; k < cnt; k++)
        n[k] = (uint8_t)(r + (sev ? s->sev[k] : s->tri[k]));
    if (!sev && s->tri[2] == 10) {               /* the 7th chords among the keys (V7, II7): 4 notes */
        n[0] = (uint8_t)r;
        n[1] = (uint8_t)(r + 4);
        n[2] = (uint8_t)(r + 7);
        n[3] = (uint8_t)(r + 10);
        cnt = 4;
    }
    return cnt;
}
static void chord_name(int code, char *b)       /* "Em7" */
{
    uint8_t n[4];
    char q[16];
    int cnt = chord_notes(code, n);
    om_chord_quality(n, cnt, q);
    scpy_(b, OM_NOTE_NAME[n[0] % 12]);
    scat__(b, q);
}
static void chord_num(int code, char *b)        /* "vi", "V7" */
{
    const slot_t *s = slot_of(code);
    const char *c;
    int has7 = 0;
    for (c = s->num; *c; c++)
        has7 |= *c == '7';
    scpy_(b, s->num);
    if (code >= 11 && !has7) {                   /* the 7th: maj7 where the chord has a major 7th */
        uint8_t n[4];
        int cnt = chord_notes(code, n);
        scat__(b, cnt == 4 && n[3] - n[0] == 11 ? "maj7" : "7");
    }
}

/* ------------------------------------------------------- the shapes --- */
/* A guitar shape for any chord in any tuning: a root on the lowest string that sounds, and on each
 * string above it the note of the chord that the hand reaches in a four-fret window (or an open
 * string), the ones still missing first. The best by: every tone there (root, third, 7th count most),
 * few muted strings, the root low, the hand near the nut, at most four fingers. */
static uint8_t shape_fret[OM_NCODES][6];        /* for the screen: 255 muted */

static void make_shape(int code, uint8_t pitch[6])
{
    const int8_t *open = OM_TUNING[proj.par[P_TUNING] % NTUNINGS];
    uint8_t n[4];
    int cnt = chord_notes(code, n), pcs = 0, root = n[0] % 12, b, bs, s, k, best = 1 << 30;
    int fbest[6] = {-1, -1, -1, -1, -1, -1};
    for (k = 0; k < cnt; k++)
        pcs |= 1 << (n[k] % 12);
    for (b = 0; b <= 9; b++) {
        int lo = b ? b : 1, hi = b ? b + 3 : 3;
        for (bs = 0; bs <= 2; bs++) {
            int f[6], covered = 1 << root, mutes = 0, score, fing[24] = {0}, nf = 0, cand, gaps = 0, top = 0;
            for (s = 0; s < 6; s++)
                f[s] = -1;
            /* the bass: the root */
            cand = -1;
            if ((open[bs] % 12) == root)
                cand = 0;
            for (k = lo; k <= hi && cand < 0; k++)
                if ((open[bs] + k) % 12 == root)
                    cand = k;
            if (cand < 0)
                continue;
            f[bs] = cand;
            for (s = bs + 1; s < 6; s++) {
                int bestf = -1, bestv = -1, opts[5], no = 0, i;
                opts[no++] = 0;
                for (k = lo; k <= hi; k++)
                    opts[no++] = k;
                for (i = 0; i < no; i++) {
                    int pc = (open[s] + opts[i]) % 12, v;
                    if (!(pcs >> pc & 1))
                        continue;
                    v = !(covered >> pc & 1) ? 100 - opts[i] : 50 - opts[i] - (pc == root ? 0 : 5);
                    if (v > bestv) {
                        bestv = v;
                        bestf = opts[i];
                    }
                }
                f[s] = bestf;
                if (bestf < 0) {
                    mutes++;
                    if (s < 5)
                        gaps++;
                } else {
                    covered |= 1 << ((open[s] + bestf) % 12);
                    top = s;
                }
            }
            /* the muted strings above the last sounding one are no gap */
            for (s = 5; s > top; s--)
                if (f[s] < 0)
                    gaps--;
            for (s = 0; s < 6; s++)
                if (f[s] > 0 && !fing[f[s]]++)
                    nf++;
            score = 0;
            for (k = 0; k < cnt; k++)
                if (!(covered >> (n[k] % 12) & 1))
                    score += k == 2 && cnt == 4 ? 12 : 120;     /* a missing fifth of a 7th is fine */
            score += mutes * 7 + gaps * 25 + bs * 6 + b * 3 + (nf > 4 ? 60 : 0);
            if (score < best) {
                best = score;
                for (s = 0; s < 6; s++)
                    fbest[s] = f[s];
            }
        }
    }
    for (s = 0; s < 6; s++) {
        int o = 12 * proj.par[P_OCTAVE];
        pitch[s] = fbest[s] < 0 ? 0 : (uint8_t)(open[s] + fbest[s] + o);
        shape_fret[code][s] = fbest[s] < 0 ? 255 : (uint8_t)fbest[s];
    }
}

/* every chord's shape to the engine: at the start, and when the key, tuning or octave change */
static void send_shapes(void)
{
    int c;
    for (c = 0; c < OM_NCODES; c++) {
        uint8_t p[6], n[4];
        int cnt = chord_notes(c, n);
        make_shape(c, p);
        om_chord_def(c, p, n[0] % 12, n[1] - n[0], cnt > 2 ? n[2] - n[0] : 7);
    }
}

/* ------------------------------------------------------- the presets --- */
typedef struct {
    const char *name;
    uint8_t bars;
    uint8_t chord[12];                           /* a code per bar */
} preset_t;
#define NPRESETS 9
static const preset_t PRESETS[NPRESETS] = {
    {"Pop I-V-vi-IV", 4, {0, 4, 5, 3}},
    {"50s I-vi-IV-V", 4, {0, 5, 3, 4}},
    {"Sad vi-IV-I-V", 4, {5, 3, 0, 4}},
    {"Rock I-IV-V-IV", 4, {0, 3, 4, 3}},
    {"Jazz ii-V-I", 4, {1 + 11, 7, 0 + 11, 0 + 11}},
    {"Canon", 8, {0, 4, 5, 2, 3, 0, 3, 4}},
    {"Blues 12 bar", 12, {0, 0, 0, 0, 3, 3, 0, 0, 7, 3, 0, 7}},
    {"Epic i-VI-III-VII", 4, {0, 5, 2, 6}},
    {"Andalusian", 4, {0, 6, 5, 7}},
};

/* ------------------------------------------------------------- state --- */
static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    int8_t key_code[NKEYS_];                     /* the chord a black key started */
    int8_t last_code;
    uint8_t step_mode;
    int8_t cursor;                               /* step entry: the half bar the next chord goes to */
    uint8_t undo[OM_SLOTS], undo_bars, undo_ok;
    uint32_t lfo_t;
    uint8_t lfo_cleared;
    uint32_t rec_seen;
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }
static void mark_dirty(void) { ui.dirty = 1; }
static int nslots(void) { return proj.par[P_BARS] * 2; }
static int unit(void) { return proj.par[P_STEP] ? 1 : 2; }   /* step entry's step, in half bars */

/* ------------------------------------------------------- the progression --- */
static void snapshot(void)
{
    int i;
    for (i = 0; i < OM_SLOTS; i++)
        ui.undo[i] = om_prog[i];
    ui.undo_bars = (uint8_t)proj.par[P_BARS];
    ui.undo_ok = 1;
}
static void set_bars(int v)
{
    proj.par[P_BARS] = (int16_t)v;
    om_set(P_BARS, v);
}
static void undo_swap(void)
{
    int i, b;
    if (!ui.undo_ok) {
        say("NOTHING TO UNDO", 0);
        return;
    }
    for (i = 0; i < OM_SLOTS; i++) {
        uint8_t t = om_prog[i];
        om_prog[i] = ui.undo[i];
        ui.undo[i] = t;
    }
    b = ui.undo_bars;
    ui.undo_bars = (uint8_t)proj.par[P_BARS];
    set_bars(b);
    say("UNDO", "(LFO again: redo)");
    mark_dirty();
}
static void load_preset(int i)
{
    const preset_t *p = &PRESETS[(unsigned)i % NPRESETS];
    int b;
    snapshot();
    for (b = 0; b < OM_SLOTS; b++)
        om_prog[b] = 0;
    for (b = 0; b < p->bars; b++)
        om_prog[2 * b] = (uint8_t)(p->chord[b] + 1);
    set_bars(p->bars);
    proj.preset = (uint8_t)i;
    say(p->name, 0);
    mark_dirty();
}
/* the chord in effect at a half bar: the last change at or before it */
static int prog_code(int slot)
{
    int k, n = nslots();
    for (k = 0; k < n; k++) {
        int i = ((slot - k) % n + n) % n;
        if (om_prog[i])
            return om_prog[i] - 1;
    }
    return -1;
}

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_PRESET)
        return proj.preset;
    if (k == K_LEDS)
        return proj.leds;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = om_param_info(k)->lo;
        *hi = om_param_info(k)->hi;
        if (k == P_INSTR)
            *hi = om_ninstr() - 1;
        return;
    }
    *lo = 0;
    *hi = k == K_PRESET ? NPRESETS - 1 : k == K_LEDS ? 1 : 0;
}
static const char *knob_name(int k)
{
    if (k < P_NPARAMS)
        return om_param_info(k)->name;
    return k == K_PRESET ? "Load" : k == K_LEDS ? "Lights" : "";
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        om_param_text(k, v, b);
    else if (k == K_PRESET) {                    /* the preset's name, up to its first space */
        const char *n = PRESETS[v % NPRESETS].name;
        int i = 0;
        while (n[i] && n[i] != ' ' && i < 12) {
            b[i] = n[i];
            i++;
        }
        b[i] = 0;
    } else if (k == K_LEDS)
        scpy_(b, v ? "On" : "Off");
    else
        b[0] = 0;
}

static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (k == K_PRESET) {                         /* a turn loads a preset */
        load_preset(v);
        return;
    }
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        om_set(k, v);
        if (k == P_PATTERN && v)
            proj.pat_last = (uint8_t)v;
        if (k == P_KEY || k == P_TUNING || k == P_OCTAVE)
            send_shapes();
        if (k == P_BARS && ui.cursor >= v * 2)
            ui.cursor = 0;
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
    mark_dirty();
}

static void knob_wrap(int k, int32_t e)
{
    int lo, hi, v;
    knob_range(k, &lo, &hi);
    v = knob_get(k) + (e > 0 ? 1 : -1);
    if (v > hi)
        v = lo;
    if (v < lo)
        v = hi;
    knob_set(k, v);
}

static uint8_t accel_off;
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE)
        return;
    if (k == K_PRESET) {
        int v = proj.preset + (int)e;
        load_preset(((v % NPRESETS) + NPRESETS) % NPRESETS);
        return;
    }
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void step_mode_set(int on)
{
    if (on && !ui.step_mode) {
        snapshot();
        ui.cursor = 0;
    }
    ui.step_mode = (uint8_t)on;
    say("STEP ENTRY", on ? "PLAY A CHORD PER BAR" : "OFF");
}

static void button(int b)
{
    char t[16];
    switch (b) {
    case B_HOME: set_view(V_PLAY); break;
    case B_FX: set_view(V_SOUND); break;
    case B_SEL: set_view(V_BASS); break;
    case B_SEQ: set_view(V_SONG); break;
    case B_GLO: set_view(V_SETUP); break;
    case B_EDIT: set_view((ui.view + 1) % NVIEWS); break;
    case B_PLAY:
        if (om_playing || om_counting) {
            om_play(0, 0, 0);
            say("STOP", 0);
        } else if (ui.step_mode) {               /* armed: record live after the count-in */
            ui.step_mode = 0;
            snapshot();
            om_play(1, 1, proj.par[P_COUNTIN]);
            say("RECORDING", proj.par[P_COUNTIN] ? "AFTER THE COUNT-IN" : 0);
        } else {
            om_play(1, 0, 0);
        }
        break;
    case B_REC:
        if (om_playing || om_counting) {
            if (om_recording) {
                om_rec(0);
                say("RECORDING", "OFF");
            } else {
                snapshot();
                om_rec(1);
                say("RECORDING", "PLAY THE CHANGES");
            }
        } else {
            step_mode_set(!ui.step_mode);
        }
        break;
    case B_LFO:
        if (ui.lfo_cleared)
            break;
        if (ui.step_mode) {                      /* the chord before goes on */
            int u = unit(), i;
            for (i = 0; i < u; i++)
                om_prog[(ui.cursor + i) % nslots()] = 0;
            ui.cursor = (int8_t)((ui.cursor + u) % nslots());
            mark_dirty();
        } else if (!om_recording) {
            undo_swap();
        }
        break;
    case B_ARP:
        knob_set(P_PATTERN, proj.par[P_PATTERN] ? 0 : (proj.pat_last ? proj.pat_last : 5));
        om_param_text(P_PATTERN, proj.par[P_PATTERN], t);
        say("PATTERN", t);
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        om_param_text(P_OCTAVE, proj.par[P_OCTAVE], t);
        say("OCTAVE", t);
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else {
            say("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

static void chord_key(int k, int slot, int down)
{
    if (down) {
        int code = slot + ((ui.btn >> B_ENV & 1u) ? 11 : 0);
        if (ui.btn >> B_ENV & 1u)
            ui.btn_used |= 1u << B_ENV;
        ui.key_code[k] = (int8_t)code;
        ui.last_code = (int8_t)code;
        om_chord(code, 1);
        if (ui.step_mode) {
            int u = unit(), n = nslots();
            om_prog[ui.cursor % n] = (uint8_t)(code + 1);
            if (u == 2)
                om_prog[(ui.cursor + 1) % n] = 0;
            ui.cursor = (int8_t)((ui.cursor + u) % n);
            mark_dirty();
        }
    } else {
        om_chord(ui.key_code[k], 0);
    }
}

static void key_event(int k, int down)
{
    int i;
    for (i = 0; i < 11; i++)
        if (BLACK_K[i] == k) {
            chord_key(k, i, down);
            return;
        }
    if (!down)
        return;
    for (i = 0; i < 16; i++)
        if (WHITE_K[i] == k)
            om_pluck(WHITE_STRING[i], i < 6 ? 0 : i < 12 ? 1 : 2);
}

/* MIDI in: notes on channel 1 pick the chord whose root they are (lowest of the key's chords) */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (!proj.par[P_MIDI])
            continue;
        if (st == 0x90u && d2) {
            int i, pc = (int)(d1 % 12u);
            for (i = 0; i < 7; i++)
                if ((key_root() + slot_of(i)->root) % 12 == pc) {
                    om_chord(i, 1);
                    om_chord(i, 0);
                    ui.last_code = (int8_t)i;
                    break;
                }
        }
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i, now = plat_ms();
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_PLAY)
                button((int)i);
            if (i == B_LFO) {
                ui.lfo_t = now;
                ui.lfo_cleared = 0;
            }
        } else if (!(ui.btn_used & m) && i != B_PLAY && i != B_ENV) {
            button((int)i);
        }
    }
    if ((btn >> B_LFO & 1u) && !ui.lfo_cleared && now - ui.lfo_t > 1500u && !om_recording) {
        int k;
        snapshot();
        for (k = 0; k < OM_SLOTS; k++)
            om_prog[k] = 0;
        ui.lfo_cleared = 1;
        ui.btn_used |= 1u << B_LFO;
        say("PROGRESSION CLEARED", "(LFO: undo)");
        mark_dirty();
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS_; i++)
        if (ch >> i & 1u)
            key_event((int)i, (int)(keys >> i & 1u));
    if (btn || keys || ch)
        ui.act_t = now;
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.step_mode)
            ui.cursor = (int8_t)(((ui.cursor + (e > 0 ? 1 : -1)) % nslots() + nslots()) % nslots());
        else
            knob_set(P_TEMPO, proj.par[P_TEMPO] + accel(EN_SELECT, e, 180));
    }
    if ((e = plat_enc(EN_ALGO)) != 0)
        knob_wrap(P_INSTR, e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        knob_wrap(P_KEY, e);
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = now + 1200u;
            ui.act_t = now;
        }
    if (om_rec_writes != ui.rec_seen) {
        ui.rec_seen = om_rec_writes;
        mark_dirty();
    }
}

/* ------------------------------------------------------------- screen --- */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void draw_header(void)
{
    char t[28];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, ui.dirty | (uint32_t)msg << 1 | (uint32_t)proj.par[P_KEY] << 2),
                      (uint32_t)proj.par[P_INSTR] | (uint32_t)proj.par[P_TEMPO] << 8);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_HDR);
    cv_rect(0, HDR_H - 1, 240, 1, K_LINE);
    if (msg) {
        int32_t x = cv_text(10, 5, &FONT_B, ui.msg[0], K_TEXT);
        if (x + 8 + text_w(&FONT_S, ui.msg[1]) < 236)
            cv_text(x + 8, 6, &FONT_S, ui.msg[1], K_ACC);
    } else {
        int32_t x;
        om_param_text(P_KEY, proj.par[P_KEY], t);
        cv_text(10, 5, &FONT_B, t, K_ACC);
        itoa_u((uint32_t)proj.par[P_TEMPO], t);
        x = 232 - text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, K_DIM);
        x -= 8 + text_w(&FONT_S, om_instr_name(proj.par[P_INSTR]));
        cv_text(x, 6, &FONT_S, om_instr_name(proj.par[P_INSTR]), K_LABEL);
        if (ui.dirty)
            cv_round(x - 10, 12, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* the chord: its name big, its numeral, its shape as fret numbers; the state at the right */
static void draw_chord(int32_t y0)
{
    char t[24], q[12];
    int c = om_code != 255 ? om_code : ui.last_code, s;
    int32_t x = 10;
    if (c >= 0 && c < OM_NCODES) {
        chord_name(c, t);
        x = cv_text(10, y0, &FONT_L, t, om_running || om_playing ? K_TEXT : K_LABEL) + 10;
        chord_num(c, t);
        cv_text(x, y0 + 2, &FONT_B, t, K_SKY);
        t[0] = 0;
        for (s = 0; s < 6; s++) {                /* the shape, low string first: x 3 2 0 1 0 */
            uint8_t f = shape_fret[c][s];
            if (f == 255)
                scat__(t, "x");
            else {
                itoa_u(f, q);
                scat__(t, q);
            }
            if (s < 5)
                scat__(t, " ");
        }
        cv_text(x, y0 + 20, &FONT_XS, t, K_DIM);
    } else {
        cv_text(10, y0 + 6, &FONT_B, "Press a black key", K_DIM);
    }
    if (om_counting) {
        scpy_(t, "COUNT ");
        itoa_u(om_counting, q);
        scat__(t, q);
    } else if (om_playing || ui.step_mode) {
        int st = om_playing ? om_step : ui.cursor * 8;
        scpy_(t, om_recording ? "REC " : ui.step_mode ? "STEP " : "");
        itoa_u((uint32_t)(st / OM_BAR + 1), q);
        scat__(t, q);
        scat__(t, ".");
        itoa_u((uint32_t)(st % OM_BAR / 4 + 1), q);
        scat__(t, q);
    } else {
        t[0] = 0;
    }
    cv_text(230 - text_w(&FONT_B, t), y0 + 6, &FONT_B, t, om_recording || ui.step_mode || om_counting ? K_REC : K_LABEL);
}

/* the eleven chord keys, laid out as the black keys are (3 2 3 2 1) */
static void draw_keys(int32_t y0)
{
    static const uint8_t GROUP[11] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4};
    int i, cur = om_code != 255 ? om_code % 11 : -1;
    for (i = 0; i < 11; i++) {
        char t[16], q[16];
        int32_t x = 4 + i * 20 + GROUP[i] * 3, cx = x + 9;
        int on = i == cur, held = 0, k;
        for (k = 0; k < NKEYS_; k++)
            if (ui.keys >> k & 1u && BLACK_K[i] == k)
                held = 1;
        cv_round(x, y0, 18, 40, 4, on ? K_SKY : held ? K_SKY_T : K_PANEL);
        chord_num(i, t);
        text_c(cx, y0 + 2, &FONT_XS, t, on ? K_BG : K_SKY);
        chord_name(i, q);
        {   /* the root, and its quality below it */
            char r[4];
            int n = (q[1] == '#' || q[1] == 'b') ? 2 : 1;
            r[0] = q[0];
            r[1] = n == 2 ? q[1] : 0;
            r[2] = 0;
            text_c(cx, y0 + 14, &FONT_XS, r, on ? K_BG : K_TEXT);
            text_c(cx, y0 + 26, &FONT_XS, q + n, on ? K_BG : K_DIM);
        }
    }
}

/* the progression: a chip per bar (split where it changes at the half), the bar playing lit */
static void draw_song(int32_t y0)
{
    int bars = proj.par[P_BARS], b, cur = om_playing ? om_step / OM_BAR : -1;
    int32_t w = 232 / bars;
    for (b = 0; b < bars; b++) {
        int32_t x = 4 + b * w;
        int h1 = om_prog[2 * b], h2 = om_prog[2 * b + 1], c1 = prog_code(2 * b);
        uint16_t bg = b == cur ? K_ACC : K_ACC_T, fg = b == cur ? K_BG : K_ACC;
        char t[16];
        cv_round(x + 1, y0, w - 2, 30, 4, bg);
        if (ui.step_mode && (ui.cursor == 2 * b || ui.cursor == 2 * b + 1)) {
            int32_t cx = ui.cursor & 1 ? x + w / 2 : x + 1;
            cv_rect(cx, y0 + 32, w / 2 - 1, 3, K_REC);
        }
        if (h2) {                                /* two chords in the bar */
            int c2 = h2 - 1;
            if (c1 >= 0) {
                chord_num(c1, t);
                text_c(x + w / 4, y0 + 8, &FONT_XS, t, fg);
            }
            chord_num(c2, t);
            text_c(x + 3 * w / 4, y0 + 8, &FONT_XS, t, fg);
            cv_rect(x + w / 2, y0 + 4, 1, 22, b == cur ? K_BG : K_LINE);
        } else if (c1 >= 0) {
            chord_num(c1, t);
            if (!h1)                             /* the chord before goes on */
                fg = b == cur ? K_BG : K_DIM;
            if (w >= 50) {
                char nm[16];
                chord_name(c1, nm);
                scat__(t, " ");
                scat__(t, nm);
            }
            text_c(x + w / 2, y0 + 8, &FONT_XS, t, fg);
        }
    }
    if (om_playing) {                            /* the playhead */
        int32_t x = 4 + (int32_t)((float)w * ((float)om_step + om_step_frac) / (float)OM_BAR);
        cv_rect(x, y0 - 3, 2, 36, om_recording ? K_REC : K_TEXT);
    }
}

static void draw_main(void)
{
    uint32_t h = hash(2166136261u, (uint32_t)om_code | (uint32_t)(uint8_t)ui.last_code << 8 | ui.keys << 16);
    int i;
    for (i = 0; i < OM_SLOTS; i++)
        h = hash(h, om_prog[i]);
    h = hash(hash(h, (uint32_t)om_playing | (uint32_t)om_recording << 1 | (uint32_t)om_counting << 2 |
                         (uint32_t)om_running << 6 | (uint32_t)ui.step_mode << 7 | (uint32_t)ui.cursor << 8),
             (uint32_t)(om_playing ? om_step * 4 + (int)(om_step_frac * 4.0f) : 0) |
                 (uint32_t)proj.par[P_BARS] << 12 | (uint32_t)proj.par[P_KEY] << 16 | (uint32_t)proj.par[P_TUNING] << 24);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_chord(2);
    draw_keys(44);
    draw_song(100);
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_HDR);
    cv_text(10, 3, &FONT_XS, VIEW_NAME[ui.view], K_ACC);
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        char t[20];
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 18, 1, KNB_H - 32, K_LINE);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        text_c(cx, 16, &FONT_XS, knob_name(k), hot ? K_TEXT : K_LABEL);
        knob_text(k, t);
        {
            const char *qq = t;
            int lower = 0;
            for (; *qq; qq++)
                lower |= *qq >= 'a' && *qq <= 'z';
            if (lower || text_w(&FONT_M, t) > 56)
                text_c(cx, 32, text_w(&FONT_B, t) > 58 ? &FONT_XS : &FONT_B, t, hot ? K_ACC : K_TEXT);
            else
                text_c(cx, 29, &FONT_M, t, hot ? K_ACC : K_TEXT);
        }
        if (k == K_PRESET)
            continue;
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 54, 44, 5, 2, K_LINE);
        if (lo < 0 && hi > 0) {
            int32_t m = cx - 22 + 44 * (0 - lo) / (hi - lo), e = cx - 22 + bw;
            cv_round(e < m ? e : m, 54, (e < m ? m - e : e - m) + 5, 5, 2, K_ACC);
        } else if (bw) {
            cv_round(cx - 22, 54, bw < 5 ? 5 : bw, 5, 2, K_ACC);
        }
    }
    cv_blit(0, KNB_Y);
}

/* the lights: the page's button, PLAY (red recording), REC, ARP; the chord's black key, and the white
 * keys of the strings that ring */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    static const int8_t VIEW_BTN[NVIEWS] = {B_HOME, B_FX, B_SEL, B_SEQ, B_GLO};
    int blink = (ui.frame / 10u) & 1u;
    b |= 1u << VIEW_BTN[ui.view];
    if (om_counting)
        b |= blink ? 1u << B_PLAY : 0u;
    else if (om_playing)
        b |= (om_step & 3u) < 2u ? 1u << B_PLAY : 0u;
    if (om_recording || ui.step_mode)
        b |= 1u << B_REC;
    if (proj.par[P_PATTERN])
        b |= 1u << B_ARP;
    if (proj.leds) {
        if (om_code != 255)
            k |= 1u << BLACK_K[om_code % 11];
        for (i = 0; i < 16; i++)
            if (om_string[WHITE_STRING[i]] > 0.3f && i < 6)
                k |= 1u << WHITE_K[i];
    }
    k |= ui.keys;
    plat_glow(proj.leds != 0);
    plat_play_red(om_recording);
    plat_leds(b, k);
}

/* the CPU guard: over 85 % of the audio's time, two string voices fewer (down to 6); under 55 % for a
 * second, one back (up to 14) */
static void cpu_guard(void)
{
    static uint32_t calm_since, last_drop;
    static int maxv = 14;
    uint32_t now = plat_ms(), pct = plat_cpu_pct();
    if (pct > 85u && now - last_drop > 250u && maxv > 6) {
        maxv -= 2;
        om_voices(maxv);
        last_drop = calm_since = now;
    } else if (pct < 55u) {
        if (maxv < 14 && now - calm_since > 1000u) {
            om_voices(++maxv);
            calm_since = now;
        }
    } else {
        calm_since = now;
    }
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (!ui.dirty || om_playing || om_counting || om_running || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET ||
        om_nvoices)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.last_code = 0;
    ui.rec_seen = om_rec_writes;
    send_shapes();
    om_chord(0, 2);                              /* the key's I, to strum before any chord is pressed */
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    cpu_guard();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
