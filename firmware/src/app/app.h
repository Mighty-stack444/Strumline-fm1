/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/omni.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x4D555253u           /* "SRUM" */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* omni.h P_* */
    uint8_t prog[OM_SLOTS];              /* the progression (om_prog), copied in and out on load and save */
    uint8_t pat_last;                    /* the pattern ARP turns back on */
    uint8_t leds;                        /* lights: 0 off, 1 on */
    uint8_t preset;                      /* the progression preset loaded last */
    uint8_t rsv[13];
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
