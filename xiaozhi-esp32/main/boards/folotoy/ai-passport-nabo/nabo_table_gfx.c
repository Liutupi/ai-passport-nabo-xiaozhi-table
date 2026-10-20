#include "nabo_table_gfx.h"

#include <string.h>

// ---------- display list ----------

static uint32_t mix(uint32_t h, uint32_t v)
{
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

static ntg_op_t *add(ntg_t *g, int kind, int x0, int y0, int x1, int y1)
{
    if (g->n >= NTG_MAX_OPS) { g->overflow = true; return NULL; }
    ntg_op_t *op = &g->ops[g->n++];
    memset(op, 0, sizeof(*op));
    op->kind = (uint8_t)kind;
    op->x0 = (int16_t)x0; op->y0 = (int16_t)y0; op->x1 = (int16_t)x1; op->y1 = (int16_t)y1;
    return op;
}

static void seal(ntg_op_t *op, uint32_t key)
{
    uint32_t h = mix(mix(mix(mix(mix(key, op->kind), (uint16_t)op->x0), (uint16_t)op->y0), (uint16_t)op->x1),
                     (uint16_t)op->y1);
    op->key = h;
}

void ntg_init(ntg_t *g, const nts_pack_t *pack, uint8_t *sprite_buf, size_t sprite_buf_size, uint16_t *strip0,
              uint16_t *strip1, ntg_send_fn send, void *ctx)
{
    memset(g, 0, sizeof(*g));
    nts_player_init(&g->sprite, pack, sprite_buf, sprite_buf_size);
    g->strip[0] = strip0;
    g->strip[1] = strip1;
    g->send = send;
    g->ctx = ctx;
    g->full = true;
    g->prev_scene_id = -1;
}

void ntg_invalidate(ntg_t *g) { g->full = true; }

void ntg_begin(ntg_t *g, int scene_id)
{
    g->n = 0;
    g->glyphs.used = 0;
    g->dice_n = 0;
    g->card_n = 0;
    g->overflow = false;
    g->scene_id = scene_id;
}

bool ntg_bg(ntg_t *g, int rim)
{
    ntg_op_t *op = add(g, NTG_BG, 0, 0, NTG_W, NTG_H);
    if (!op) return false;
    op->u.bg.rim = (int16_t)rim;
    seal(op, (uint32_t)rim);
    return true;
}

bool ntg_rect(ntg_t *g, int x, int y, int w, int h, int r, uint16_t fill, uint8_t alpha, uint16_t line, int line_w)
{
    ntg_op_t *op = add(g, NTG_RRECT, x, y, x + w, y + h);
    if (!op) return false;
    op->u.rect.x = (int16_t)x; op->u.rect.y = (int16_t)y; op->u.rect.w = (int16_t)w; op->u.rect.h = (int16_t)h;
    op->u.rect.r = (int16_t)r; op->u.rect.fill = fill; op->u.rect.alpha = alpha;
    op->u.rect.line = line; op->u.rect.line_w = (uint8_t)line_w;
    seal(op, mix(mix(mix(mix((uint32_t)r, fill), alpha), line), (uint32_t)line_w));
    return true;
}

int ntg_text_width(const lv_font_t *font, const char *utf8) { return nt_text_width(font, utf8); }

bool ntg_text(ntg_t *g, const lv_font_t *font, const char *utf8, int x, int y, nt_align_t align, uint16_t color)
{
    if (!font || !utf8 || !*utf8) return true;
    nt_text_t t;
    if (!nt_text_layout(&g->glyphs, &t, font, utf8, x, y, align)) { g->overflow = true; return false; }
    ntg_op_t *op = add(g, NTG_TEXT, t.x0, t.y0, t.x1, t.y1);
    if (!op) return false;
    op->u.text.t = t;
    op->u.text.color = color;
    uint32_t h = mix((uint32_t)(uintptr_t)font, color);
    for (const char *s = utf8; *s; s++) h = mix(h, (uint8_t)*s);
    seal(op, h);
    return true;
}

static const char *const RANK_STR[14] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

bool ntg_card(ntg_t *g, int x, int y, int w, int h, int rank, int suit, bool face, int squash)
{
    if (g->card_n >= NTG_MAX_CARDS) { g->overflow = true; return false; }
    int pad = w / 8 + 2;   // soft shadow below and to the right
    ntg_op_t *op = add(g, NTG_CARD, x - 1, y - 1, x + w + pad, y + h + pad);
    if (!op) return false;
    int slot = g->card_n++;
    op->u.card.x = (int16_t)x; op->u.card.y = (int16_t)y; op->u.card.w = (int16_t)w; op->u.card.h = (int16_t)h;
    op->u.card.rank = (uint8_t)rank; op->u.card.suit = (uint8_t)suit;
    op->u.card.face = face; op->u.card.squash = (uint8_t)(squash < 0 ? 0 : squash > 64 ? 64 : squash);
    op->u.card.text = (uint8_t)slot;
    memset(g->card_text[slot], 0, sizeof(g->card_text[slot]));
    if (face && rank >= 1 && rank <= 13) {
        // Rank index laid out at the card's origin; the painter moves and mirrors it.
        nt_text_layout(&g->glyphs, &g->card_text[slot][0], g->font_mono, RANK_STR[rank], 0, 0, NT_ALIGN_CENTER);
        if (rank >= 11 && w >= 40)
            nt_text_layout(&g->glyphs, &g->card_text[slot][1], g->font_poster, RANK_STR[rank], 0, 0, NT_ALIGN_CENTER);
    }
    seal(op, mix(mix(mix(mix((uint32_t)rank, (uint32_t)suit), face), op->u.card.squash), (uint32_t)(w << 8 | h)));
    return true;
}

bool ntg_die(ntg_t *g, int value, int cx, int cy, int half, int yaw_deg, bool dim)
{
    if (g->dice_n >= NTG_MAX_DICE) { g->overflow = true; return false; }
    nabo_dice_die_t *d = &g->dice[g->dice_n];
    // 58 degrees: steeper than the dice app's 47 so the top face reads at a glance.
    uint16_t yaw = (uint16_t)(((yaw_deg % 360) + 360) % 360 * 65536 / 360);
    nabo_dice_pose(d, (unsigned)value, cx, cy, half, yaw, (uint16_t)(58 * 65536 / 360));
    int x0 = d->bx0 < d->sx0 ? d->bx0 : d->sx0, y0 = d->by0 < d->sy0 ? d->by0 : d->sy0;
    int x1 = d->bx1 > d->sx1 ? d->bx1 : d->sx1, y1 = d->by1 > d->sy1 ? d->by1 : d->sy1;
    ntg_op_t *op = add(g, NTG_DIE, x0, y0, x1 + 1, y1 + 1);
    if (!op) return false;
    op->u.die.index = (uint8_t)g->dice_n++;
    op->u.die.dim = dim;
    seal(op, mix(mix(mix((uint32_t)value, (uint32_t)half), yaw), dim));
    return true;
}

bool ntg_glow(ntg_t *g, int cx, int cy, int r, uint16_t color)
{
    ntg_op_t *op = add(g, NTG_GLOW, cx - r, cy - r, cx + r + 1, cy + r + 1);
    if (!op) return false;
    op->u.glow.cx = (int16_t)cx; op->u.glow.cy = (int16_t)cy; op->u.glow.r = (int16_t)r; op->u.glow.color = color;
    seal(op, mix((uint32_t)r, color));
    return true;
}

bool ntg_sprite(ntg_t *g, int anim, int frame, int ax, int ay)
{
    nts_anim_t a;
    if (!nts_anim(g->sprite.pack, anim, &a)) return false;
    ntg_op_t *op = add(g, NTG_SPRITE, ax + a.ox, ay + a.oy, ax + a.ox + a.w, ay + a.oy + a.h);
    if (!op) return false;
    op->u.sprite.ax = (int16_t)ax; op->u.sprite.ay = (int16_t)ay;
    op->u.sprite.anim = (uint8_t)anim; op->u.sprite.frame = (uint8_t)frame;
    seal(op, mix((uint32_t)anim, (uint32_t)frame));
    return true;
}

bool ntg_dusk(ntg_t *g)
{
    ntg_op_t *op = add(g, NTG_DUSK, 0, 0, NTG_W, NTG_H);
    if (!op) return false;
    seal(op, 0xD05C);
    return true;
}

bool ntg_cup(ntg_t *g, int cx, int by, int alpha)
{
    ntg_op_t *op = add(g, NTG_CUP, cx - 26, by - 52, cx + 27, by + 4);
    if (!op) return false;
    op->u.cup.cx = (int16_t)cx; op->u.cup.by = (int16_t)by; op->u.cup.alpha = (uint8_t)alpha;
    seal(op, (uint32_t)alpha);
    return true;
}

bool ntg_tri(ntg_t *g, int cx, int cy, int size, bool up, uint16_t color)
{
    ntg_op_t *op = add(g, NTG_TRI, cx - size, cy - size, cx + size + 1, cy + size + 1);
    if (!op) return false;
    op->u.tri.cx = (int16_t)cx; op->u.tri.cy = (int16_t)cy; op->u.tri.size = (uint8_t)size;
    op->u.tri.up = up; op->u.tri.color = color;
    seal(op, mix(mix((uint32_t)size, up), color));
    return true;
}

// ---------- dirty rectangles ----------

typedef struct { int16_t x0, y0, x1, y1; } rect_t;

static bool same_op(const ntg_op_t *a, const ntg_op_t *b)
{
    return a->kind == b->kind && a->key == b->key;
}

static void add_rect(rect_t *r, int *n, int x0, int y0, int x1, int y1, bool *overflow)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > NTG_W) x1 = NTG_W;
    if (y1 > NTG_H) y1 = NTG_H;
    if (x0 >= x1 || y0 >= y1) return;
    // Merge with anything it touches (8 px slack keeps the list short).
    for (int i = 0; i < *n; i++) {
        if (x0 <= r[i].x1 + 8 && r[i].x0 <= x1 + 8 && y0 <= r[i].y1 + 8 && r[i].y0 <= y1 + 8) {
            int a = x0 < r[i].x0 ? x0 : r[i].x0, b = y0 < r[i].y0 ? y0 : r[i].y0;
            int c = x1 > r[i].x1 ? x1 : r[i].x1, d = y1 > r[i].y1 ? y1 : r[i].y1;
            r[i] = r[--*n];
            add_rect(r, n, a, b, c, d, overflow);
            return;
        }
    }
    if (*n >= NTG_MAX_RECTS) { *overflow = true; return; }
    r[*n].x0 = (int16_t)x0; r[*n].y0 = (int16_t)y0; r[*n].x1 = (int16_t)x1; r[*n].y1 = (int16_t)y1;
    ++*n;
}

