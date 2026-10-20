// Host side of the NABO DUEL runtime for test_duel.py: glue instances (nabo_duel.c) and the real
// renderer drawing them into a 240x320 framebuffer, plus a second renderer that always redraws
// everything, so the dirty-rectangle path is checked pixel for pixel.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nabo_table_scene_duel.h"

LV_FONT_DECLARE(nabo_table_font_12)
LV_FONT_DECLARE(nabo_menu_font_16)
LV_FONT_DECLARE(nabo_ui_font_18)
LV_FONT_DECLARE(nabo_table_font_24)
LV_FONT_DECLARE(nabo_mono_font_12)
LV_FONT_DECLARE(nabo_poster_font_28)
LV_FONT_DECLARE(nabo_duel_font_56)

#define GLUES 3

typedef struct {
    ntg_t g;
    uint16_t fb[NTG_W * NTG_H];
    uint16_t strip[2][NTG_W * NTG_STRIP];
    uint8_t *sprite_buf;
} target_t;

static nabo_duel_t s_glue[GLUES];
static nts_pack_t pack;
static uint8_t *pack_data;
static target_t *inc, *full;
static nt_fonts_t fonts;
static uint8_t s_wire[ND_OUTBOX][ND_WIRE_MAX];
static int s_wire_len[ND_OUTBOX], s_wire_to[ND_OUTBOX];

static void send(void *ctx, int x, int y, int w, int h, uint16_t *px)
{
    target_t *t = ctx;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            uint16_t v = px[r * w + c];
            t->fb[(y + r) * NTG_W + x + c] = (uint16_t)(v << 8 | v >> 8);
        }
}

static target_t *make_target(void)
{
    target_t *t = calloc(1, sizeof(*t));
    size_t need = nts_buffer_need(&pack);
    t->sprite_buf = malloc(need);
    ntg_init(&t->g, &pack, t->sprite_buf, need, t->strip[0], t->strip[1], send, t);
    return t;
}

int rh_init(const char *pack_path)
{
    FILE *f = fopen(pack_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    pack_data = malloc((size_t)n);
    if (fread(pack_data, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -2; }
    fclose(f);
    if (!nts_open(&pack, pack_data, (size_t)n)) return -3;
    fonts = (nt_fonts_t){&nabo_table_font_12, &nabo_menu_font_16, &nabo_ui_font_18, &nabo_table_font_24,
                         &nabo_mono_font_12, &nabo_poster_font_28};
    inc = make_target();
    full = make_target();
    return (int)nts_buffer_need(&pack);
}

int rh_glue_size(void) { return (int)sizeof(nabo_duel_t); }

void rh_new(int i, int mac, unsigned entropy, int bo, int fake, int ai, unsigned now)
{
    uint8_t m[ND_MAC_LEN] = {0, 0, 0, 0, (uint8_t)(mac >> 8), (uint8_t)mac};
    nd_settings_t set = {(uint8_t)bo, (uint8_t)ai, fake != 0};
    ndu_init(&s_glue[i], m, entropy, &set, 0, now);
    s_glue[i].m.suppress = true;
}

void rh_key(int i, int k, int down, unsigned t) { ndu_key(&s_glue[i], (nd_key_t)k, down != 0, t); }

int rh_rx(int i, int from, const uint8_t *wire, int len, int rssi, unsigned t)
{
    nd_pkt_t p;
    if (!nd_decode(wire, (size_t)len, &p)) return 0;
    uint8_t m[ND_MAC_LEN] = {0, 0, 0, 0, (uint8_t)(from >> 8), (uint8_t)from};
    ndu_rx(&s_glue[i], m, &p, rssi, t);
    return 1;
}

void rh_advance(int i, unsigned now) { ndu_advance(&s_glue[i], now); }

// Draw time still to come (or due) for the current attempt; 0 when nothing is pending.
int rh_go_pending(int i, unsigned *go_at) { return ndu_go_pending(&s_glue[i], go_at); }
void rh_flipped(int i, unsigned t) { ndu_flipped(&s_glue[i], t); }
int rh_inverted(int i) { return ndu_inverted(&s_glue[i]); }
unsigned rh_cues(int i) { return ndu_take_cues(&s_glue[i]); }
int rh_exit(int i) { return s_glue[i].m.exit_req; }
int rh_dropped(int i) { return s_glue[i].in_dropped; }
void rh_set_sel(int i, int sel) { s_glue[i].m.sel = (uint8_t)sel; }

// scene phase me opp round has_cur me_has me_k me_r opp_has opp_k opp_r resolved go_at go_shown ticked
void rh_state(int i, long long *o)
{
    const nabo_duel_t *u = &s_glue[i];
    const nd_t *m = &u->m;
    long long v[] = {m->scene, m->scene == ND_DUEL ? (long long)nd_phase(m, u->ticked) : -1LL, m->match.me,
                     m->match.opp, m->match.round, m->has_cur, m->cur.has_me, m->cur.me.k, m->cur.me.r,
                     m->cur.has_opp, m->cur.opp.k, m->cur.opp.r, m->cur.resolved, m->cur.go_at,
                     m->cur.has_go_shown ? (long long)m->cur.go_shown : -1LL, u->ticked};
    memcpy(o, v, sizeof(v));
}

int rh_take_out(int i)
{
    nd_t *d = &s_glue[i].m;
    int n = d->out_n;
    for (int k = 0; k < n; k++) {
        s_wire_len[k] = (int)nd_encode(&d->out[k].pkt, s_wire[k], ND_WIRE_MAX);
        s_wire_to[k] = d->out[k].broadcast ? -1 : (d->out[k].to[4] << 8 | d->out[k].to[5]);
    }
    d->out_n = 0;
    return n;
}

int rh_out(int k, uint8_t *buf) { memcpy(buf, s_wire[k], (size_t)s_wire_len[k]); return s_wire_len[k]; }
int rh_out_to(int k) { return s_wire_to[k]; }

// Draws glue i incrementally; with check, also redraws everything and returns -1 on a mismatch,
// -2 when a builder ran out of ops or glyphs. Otherwise the bytes sent.
int rh_draw(int i, int check)
{
    nt_scene_duel(&inc->g, &s_glue[i], &fonts, &nabo_duel_font_56);
    bool over = inc->g.overflow;
    ntg_flush(&inc->g);
    int bytes = (int)inc->g.bytes;
    if (over) return -2;
    if (!check) return bytes;
    ntg_invalidate(&full->g);
    nt_scene_duel(&full->g, &s_glue[i], &fonts, &nabo_duel_font_56);
    ntg_flush(&full->g);
    return memcmp(inc->fb, full->fb, sizeof(inc->fb)) ? -1 : bytes;
}

void rh_frame(uint16_t *out) { memcpy(out, inc->fb, sizeof(inc->fb)); }
