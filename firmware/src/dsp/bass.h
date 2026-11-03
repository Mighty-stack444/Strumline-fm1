/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE: the bass voice (BASSLINE's). Call only from the render (the audio interrupt). */
#pragma once
#include <stdint.h>
#define BASS_NSOUNDS 10
extern const char *const BASS_SOUND_NAME[BASS_NSOUNDS];
void bass_init(void);
void bass_sound(int s);                  /* 0 808, 1 Finger, 2 Pick, 3 Slap, 4 Muted, 5 Fretless, 6 Upright, 7 Acid, 8 Sub, 9 Reese */
void bass_tone(int tone, int decay);     /* 0..100 each */
void bass_tune(float factor);
void bass_note(int note, int legato, int accent);   /* legato: a slide from the note sounding */
void bass_release(void);
int bass_playing(void);                  /* the note sounding (gate on), 0 none */
void bass_render(float *out, uint32_t n, float gain);   /* adds n samples */