// ---------- composition ----------

static void paint(ntg_t *g, const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    if (y < op->y0 || y >= op->y1 || x1 <= op->x0 || x0 >= op->x1) return;
    switch (op->kind) {
    case NTG_BG: ntg_paint_bg(op, y, x0, x1, row); break;
    case NTG_RRECT: ntg_paint_rect(op, y, x0, x1, row); break;
    case NTG_TEXT: nt_text_row(&g->glyphs, &op->u.text.t, y, x0, x1, row, op->u.text.color); break;
    case NTG_CARD: ntg_paint_card(g, op, y, x0, x1, row); break;
    case NTG_DIE: {
        uint16_t tmp[NTG_W];
        if (!op->u.die.dim) { nabo_dice_die_row(&g->dice[op->u.die.index], y, x0, x1 - 1, row); break; }
        // A die that does not count at the reveal: drawn, then faded back towards the felt.
        memcpy(tmp, row, (size_t)(x1 - x0) * 2);
        nabo_dice_die_row(&g->dice[op->u.die.index], y, x0, x1 - 1, row);
        for (int x = 0; x < x1 - x0; x++) row[x] = nt_blend565(tmp[x], row[x], 90);
        break;
    }
    case NTG_SPRITE:
        nts_player_row(&g->sprite, y - op->y0, op->x0, x0, x1, row);
        break;
    case NTG_CUP: ntg_paint_cup(op, y, x0, x1, row); break;
    case NTG_TRI: ntg_paint_tri(op, y, x0, x1, row); break;
    case NTG_GLOW: ntg_paint_glow(op, y, x0, x1, row); break;
    case NTG_DUSK: ntg_paint_dusk(op, y, x0, x1, row); break;
    default: break;
    }
}

