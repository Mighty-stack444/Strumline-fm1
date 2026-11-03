#ifndef GUITOMNICHORD_ENGINE_H
#define GUITOMNICHORD_ENGINE_H
#define GM_RATE 44100
#define GM_VOICES 14
#define GM_BLOCK 128
int gm_tone_count(void);
const char *gm_tone_name(int tone);
int gm_part(int tone, int string_id);
int gm_parts(int tone);
int gm_pitch(int tone, int pitch);
void gm_reset(void);
void gm_set_character(int percent);
int gm_note_on(int string_id, int pitch, float intensity, int tone);
// Zero frames keeps the natural envelope while retaining automatic-note stop tracking.
int gm_note_on_timed(int string_id,int pitch,float intensity,int tone,int frames);
int gm_note_on_gated(int part,int pitch,float intensity,int tone,int frames,int ring);
void gm_auto_stop(void);
void gm_damp_string(int string_id);
void gm_max_voices(int n);
float *gm_render(int frames);
int gm_active(void);
int gm_steals(void);
int gm_state_bytes(void);
int gm_clips(void);
#endif
