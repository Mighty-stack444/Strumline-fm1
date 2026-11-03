// Independent adapter around Clipper's MIT-licensed phys_dsp.c. See third_party/clipper-phys/README.md.
// From Guitchordi (github.com/stoogs/guitchordi, GPL-3.0-only, commit 9b16934), for Strumline: the
// voices in the FM-1's .pool RAM, and gm_damp_string() (a string struck again stops ringing first).
#include <stdint.h>
#include <string.h>
#include "engine.h"
#include "pitch-table.h"
#include "instruments.h"
#include "bow-wave.h"
#include "phys_dsp.c"

typedef struct {
    px_string_t state;
    px_string_blk_t coefficients;
    uint32_t age, remaining, duration, release_frames;
    float gain;
    int used, string_id, fade, pending_pitch, pending_tone, pending_string;
    int attack, bowed; float drive;
    uint32_t phase, phase_step;
    float pending_intensity; int pending_character, pending_frames, pending_auto, automatic, gated;
} gm_voice;
#ifdef OM_HOST
static gm_voice voices[GM_VOICES];
#else
static gm_voice voices[GM_VOICES] __attribute__((section(".pool")));
#endif
static int max_voices = GM_VOICES;                /* the CPU guard's limit (Strumline) */
void gm_max_voices(int n) { max_voices = n < 4 ? 4 : n > GM_VOICES ? GM_VOICES : n; }
static float output[GM_BLOCK];
static uint32_t serial;
static int steals, clips, character = 50;
void gm_set_character(int percent) { character=percent<0?0:percent>100?100:percent; }
#define FADE_SAMPLES 132