static void compose(ntg_t *g, int x0, int y0, int x1, int y1)
{
    int w = x1 - x0;
    for (int y = y0; y < y1; y += NTG_STRIP) {
        int h = y + NTG_STRIP < y1 ? NTG_STRIP : y1 - y;
        uint16_t *px = g->strip[g->which];
        g->which ^= 1;
        for (int r = 0; r < h; r++)
            for (int i = 0; i < g->n; i++) paint(g, &g->ops[i], y + r, x0, x1, px + r * w);
        for (int k = 0; k < w * h; k++) px[k] = (uint16_t)(px[k] << 8 | px[k] >> 8);   // panel order
        g->send(g->ctx, x0, y, w, h, px);
        g->bytes += (uint32_t)(w * h * 2);
    }
}

void ntg_flush(ntg_t *g)
{
    // Only one sprite op per frame: point the shared player at it before painting.
    for (int i = 0; i < g->n; i++)
        if (g->ops[i].kind == NTG_SPRITE) nts_player_set(&g->sprite, g->ops[i].u.sprite.anim, g->ops[i].u.sprite.frame);
    rect_t rects[NTG_MAX_RECTS];
    int n = 0;
    bool overflow = g->full || g->scene_id != g->prev_scene_id;
    if (!overflow) {
        bool matched_prev[NTG_MAX_OPS] = {0};
        for (int i = 0; i < g->n && !overflow; i++) {
            const ntg_op_t *op = &g->ops[i];
            int j = 0;
            while (j < g->prev_n && (matched_prev[j] || !same_op(op, &g->prev[j]))) j++;
            if (j < g->prev_n) matched_prev[j] = true;
            else add_rect(rects, &n, op->x0, op->y0, op->x1, op->y1, &overflow);
        }
        for (int j = 0; j < g->prev_n && !overflow; j++)
            if (!matched_prev[j]) add_rect(rects, &n, g->prev[j].x0, g->prev[j].y0, g->prev[j].x1, g->prev[j].y1, &overflow);
    }
    g->bytes = 0;
    if (overflow) {
        compose(g, 0, 0, NTG_W, NTG_H);
    } else {
        for (int i = 0; i < n; i++) compose(g, rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
    }
    memcpy(g->prev, g->ops, sizeof(ntg_op_t) * (size_t)g->n);
    g->prev_n = g->n;
    g->prev_scene_id = g->scene_id;
    g->full = false;
}
