// Row painters for NABO TABLE's procedural shapes: felt and rim, rounded rectangles, cards
// (face, back, flip squash), suits, the dice cup, arrows and glows. Coordinates inside shapes
// are Q4 (1/16 px); coverage is 0..256 with about one pixel of anti-aliasing. No floats.
#include <string.h>

#include "nabo_table_gfx.h"

#define C565(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))

static int iabs_(int v) { return v < 0 ? -v : v; }
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t isqrt_u(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

// Signed distance (Q4) to a rounded box centred at (cx, cy) Q4 with half size (hx, hy) Q4, radius r Q4.
static int rbox_sd(int px, int py, int cx, int cy, int hx, int hy, int r)
{
    int qx = iabs_(px - cx) - (hx - r), qy = iabs_(py - cy) - (hy - r);
    int out = qx > 0 && qy > 0 ? (int)isqrt_u((uint32_t)(qx * qx + qy * qy)) : imax(qx, qy);
    return out - r;
}

static int cov_from_sd(int sd) { return clampi(8 - sd, 0, 16) * 16; }   // 256 well inside, 0 outside

static uint16_t lerp565(int r0, int g0, int b0, int r1, int g1, int b1, int t)   // t 0..256
{
    return C565(r0 + (r1 - r0) * t / 256, g0 + (g1 - g0) * t / 256, b0 + (b1 - b0) * t / 256);
}

// ---------- background ----------

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void ntg_paint_bg(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    int rim = op->u.bg.rim;
    if (y < rim - 4) {   // wall: dark green-black, a touch lighter towards the table
        uint16_t c = lerp565(7, 19, 15, 10, 26, 23, rim > 4 ? y * 256 / (rim - 4) : 0);
        for (int x = x0; x < x1; x++) row[x - x0] = c;
        return;
    }
    if (y < rim + 5) {   // wooden rim with a lit top edge and a brass inlay underneath
        uint16_t c;
        if (y == rim - 4) c = C565(124, 92, 72);
        else if (y == rim + 4) c = C565(201, 162, 74);
        else c = y < rim ? lerp565(90, 58, 38, 58, 36, 24, (y - rim + 4) * 64)
                         : lerp565(58, 36, 24, 34, 20, 12, (y - rim) * 64);
        for (int x = x0; x < x1; x++) row[x - x0] = c;
        return;
    }
    // Felt: vignette around (120, 190), fine nap noise, the rim's shadow just below it.
    static uint8_t root[257];
    if (!root[256])
        for (int i = 0; i <= 256; i++) root[i] = (uint8_t)(isqrt_u((uint32_t)i * 256) > 255 ? 255 : isqrt_u((uint32_t)i * 256));
    int dy = y - 190, ry = dy * dy * 256 / (190 * 190);
    int shade = y - (rim + 5) < 14 ? 115 + (y - (rim + 5)) * 10 : 256;
    for (int x = x0; x < x1; x++) {
        int dx = x - 120, d2 = imin(256, dx * dx * 256 / (150 * 150) + ry);
        int t = 256 - root[d2] * 3 / 4;   // 256 at the centre, 64 at the edge
        int n = (int)(hash2(x, y) & 15) - 7 + ((x * 7 + y * 3) % 5 == 0 ? 4 : 0);
        int r = 12 + 24 * t / 256 + n / 2, g = 48 + 64 * t / 256 + n, b = 40 + 46 * t / 256 + n * 4 / 5;
        if (shade < 256) { r = r * shade / 256; g = g * shade / 256; b = b * shade / 256; }
        row[x - x0] = C565(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
    }
}

// ---------- rounded rectangles ----------

void ntg_paint_rect(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    int w = op->u.rect.w, h = op->u.rect.h, r = op->u.rect.r * 16, lw = op->u.rect.line_w * 16;
    int cx = op->u.rect.x * 16 + w * 8, cy = op->u.rect.y * 16 + h * 8, py = y * 16 + 8;
    int a = imax(x0, op->x0), b = imin(x1, op->x1);
    for (int x = a; x < b; x++) {
        int sd = rbox_sd(x * 16 + 8, py, cx, cy, w * 8, h * 8, r);
        int cov = cov_from_sd(sd);
        if (!cov) continue;
        uint16_t *p = &row[x - x0];
        if (op->u.rect.alpha) *p = nt_blend565(*p, op->u.rect.fill, cov * op->u.rect.alpha / 255);
        if (lw) {
            int ring = cov - cov_from_sd(sd + lw);
            if (ring > 0) *p = nt_blend565(*p, op->u.rect.line, ring);
        }
    }
}

void ntg_paint_glow(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    int r = op->u.glow.r, dy = y - op->u.glow.cy;
    for (int x = imax(x0, op->x0); x < imin(x1, op->x1); x++) {
        int dx = x - op->u.glow.cx, d2 = dx * dx + dy * dy;
        if (d2 >= r * r) continue;
        int a = 180 * (r * r - d2) / (r * r);
        row[x - x0] = nt_blend565(row[x - x0], op->u.glow.color, a);
    }
}

void ntg_paint_tri(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    int s = op->u.tri.size, k = y - (op->u.tri.cy - s / 2);   // rows 0..s-1
    if (k < 0 || k >= s) return;
    int half = (op->u.tri.up ? k : s - 1 - k) * 16 * 2 / 3 + 8;   // Q4, widest at the base
    int cx = op->u.tri.cx * 16 + 8;
    for (int x = imax(x0, op->x0); x < imin(x1, op->x1); x++) {
        int cov = clampi(half - iabs_(x * 16 + 8 - cx) + 8, 0, 16) * 16;
        if (cov) row[x - x0] = nt_blend565(row[x - x0], op->u.tri.color, cov);
    }
}

// ---------- suits: shapes in a 64-unit box (y down), distances in units ----------

static int circle_sd(int x, int y, int cx, int cy, int r)
{
    int dx = x - cx, dy = y - cy;
    return (int)isqrt_u((uint32_t)(dx * dx + dy * dy)) - r;
}

// Half-plane through (ax, ay) with outward normal (nx, ny) * 256.
static int plane_sd(int x, int y, int ax, int ay, int nx, int ny) { return ((x - ax) * nx + (y - ay) * ny) / 256; }

// Triangle (apex down when tip_y > base_y) as the max of its three half-planes; units Q4.
static int tri_sd(int x, int y, int half, int base_y, int tip_y)
{
    int h = tip_y - base_y, s = h > 0 ? 1 : -1, len = (int)isqrt_u((uint32_t)(half * half + h * h));
    int nx = iabs_(h) * 256 / len, ny = half * 256 / len * s;
    int a = plane_sd(x, y, -half, base_y, -nx, ny), b = plane_sd(x, y, half, base_y, nx, ny);
    int c = s * (base_y - y);
    return imax(imax(a, b), c);
}

static int suit_sd(int suit, int x, int y)   // x, y in Q4 units of the 64 box; result Q4 units
{
    switch (suit) {
    case 3:   // diamond
        return (iabs_(x) * 30 + iabs_(y) * 22 - 22 * 30 * 16) / 37;
    case 1:   // heart: two lobes and a point
        return imin(imin(circle_sd(x, y, -12 * 16, -8 * 16, 14 * 16), circle_sd(x, y, 12 * 16, -8 * 16, 14 * 16)),
                    tri_sd(x, y, 25 * 16, -2 * 16, 28 * 16));
    case 0:   // spade: the heart upside down, plus a stem
        return imin(imin(imin(circle_sd(x, y, -12 * 16, 6 * 16, 14 * 16), circle_sd(x, y, 12 * 16, 6 * 16, 14 * 16)),
                         tri_sd(x, y, 25 * 16, 2 * 16, -28 * 16)),
                    tri_sd(x, y, 8 * 16, 30 * 16, 8 * 16));
    default:  // club: three balls and a stem
        return imin(imin(circle_sd(x, y, 0, -12 * 16, 11 * 16), circle_sd(x, y, -12 * 16, 6 * 16, 11 * 16)),
                    imin(circle_sd(x, y, 12 * 16, 6 * 16, 11 * 16), tri_sd(x, y, 8 * 16, 30 * 16, 4 * 16)));
    }
}

// Coverage of a suit of height size px centred at (cx, cy) Q4, at point (px, py) Q4.
static int suit_cov(int suit, int px, int py, int cx, int cy, int size, bool flip)
{
    int ux = (px - cx) * 64 / size, uy = (py - cy) * 64 / size;
    if (flip) { ux = -ux; uy = -uy; }
    if (iabs_(ux) > 40 * 16 || iabs_(uy) > 40 * 16) return 0;
    return cov_from_sd(suit_sd(suit, ux, uy) * size / 64);
}

// ---------- cards ----------

static const uint8_t PIPS_X[11][10] = {
    {0}, {0}, {2, 2}, {2, 2, 2}, {0, 4, 0, 4}, {0, 4, 2, 0, 4}, {0, 4, 0, 4, 0, 4}, {0, 4, 2, 0, 4, 0, 4},
    {0, 4, 2, 0, 4, 2, 0, 4}, {0, 4, 0, 4, 2, 0, 4, 0, 4}, {0, 4, 2, 0, 4, 0, 4, 2, 0, 4}};
static const uint8_t PIPS_Y[11][10] = {   // sixths of the pip area
    {0}, {0}, {0, 6}, {0, 3, 6}, {0, 0, 6, 6}, {0, 0, 3, 6, 6}, {0, 0, 3, 3, 6, 6}, {0, 0, 1, 3, 3, 6, 6},
    {0, 0, 1, 3, 3, 5, 6, 6}, {0, 0, 2, 2, 3, 4, 4, 6, 6}, {0, 0, 1, 2, 2, 4, 4, 5, 6, 6}};

// Glyph coverage of laid-out text t, drawn with its baseline centre at (bx, by) px, at pixel (lx, ly).
static int text_cov(const ntg_t *g, const nt_text_t *t, int bx, int by, int lx, int ly)
{
    for (int i = t->first; i < t->first + t->count; i++) {
        const nt_glyph_t *gl = &g->glyphs.g[i];
        int gx = lx - bx - gl->x, gy = ly - by - gl->y;
        if (gx < 0 || gy < 0 || gx >= gl->w || gy >= gl->h) continue;
        unsigned bit = (unsigned)(gy * gl->w + gx) * 4;
        return ((gl->bitmap[bit >> 3] >> ((bit & 4) ? 0 : 4)) & 15) * 256 / 15;
    }
    return 0;
}

static uint16_t card_face_px(const ntg_t *g, const ntg_op_t *op, int lx, int ly, int LX, int LY)
{
    int w = op->u.card.w, h = op->u.card.h, rank = op->u.card.rank, suit = op->u.card.suit;
    bool red = suit & 1, big = w >= 40;
    uint16_t ink = red ? C565(210, 58, 58) : C565(20, 16, 14);
    uint16_t c = lerp565(255, 253, 247, 236, 229, 212, ly * 256 / (h > 1 ? h - 1 : 1));
    const nt_text_t *idx = &g->card_text[op->u.card.text][0];
    // Corner index: rank over a small suit, top-left and (on big cards) mirrored bottom-right.
    for (int corner = 0; corner < (big ? 2 : 1); corner++) {
        int qx = corner ? w - 1 - lx : lx, qy = corner ? h - 1 - ly : ly;
        int QX = corner ? (w * 16 - 1) - LX : LX, QY = corner ? (h * 16 - 1) - LY : LY;
        int a = text_cov(g, idx, w * 16 / 100, w * 30 / 100 + 1, qx, qy);
        if (a) return nt_blend565(c, ink, a);
        int s = suit_cov(suit, QX, QY, w * 16 * 16 / 100, w * 16 * 46 / 100, w * 22 / 100, false);
        if (s) return nt_blend565(c, ink, s);
    }
    if (rank >= 11 && big) {   // court card: tinted panel, brass frame, big letter and suit
        int px = w * 28 / 100, py = h * 20 / 100, pw = w * 56 / 100, ph = h * 60 / 100;
        int sd = rbox_sd(LX, LY, (px * 2 + pw) * 8, (py * 2 + ph) * 8, pw * 8, ph * 8, w * 6 / 100 * 16);
        int in = cov_from_sd(sd), ring = in - cov_from_sd(sd + 16);
        if (in) {
            uint16_t pc = red ? lerp565(246, 217, 210, 235, 193, 182, (ly - py) * 256 / ph)
                              : lerp565(220, 227, 234, 197, 208, 220, (ly - py) * 256 / ph);
            c = nt_blend565(c, pc, in);
            if (ring > 0) c = nt_blend565(c, C565(201, 162, 74), ring);
            const nt_text_t *letter = &g->card_text[op->u.card.text][1];
            int a = letter->count ? text_cov(g, letter, px + pw / 2, py + ph * 58 / 100, lx, ly) : 0;
            if (a) return nt_blend565(c, ink, a);
            int s = suit_cov(suit, LX, LY, (px + pw / 2) * 16, (py + ph * 80 / 100) * 16, pw * 30 / 100, false);
            if (s) return nt_blend565(c, ink, s);
        }
        return c;
    }
    if (rank == 1 || rank >= 11 || !big) {   // ace, or any card too small for pips
        int s = suit_cov(suit, LX, LY, w * 16 * 56 / 100, h * 16 * 60 / 100, w * (rank == 1 ? 56 : 46) / 100, false);
        return s ? nt_blend565(c, ink, s) : c;
    }
    int ix = w * 34 / 100, iy = h * 20 / 100, iw = w * 40 / 100, ih = h * 60 / 100, ps = w * 19 / 100;
    for (int i = 0; i < rank; i++) {
        int pcx = (ix * 4 + iw * PIPS_X[rank][i]) * 4, pcy = (iy * 6 + ih * PIPS_Y[rank][i]) * 16 / 6;
        int s = suit_cov(suit, LX, LY, pcx, pcy, ps, PIPS_Y[rank][i] > 3);
        if (s) return nt_blend565(c, ink, s);
    }
    return c;
}

static uint16_t card_back_px(const ntg_op_t *op, int lx, int ly, int LX, int LY)
{
    int w = op->u.card.w, h = op->u.card.h;
    uint16_t c = lerp565(138, 50, 72, 86, 27, 44, (lx + ly) * 256 / (w + h));
    int m = imax(2, w / 10);
    int sd = rbox_sd(LX, LY, w * 8, h * 8, (w / 2 - m) * 16, (h / 2 - m) * 16, imax(1, w / 16) * 16);
    int in = cov_from_sd(sd), ring = in - cov_from_sd(sd + 16);
    int step = imax(4, w * 16 / 100);
    if (in > 128 && ((lx + ly) % step == 0 || (lx - ly + 4 * step) % step == 0))
        c = nt_blend565(c, C565(234, 196, 110), 72);   // lattice
    if (ring > 0) c = nt_blend565(c, C565(201, 162, 74), ring);
    if (w >= 22) {   // brass medallion
        int r = w * 24 / 100 * 16, d = (int)isqrt_u((uint32_t)((LX - w * 8) * (LX - w * 8) + (LY - h * 8) * (LY - h * 8)));
        int a = cov_from_sd(d - r);
        if (a) c = nt_blend565(c, lerp565(241, 213, 138, 164, 123, 44, clampi((LX - w * 8 + LY - h * 8 + r) * 128 / r, 0, 256)), a);
        int dot = cov_from_sd(d - r / 3) - 0;
        if (dot) c = nt_blend565(c, C565(86, 27, 44), dot);
    }
    return c;
}

void ntg_paint_card(const ntg_t *g, const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    int w = op->u.card.w, h = op->u.card.h, sq = op->u.card.squash;
    int r = imax(2, w / 10) * 16;
    int ly16 = (y - op->u.card.y) * 16 + 8;
    int cx16 = op->u.card.x * 16 + w * 8;
    for (int x = imax(x0, op->x0); x < imin(x1, op->x1); x++) {
        uint16_t *p = &row[x - x0];
        // Soft shadow, offset down-right, under the whole card.
        int ssd = rbox_sd(x * 16 + 8, ly16, cx16 + 16, h * 8 + 24, (w * sq / 64) * 8, h * 8, r);
        int sh = clampi(24 - ssd, 0, 48) * 2;
        if (sh) *p = nt_blend565(*p, 0, sh);
        if (!sq) continue;
        int LX = ((x * 16 + 8) - cx16) * 64 / sq + w * 8, LY = ly16;   // card-local Q4 after the flip squash
        int sd = rbox_sd(LX, LY, w * 8, h * 8, w * 8, h * 8, r);
        int cov = cov_from_sd(sd * sq / 64);
        if (!cov) continue;
        int lx = LX >> 4, ly = LY >> 4;
        uint16_t c = op->u.card.face ? card_face_px(g, op, lx, ly, LX, LY) : card_back_px(op, lx, ly, LX, LY);
        int edge = cov - cov_from_sd(sd * sq / 64 + 18);   // dark outline
        if (edge > 0) c = nt_blend565(c, C565(20, 16, 14), edge * 3 / 4);
        *p = nt_blend565(*p, c, cov);
    }
}

// ---------- dice cup (front view, scale 1.3, as in the prototype) ----------

void ntg_paint_cup(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    const int S = 13;   // tenths
    int cx = op->u.cup.cx * 16 + 8, by = op->u.cup.by, py = y * 16 + 8, alpha = op->u.cup.alpha;
    int body_top = by * 16 - 30 * S * 16 / 10, dome_top = by * 16 - 36 * S * 16 / 10, band_top = by * 16 - 6 * S * 16 / 10;
    for (int x = imax(x0, op->x0); x < imin(x1, op->x1); x++) {
        int px = x * 16 + 8, dx = px - cx;
        uint16_t c = 0;
        int cov = 0;
        if (py >= band_top && py < by * 16) {   // brass band
            int sd = rbox_sd(px, py, cx, (band_top + by * 16) / 2, 19 * S * 16 / 10, (by * 16 - band_top) / 2, 2 * 16);
            cov = cov_from_sd(sd);
            c = lerp565(233, 200, 115, 155, 117, 36, (py - band_top) * 256 / (by * 16 - band_top));
            int ring = cov - cov_from_sd(sd + 20);
            if (ring > 0) c = nt_blend565(c, C565(20, 16, 14), ring);
        } else if (py >= dome_top && py < band_top) {
            int half;
            if (py >= body_top) {
                half = 15 * S * 16 / 10 + 3 * S * 16 / 10 * (py - body_top) / (band_top - body_top);
            } else {   // rounded top
                int k = (body_top - py) * 256 / (body_top - dome_top);
                half = 15 * S * 16 / 10 * (int)isqrt_u((uint32_t)(65536 - k * k)) / 256;
            }
            cov = clampi(half - iabs_(dx) + 8, 0, 16) * 16;
            int u = clampi((dx + 18 * S * 16 / 10) * 256 / (36 * S * 16 / 10), 0, 256);
            c = u < 90 ? lerp565(62, 26, 44, 122, 51, 82, u * 256 / 90)
                       : u < 154 ? lerp565(122, 51, 82, 92, 36, 64, (u - 90) * 4) : lerp565(92, 36, 64, 46, 18, 32, (u - 154) * 256 / 102);
            int edge = cov - clampi(half - iabs_(dx) - 16, 0, 16) * 16;
            if (edge > 0) c = nt_blend565(c, C565(20, 16, 14), edge);
            if (dx > -10 * S * 16 / 10 && dx < -6 * S * 16 / 10 && py > body_top + 16 && py < band_top - 48)
                c = nt_blend565(c, C565(255, 220, 240), 46);   // highlight
        }
        if (cov) row[x - x0] = nt_blend565(row[x - x0], c, cov * alpha / 255);
    }
}

// ---------- NABO DUEL backdrop ----------
// Flat bands only (no fine gradients: they band on RGB565), as in game-art/duel/prototype.

#define DUSK_HORIZON 192
#define DUSK_STREET 262

void ntg_paint_dusk(const ntg_op_t *op, int y, int x0, int x1, uint16_t *row)
{
    (void)op;
    static const uint16_t SKY[4] = {C565(42, 24, 56), C565(90, 42, 62), C565(168, 68, 60), C565(224, 122, 58)};
    static const int16_t TOWN[][4] = {{0, 168, 34, 24}, {30, 160, 26, 32}, {56, 174, 20, 18}, {150, 164, 30, 28},
                                      {182, 154, 18, 38}, {200, 170, 40, 22}, {186, 146, 2, 8}, {40, 152, 6, 8}};
    const uint16_t town = C565(30, 18, 24), sun = C565(255, 196, 107);
    if (y < DUSK_HORIZON) {
        uint16_t sky = SKY[y / 48];
        int dy = DUSK_HORIZON - y, sun_w2 = 26 * 26 - dy * dy;   // sun: half disc sitting on the horizon
        for (int x = x0; x < x1; x++) {
            uint16_t c = sky;
            int dx = x - 176;
            if (sun_w2 >= 0 && dx * dx <= sun_w2) c = sun;
            for (size_t k = 0; k < sizeof(TOWN) / sizeof(TOWN[0]); k++)
                if (x >= TOWN[k][0] && x < TOWN[k][0] + TOWN[k][2] && y >= TOWN[k][1] && y < TOWN[k][1] + TOWN[k][3]) {
                    c = town;
                    break;
                }
            row[x - x0] = c;
        }
        return;
    }
    uint16_t ground = y < DUSK_STREET ? C565(122, 74, 44) : C565(92, 54, 32), dust = C565(154, 106, 64);
    for (int x = x0; x < x1; x++) {
        // A few dust specks in the road, 6 x 2 px, fixed.
        bool speck = false;
        for (int i = 0; i < 9 && !speck; i++) {
            int sx = (i * 53 + 17) % 240, sy = 200 + (i * 29) % 56;
            speck = x >= sx && x < sx + 6 && y >= sy && y < sy + 2;
        }
        row[x - x0] = speck ? dust : ground;
    }
}
