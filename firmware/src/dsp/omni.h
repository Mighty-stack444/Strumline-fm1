/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE: the instrument. A six-string chord instrument for the FM-1, after Guitchordi, with a bass:
 *
 *   STRINGS  Guitchordi's sixteen instruments (gtr/: Clipper's PHYS string model, MIT, through
 *            Guitchordi's adapter, GPL): six strings, each playing one note of the chord's shape.
 *   CHORDS   up to 22: the eleven chord keys' chords and their 7ths. The UI works out each one's
 *            guitar shape for the key and the tuning and sends it here (om_chord_def).
 *   PATTERN  the right hand: strums, chops, picking and arpeggios, a bar of sixteenths each.
 *   BASS     BASSLINE's bass voice (bass.c) under the chord, by its own pattern.
 *   SONG     a progression of 1-12 bars, a chord per half bar, that the loop plays (and records).
 *
 * Threads: om_render() runs in the audio interrupt; it alone calls the string and bass engines.
 * Everything else posts commands (om_*), drained at the start of each block. om_prog[] is shared: the
 * render writes it only while recording, the UI only when it isn't. Float only, no libm. */
#pragma once
#include <stdint.h>

#define OM_SR 44100.0f
#define OM_BAR 16                      /* steps (sixteenths) in a bar */
#define OM_MAXBARS 12
#define OM_SLOTS (OM_MAXBARS * 2)      /* the progression: a chord per half bar */
#define OM_NCODES 22                   /* chord codes: key 0..10, +11 for its 7th */

/* ---- music theory (shared with the UI) */
extern const char *const OM_NOTE_NAME[12];
void om_chord_quality(const uint8_t *notes, int n, char *buf);   /* "m", "maj7", "m7b5"... */
void om_roman(int deg, int minor, char *buf);

/* ---- names of things */
#define NPATTERNS 14
#define NBASSPAT 8
#define NTUNINGS 6
extern const char *const OM_PATTERN_NAME[NPATTERNS];
extern const char *const OM_BASSPAT_NAME[NBASSPAT];
extern const char *const OM_TUNING_NAME[NTUNINGS];
extern const int8_t OM_TUNING[NTUNINGS][6];     /* open strings, MIDI, low to high */
int om_ninstr(void);
const char *om_instr_name(int i);
const char *om_bass_name(int i);

/* ---- parameters: value ranges in om_param_info() */
enum {
    P_PATTERN, P_TEMPO, P_SPEED, P_SWING,              /* PLAY: the right hand */
    P_INSTR, P_TONE, P_ROOM, P_LEVEL,                  /* SOUND */
    P_BPAT, P_BSOUND, P_BLEVEL, P_BTONE,               /* BASS */
    P_BARS, P_STEP, P_METRO, P_COUNTIN,                /* SONG (+ the preset, a UI action) */
    P_TUNING, P_OCTAVE, P_KEY, P_MIDI,                 /* SETUP; the key (PRESETS); MIDI in */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} om_param_t;
const om_param_t *om_param_info(int i);
void om_param_text(int i, int v, char *buf);   /* the value as the screen shows it (14 bytes) */

/* ---- commands (main loop -> render) */
void om_init(void);
void om_set(int param, int value);
/* a chord's shape: six pitches (0: a muted string), its root (pitch class), third and fifth (semitones) */
void om_chord_def(int code, const uint8_t pitch[6], int root, int third, int fifth);
void om_chord(int code, int down);             /* a chord key down (1) / up (0); 2: select it, silently */
void om_pluck(int string, int lane);           /* a white key: string 0..5 of the chord, lane 0 soft 1 normal 2 accent */
void om_play(int on, int rec, int countin_bars);
void om_rec(int on);
void om_panic(void);
void om_voices(int n);                          /* the most string voices at once, 6..14: the CPU guard */

/* the progression: per half bar, 0 (the chord before goes on) or a chord code + 1 */
extern volatile uint8_t om_prog[OM_SLOTS];

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void om_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* ---- state for the screen (UI) */
extern volatile uint8_t om_playing, om_recording, om_counting, om_running;
extern volatile uint8_t om_step;               /* the step playing in the loop (or the pattern's bar) */
extern volatile float om_step_frac;
extern volatile uint8_t om_code;               /* the chord sounding: a code, 255 none */
extern volatile uint8_t om_bass_note;          /* 0 none */
extern volatile float om_string[6];            /* each string's last hit, decaying 1 -> 0 */
extern volatile float om_level;
extern volatile uint32_t om_rec_writes;
extern volatile uint8_t om_nvoices, om_maxvoices;