void gm_reset(void) { memset(voices, 0, sizeof voices); serial = 0; steals = clips = 0; character=50; }
int gm_active(void) { int n = 0; for (int i = 0; i < GM_VOICES; i++) n += !!voices[i].used; return n; }
int gm_steals(void) { return steals; }
int gm_state_bytes(void) { return (int)(sizeof voices + sizeof output); }
int gm_clips(void) { return clips; }
int gm_tone_count(void) { return GM_TONES; }
const char *gm_tone_name(int tone) { return tone>=0 && tone<GM_TONES ? gm_patches[tone].name : ""; }
int gm_part(int tone, int string_id) { return gm_patches[tone].map[string_id]; }
int gm_parts(int tone) { return gm_patches[tone].parts; }
int gm_pitch(int tone, int pitch) {
    if(pitch<0) return -1;
    pitch += gm_patches[tone].shift;
    while(pitch<24) pitch+=12;
    while(pitch>96) pitch-=12;
    return pitch;
}
static void begin(gm_voice *v, int string_id, int pitch, float intensity, int tone, uint32_t age, int character,int frames,int automatic) {
    memset(v, 0, sizeof *v);
    v->used = 1; v->string_id = string_id; v->age = age;
    v->state.rng = (0x9e3779b9u ^ (age * 2654435761u) ^ ((uint32_t)pitch << 16)) | 1u;
    v->state.src = 1u << 30; v->state.trig = 1;
    const uint32_t f0 = gm_phase[pitch - 24];
    const gm_patch *p=&gm_patches[tone];
    float soft=character<50 ? .22f+.78f*character/50.0f : 1.0f;
    int clean=tone==11 || tone==12;
    float wild=!clean && character>50 ? (character-50)/50.0f : 0.0f;
    int brightness=p->brightness+(int)(wild*(60000-p->brightness));
    brightness=(int)(brightness*(.5f+.5f*soft));
    int brit = (int)(brightness * (0.35f + 0.65f * intensity));
    px_string_block(&v->coefficients, &v->state, f0, p->structure, brit,
                    p->damping, (int)(intensity * 40000), p->bow, p->position);
    v->gain = intensity * p->gain * soft / (1.0f+wild*1.8f);
    if(clean && character>50) v->gain*=1.0f+(character-50)*.003f;
    v->drive=p->drive+wild*wild*32.0f; v->attack=p->attack;
    v->bowed=tone==12; v->phase_step=f0;
    if(v->bowed) v->gain*=.8f;
    v->duration = v->remaining = GM_RATE * p->ms / 1000;
    v->automatic=automatic; v->gated=frames>0;
    if(frames>0 && (uint32_t)frames<v->duration) { v->duration=v->remaining=(uint32_t)frames; if(v->attack>frames/3) v->attack=frames/3; }
    v->release_frames=v->automatic&&v->gated?(v->duration/3<1323?v->duration/3:1323):v->bowed?13230:1323;
    if(frames<0) {
        uint32_t hold=(uint32_t)-frames,tail=tone==11?30870u:tone==12?22050u:17640u;
        if(hold+tail<v->duration)v->duration=v->remaining=hold+tail;
        v->release_frames=tail<v->duration?tail:v->duration;
        if((uint32_t)v->attack>hold/3)v->attack=(int)(hold/3);
    }

}
static int note_on(int string_id, int pitch, float intensity, int tone,int frames,int automatic) {
    if (string_id < 0 || string_id > 5 || pitch < 24 || pitch > 96 || tone < 0 || tone >= GM_TONES ||
        !(intensity > 0 && intensity <= 1)) return 0;
    gm_voice *v = 0;
    for (int i = 0; i < max_voices; i++) if (!voices[i].used) { v = &voices[i]; break; }
    if (!v) {
        v = &voices[0];
        for (int i = 1; i < max_voices; i++) if (voices[i].age < v->age) v = &voices[i];
        v->pending_pitch = pitch; v->pending_tone = tone; v->pending_string = string_id;
        v->pending_frames=frames; v->pending_auto=automatic; v->pending_intensity = intensity; v->pending_character=character; v->fade = v->remaining < FADE_SAMPLES ? (int)v->remaining : FADE_SAMPLES; v->age = ++serial;
        steals++; return 1;
    }
    begin(v, string_id, pitch, intensity, tone, ++serial, character,frames,automatic);
    return 1;
}
int gm_note_on(int part,int pitch,float intensity,int tone) { return note_on(part,pitch,intensity,tone,0,0); }
int gm_note_on_timed(int part,int pitch,float intensity,int tone,int frames) { return frames>=0?note_on(part,pitch,intensity,tone,frames,1):0; }
int gm_note_on_gated(int part,int pitch,float intensity,int tone,int frames,int ring) {
    if(frames<0)return 0;
    return note_on(part,pitch,intensity,tone,ring && frames?-frames:frames,1);
}
/* the voices still sounding on a string fade out quickly (FADE_SAMPLES): a string plays one note */
void gm_damp_string(int string_id) {
    for(int i=0;i<GM_VOICES;i++) {
        gm_voice *v=&voices[i];
        if(v->used && v->string_id==string_id && !v->fade && !v->pending_intensity) {
            if(v->remaining>FADE_SAMPLES) v->remaining=FADE_SAMPLES;
            v->fade=(int)v->remaining;
        }
    }
}
void gm_auto_stop(void) {
    for(int i=0;i<GM_VOICES;i++) {
        gm_voice *v=&voices[i];
        if(v->pending_intensity && v->pending_auto) v->pending_intensity=0;
        if(v->used && v->automatic) { if(v->remaining>FADE_SAMPLES) v->remaining=FADE_SAMPLES; v->fade=(int)v->remaining; }
    }
}
float *gm_render(int frames) {
    if (frames < 1 || frames > GM_BLOCK) return 0;
    memset(output, 0, sizeof(float) * frames);
    for (int slot = 0; slot < GM_VOICES; slot++) {
        gm_voice *v = &voices[slot];
        for (int offset = 0; offset < frames && v->used;) {
            int count = frames - offset;
            if (count > 32) count = 32;
            if (v->fade && count > v->fade) count = v->fade;
            if ((uint32_t)count > v->remaining) count = (int)v->remaining;
            if (!count) { v->used = 0; break; }
            int32_t excitation[32], samples[32];
            if(v->bowed) {
                /* Bounded periodic bow surrogate: no sustained dust/noise accumulation. */
                for(int i=0;i<count;i++) {
                    uint32_t ph=v->phase;
                    samples[i]=bow_wave[ph>>24]*20 + bow_wave[(ph*2u)>>24]*7 + bow_wave[(ph*3u)>>24]*3 + bow_wave[(ph*4u)>>24];
                    v->phase+=v->phase_step;
                }
            } else {
                px_string_excite(&v->coefficients, &v->state, excitation, (uint32_t)count);
                px_string_run(&v->coefficients, &v->state, excitation, samples, (uint32_t)count);
            }
            for (int i = 0; i < count; i++) {
                const uint32_t played = v->duration - v->remaining;
                float envelope = played < (uint32_t)v->attack ? played / (float)v->attack : 1.0f;
                // Only the final 30ms is forcibly tapered; model damping supplies the decay.
                uint32_t release=v->release_frames;
                if(release && v->remaining<release) envelope*=v->remaining/(float)release;
                if (v->fade) envelope *= v->fade / (float)FADE_SAMPLES;
                float sample=samples[i] / (float)PX_ONE;
                if(v->drive>0) sample=sample*(1.0f+v->drive)/(1.0f+v->drive*(sample<0?-sample:sample));
                output[offset + i] += sample * v->gain * envelope;
                v->remaining--;
                if (v->fade) v->fade--;
            }
            offset += count;
            if (v->pending_intensity && !v->fade) {
                int pitch = v->pending_pitch, tone = v->pending_tone, string_id = v->pending_string;
                float intensity = v->pending_intensity; uint32_t age = v->age;
                int pending_character=v->pending_character;
                int pending_frames=v->pending_frames,pending_auto=v->pending_auto;
                begin(v, string_id, pitch, intensity, tone, age, pending_character,pending_frames,pending_auto);
            } else if (!v->remaining) v->used = 0;
        }
    }
    for (int i = 0; i < frames; i++) {
        if (output[i] > 0.98f || output[i] < -0.98f) {
            clips++; output[i] = output[i] > 0 ? 0.98f : -0.98f;
        }
    }
    return output;
}
