#pragma once
// NABO TABLE renderer core. Each frame the scene builder fills a display list of draw ops;
// ops that changed since the last frame (or moved) mark dirty rectangles, and only those are
// composed, a few rows at a time, and sent to the panel. Integer maths only (no FPU on the C3).
#include <stdbool.h>
#include <stdint.h>

#include "nabo_dice_render.h"
#include "nabo_table_sprites.h"
#include "nabo_table_text.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NTG_W 240
#define NTG_H 320
#define NTG_MAX_OPS 80     // four seats of cards or dice, scores and the result banner
#define NTG_MAX_DICE 26    // 4 players x 6 dice at the reveal, plus the die in the bid label
#define NTG_MAX_CARDS 48
#define NTG_MAX_RECTS 16
#define NTG_STRIP 8

// Sends big-endian RGB565 for screen rows [y, y + h), columns [x, x + w).
typedef void (*ntg_send_fn)(void *ctx, int x, int y, int w, int h, uint16_t *px);

typedef enum {
    NTG_BG,       // wall above the rim, felt below, rim band
    NTG_RRECT,    // rounded rectangle, optional outline, alpha
    NTG_TEXT,
    NTG_CARD,     // face or back, horizontal squash for flips
    NTG_DIE,      // index into the posed dice
    NTG_SPRITE,   // NABO; the player is shared, one sprite op per frame
    NTG_CUP,
    NTG_TRI,      // small up / down arrow
    NTG_GLOW,     // soft round highlight behind a die
    NTG_DUSK,     // NABO DUEL: dusk sky, town silhouette, street (nabo_table_shapes.c)
} ntg_kind_t;

typedef struct {
    uint8_t kind;
    int16_t x0, y0, x1, y1;   // bounding box, exclusive end, unclipped
    uint32_t key;             // hash of everything that changes pixels
    union {
        struct { int16_t rim; } bg;
        struct { int16_t x, y, w, h, r; uint16_t fill, line; uint8_t alpha, line_w; } rect;
        struct { nt_text_t t; uint16_t color; } text;
        struct { int16_t x, y, w, h; uint8_t rank, suit, face, squash, text; } card;   // squash 0..64 (64 = full width)
        struct { uint8_t index, dim; } die;
        struct { int16_t ax, ay; uint8_t anim, frame; } sprite;
        struct { int16_t cx, by; uint8_t alpha; } cup;   // centre x, bottom y
        struct { int16_t cx, cy; uint16_t color; uint8_t up, size; } tri;
        struct { int16_t cx, cy, r; uint16_t color; } glow;
    } u;
} ntg_op_t;

typedef struct {
    // Display list being built for the next frame, and the one on screen.
    ntg_op_t ops[NTG_MAX_OPS], prev[NTG_MAX_OPS];
    int n, prev_n;
    nt_glyph_pool_t glyphs;
    nabo_dice_die_t dice[NTG_MAX_DICE];
    int dice_n;
    nt_text_t card_text[NTG_MAX_CARDS][2];   // rank index, court letter
    int card_n;
    bool full;                // redraw everything on the next frame
    int scene_id, prev_scene_id;
    nts_player_t sprite;
    const lv_font_t *font_small, *font_ui, *font_mono, *font_poster;
    ntg_send_fn send;
    void *ctx;
    uint16_t *strip[2];
    int which;
    uint32_t bytes;           // pixel bytes sent by the last frame
    bool overflow;            // a builder ran out of ops, dice, cards or glyphs this frame (host tests)
} ntg_t;

// strip buffers: two of NTG_W x NTG_STRIP pixels; sprite_buf: nts_buffer_need() bytes.
void ntg_init(ntg_t *g, const nts_pack_t *pack, uint8_t *sprite_buf, size_t sprite_buf_size, uint16_t *strip0,
              uint16_t *strip1, ntg_send_fn send, void *ctx);
void ntg_begin(ntg_t *g, int scene_id);   // starts a new display list
void ntg_invalidate(ntg_t *g);
void ntg_flush(ntg_t *g);                 // diffs, composes and sends the dirty areas

// Builders (return false when the list is full).
bool ntg_bg(ntg_t *g, int rim);
bool ntg_rect(ntg_t *g, int x, int y, int w, int h, int r, uint16_t fill, uint8_t alpha, uint16_t line, int line_w);
bool ntg_text(ntg_t *g, const lv_font_t *font, const char *utf8, int x, int y, nt_align_t align, uint16_t color);
bool ntg_card(ntg_t *g, int x, int y, int w, int h, int rank, int suit, bool face, int squash);
bool ntg_die(ntg_t *g, int value, int cx, int cy, int half, int yaw_deg, bool dim);
bool ntg_glow(ntg_t *g, int cx, int cy, int r, uint16_t color);
bool ntg_dusk(ntg_t *g);
bool ntg_sprite(ntg_t *g, int anim, int frame, int ax, int ay);
bool ntg_cup(ntg_t *g, int cx, int by, int alpha);
bool ntg_tri(ntg_t *g, int cx, int cy, int size, bool up, uint16_t color);
int ntg_text_width(const lv_font_t *font, const char *utf8);

// Per-op row painters (nabo_table_shapes.c).
void ntg_paint_bg(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_rect(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_card(const ntg_t *g, const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_cup(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_tri(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_glow(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);
void ntg_paint_dusk(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row);

static inline uint16_t ntg_rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3); }

#ifdef __cplusplus
}
#endif
