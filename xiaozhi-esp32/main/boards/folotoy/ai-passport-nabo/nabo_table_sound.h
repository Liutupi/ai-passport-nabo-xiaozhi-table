#pragma once
// NABO TABLE sound: 16 kHz mono, synthesized on the fly from the model's events (no samples
// in flash). Dice knocks reuse the dice app's damped resonators; cards are filtered noise;
// "open" and wins are short bells. Mirrors nabo-table/prototype/sound.js.
#include <stddef.h>
#include <stdint.h>

#include "nabo_dice_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NTS_VOICES 8

typedef enum { NTV_OFF, NTV_KNOCK, NTV_NOISE, NTV_BELL, NTV_THUMP, NTV_WOOD } nt_voice_kind_t;

typedef struct {
    uint8_t kind;
    uint32_t at, len;          // samples since start, total length
    uint32_t delay;            // samples before it starts
    int32_t gain;              // Q8
    // noise: one-pole band (lo, hi) coefficients Q12, attack in samples, decay power
    int32_t lo, hi, lp, hp, attack, power;
    uint32_t next_click, gap, burst;   // click train (shuffle): gap shrinks after each click
    uint32_t rng;
    // bell / thump: phase and step (Q16 of one turn per sample), decay per sample Q16
    uint32_t phase, step, phase2, step2;
    int32_t env, decay;
    nabo_dice_sound_t knock;   // dice resonators fed with our own hit list
} nt_voice_t;

typedef struct {
    nt_voice_t v[NTS_VOICES];
    uint32_t seed;
} nt_sound_t;

void nt_sound_reset(nt_sound_t *s);
// Starts the sounds for a model event mask (NT_EV_*).
void nt_sound_events(nt_sound_t *s, uint32_t events);
// NABO DUEL cues (NDU_CUE_* bits from nabo_duel.h). Wind is silent; wind_ms is retained for compatibility.
void nt_sound_duel(nt_sound_t *s, uint32_t cues, uint32_t wind_ms);
// Mixes n samples into out (adds, with clipping).
void nt_sound_fill(nt_sound_t *s, int16_t *out, size_t n);

#ifdef __cplusplus
}
#endif
