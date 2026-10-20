#include "nabo_table_sound.h"

#include <string.h>

#include "nabo_duel.h"
#include "nabo_table_model.h"

#define SR 16000
#define MS(x) ((uint32_t)(x) * (SR / 1000))

static uint32_t next_rand(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static nt_voice_t *alloc_voice(nt_sound_t *s)
{
    nt_voice_t *oldest = &s->v[0];
    for (int i = 0; i < NTS_VOICES; i++) {
        if (s->v[i].kind == NTV_OFF) return &s->v[i];
        if (s->v[i].at > oldest->at) oldest = &s->v[i];
    }
    return oldest;   // steal the voice that has played longest
}

static void knock(nt_sound_t *s, const nabo_dice_impact_t *hits, int n, uint32_t delay_ms, int gain)
{
    nt_voice_t *v = alloc_voice(s);
    memset(v, 0, sizeof(*v));
    v->kind = NTV_KNOCK;
    v->delay = MS(delay_ms);
    v->gain = gain;
    nabo_dice_sound_t *k = &v->knock;
    k->hits = (uint8_t)(n < NABO_DICE_IMPACTS_MAX ? n : NABO_DICE_IMPACTS_MAX);
    memcpy(k->hit, hits, sizeof(*hits) * k->hits);
    k->noise = next_rand(&s->seed) | 1u;
    k->end = (uint32_t)hits[k->hits - 1].ms * 16u + 4800u;
}

static void knock1(nt_sound_t *s, int amp, uint32_t delay_ms, int gain)
{
    nabo_dice_impact_t h = {0, (uint16_t)amp};
    knock(s, &h, 1, delay_ms, gain);
}

// Band of noise: lo/hi are one-pole corner frequencies in Hz (0 = open), power shapes the decay.
static nt_voice_t *noise(nt_sound_t *s, uint32_t ms, uint32_t attack_ms, int power, int lo, int hi, uint32_t delay_ms, int gain)
{
    nt_voice_t *v = alloc_voice(s);
    memset(v, 0, sizeof(*v));
    v->kind = NTV_NOISE;
    v->len = MS(ms);
    uint32_t attack = MS(attack_ms);
    v->attack = (int32_t)(attack > 0 ? attack : 1);
    v->power = power;
    v->lo = lo ? lo * 25736 / SR : 0;            // Q12 of 2*pi*f/fs
    v->hi = hi ? (hi * 25736 / SR > 4095 ? 4095 : hi * 25736 / SR) : 4095;
    v->delay = MS(delay_ms);
    v->gain = gain;
    v->rng = next_rand(&s->seed) | 1u;
    return v;
}

static void tone(nt_sound_t *s, int kind, int hz, uint32_t ms, uint32_t delay_ms, int gain)
{
    nt_voice_t *v = alloc_voice(s);
    memset(v, 0, sizeof(*v));
    v->kind = (uint8_t)kind;
    v->len = MS(ms);
    v->delay = MS(delay_ms);
    v->gain = gain;
    v->step = (uint32_t)(((uint64_t)hz << 32) / SR);   // one turn = 2^32
    v->step2 = v->step * 276 / 100;             // bell partial at 2.76 x
    v->env = 1 << 16;
    v->decay = kind == NTV_THUMP ? 65420 : 65507;   // about exp(-28 t) and exp(-7 t)
}

void nt_sound_reset(nt_sound_t *s)
{
    memset(s, 0, sizeof(*s));
    s->seed = 0x2545F491u;
}

void nt_sound_events(nt_sound_t *s, uint32_t e)
{
    if (e & NT_EV_MOVE) knock1(s, 20, 0, 128);
    if (e & NT_EV_OK) {
        nabo_dice_impact_t h[2] = {{0, 46}, {12, 16}};
        knock(s, h, 2, 0, 154);
    }
    if (e & (NT_EV_CARD | NT_EV_HIT)) {   // a card slides over the felt, then settles
        noise(s, 95, 6, 2, 2000, 4500, 0, 140);
        knock1(s, 14, 80, 90);
    }
    if (e & NT_EV_HIT) {
        noise(s, 28, 1, 4, 2200, 0, 130, 180);
        knock1(s, 24, 142, 100);
    }
    if (e & NT_EV_FLIP) {
        noise(s, 28, 1, 4, 2200, 0, 0, 180);
        knock1(s, 24, 12, 100);
    }
    if (e & NT_EV_SHUFFLE) {   // riffle of the cut: faster and faster paper clicks, then squared up
        nt_voice_t *v = noise(s, 680, 0, 0, 1800, 0, 0, 128);
        v->gap = MS(24);
        v->next_click = 0;
        nabo_dice_impact_t h[2] = {{0, 70}, {18, 30}};
        knock(s, h, 2, 680, 128);
    }
    if (e & NT_EV_RATTLE) {   // dice bouncing inside the cup
        nabo_dice_impact_t h[NABO_DICE_IMPACTS_MAX];
        int n = 0;
        for (uint32_t t = 0; t < 1050 && n < NABO_DICE_IMPACTS_MAX; t += 28 + next_rand(&s->seed) % 42)
            h[n++] = (nabo_dice_impact_t){(uint16_t)t, (uint16_t)(40 + next_rand(&s->seed) % 100)};
        knock(s, h, n, 0, 115);
    }
    if (e & NT_EV_SLAM) {
        nabo_dice_impact_t h[4] = {{0, 256}, {16, 110}, {52, 60}, {95, 26}};
        knock(s, h, 4, 180, 154);
        tone(s, NTV_THUMP, 95, 180, 180, 205);
    }
    if (e & NT_EV_OPEN) {
        noise(s, 220, 30, 2, 0, 1400, 0, 128);
        tone(s, NTV_BELL, 784, 600, 50, 64);
        tone(s, NTV_BELL, 1175, 700, 140, 64);
    }
    if (e & NT_EV_LIFT) noise(s, 260, 60, 1, 500, 1400, 0, 154);
    if (e & NT_EV_WIN) {
        static const int NOTES[4] = {523, 659, 784, 1047};
        for (int i = 0; i < 4; i++) tone(s, NTV_BELL, NOTES[i], 700, (uint32_t)(i * 90), 56);
    }
}

void nt_sound_duel(nt_sound_t *s, uint32_t c, uint32_t wind_ms)
{
    // Same voices as the table, tuned after game-art/duel/prototype (playFx / ambient).
    if (c & NDU_CUE_TICK) knock1(s, 20, 0, 128);
    if (c & NDU_CUE_INVITE) tone(s, NTV_BELL, 1200, 220, 0, 64);
    if (c & NDU_CUE_READY) knock1(s, 30, 0, 110);
    if (c & NDU_CUE_WIND) noise(s, wind_ms > 6000 ? 6000 : wind_ms, 400, 0, 0, 500, 0, 40);   // low wind until the draw
    if (c & NDU_CUE_HEART) tone(s, NTV_THUMP, 58, 160, 0, 230);
    if (c & NDU_CUE_FAKE) noise(s, 60, 1, 4, 1800, 3200, 0, 150);
    if (c & NDU_CUE_GO) {   // the bell of the draw
        tone(s, NTV_BELL, 1760, 500, 0, 110);
        tone(s, NTV_BELL, 2640, 350, 0, 50);
    }
    if (c & NDU_CUE_SHOT) noise(s, 120, 1, 2, 800, 3000, 0, 170);       // water pistol
    if (c & NDU_CUE_FOUL) tone(s, NTV_THUMP, 140, 350, 0, 120);
    if (c & NDU_CUE_WIN) {
        noise(s, 400, 5, 2, 400, 1800, 0, 140);                        // splash on the other side
        static const int NOTES[3] = {523, 659, 784};
        for (int i = 0; i < 3; i++) tone(s, NTV_BELL, NOTES[i], 260, (uint32_t)(250 + i * 120), 60);
    }
    if (c & NDU_CUE_LOSE) noise(s, 500, 5, 2, 300, 1500, 0, 150);
    if (c & NDU_CUE_REPLAY) knock1(s, 24, 0, 100);
    if (c & NDU_CUE_LEFT) tone(s, NTV_BELL, 523, 400, 0, 50);
}

static int32_t sine(uint32_t phase)   // phase: 16 bits per turn -> Q15
{
    static const int16_t Q[17] = {0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170,
                                  25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767};
    uint32_t p = phase & 0xFFFF, q = p & 0x3FFF;
    int32_t i = (int32_t)(q >> 10), f = (int32_t)(q & 1023);
    if ((p >> 14) & 1) { i = 16 - i - 1; f = 1024 - f; }
    int32_t v = Q[i] + (Q[i + 1] - Q[i]) * f / 1024;
    return p >= 0x8000 ? -v : v;
}

static int32_t voice_sample(nt_voice_t *v)
{
    if (v->delay) { v->delay--; return 0; }
    int32_t x = 0;
    switch (v->kind) {
    case NTV_KNOCK: {
        int16_t one;
        if (!nabo_dice_sound_fill(&v->knock, &one, 1)) { v->kind = NTV_OFF; return 0; }
        return one * v->gain >> 8;
    }
    case NTV_NOISE: {
        if (v->at >= v->len) { v->kind = NTV_OFF; return 0; }
        int32_t w = (int32_t)(next_rand(&v->rng) >> 17) - 16384;   // white, Q14
        int32_t env = 32768;
        if ((int32_t)v->at < v->attack) env = env * (int32_t)v->at / v->attack;
        int32_t rest = (int32_t)((uint64_t)(v->len - v->at) * 32768 / v->len);
        for (int k = 0; k < v->power; k++) env = env * rest >> 15;
        if (v->gap) {   // click train: short bursts, gap shrinking towards 8 ms
            if (v->at >= v->next_click) { v->burst = MS(3); v->next_click = v->at + v->gap; v->gap = v->gap * 15 / 16 + MS(8) / 16; }
            env = v->burst ? env * (int32_t)v->burst / MS(3) : 0;
            if (v->burst) v->burst--;
        }
        v->lp += (w - v->lp) * v->hi >> 12;          // low-pass at hi
        int32_t band = v->lp;
        if (v->lo) { v->hp += (v->lp - v->hp) * v->lo >> 12; band = v->lp - v->hp; }   // minus low-pass at lo
        x = band * env >> 15;
        x = x * 2;   // Q14 noise to roughly full scale
        break;
    }
    case NTV_BELL:
    case NTV_THUMP: {
        if (v->at >= v->len || v->env < 64) { v->kind = NTV_OFF; return 0; }
        v->phase += v->step;
        int32_t a = sine(v->phase >> 16);
        if (v->kind == NTV_BELL) {
            v->phase2 += v->step2;
            a += sine(v->phase2 >> 16) * 35 / 100 * (v->env >> 8) / 256;
        }
        int32_t attack = v->at < 40 ? (int32_t)v->at * 65536 / 40 : 65536;
        x = (int32_t)((int64_t)a * (v->env >> 1) >> 15) * (attack >> 8) >> 8;
        v->env = (int32_t)((int64_t)v->env * v->decay >> 16);
        break;
    }
    default: return 0;
    }
    v->at++;
    return x * v->gain >> 8;
}

void nt_sound_fill(nt_sound_t *s, int16_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int32_t mix = out[i];
        for (int k = 0; k < NTS_VOICES; k++)
            if (s->v[k].kind != NTV_OFF) mix += voice_sample(&s->v[k]);
        out[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
}
