// NABO DUEL screens: a port of render() in game-art/duel/prototype/index.html, laid out for the
// badge fonts. Times are the model's (u->ticked), so what is drawn matches the rules exactly.
// The draw itself is a panel inversion done by nabo_table.c, not drawn here.
#include <stdio.h>
#include <string.h>

#include "nabo_duel_sprite_ids.h"
#include "nabo_table_scene_duel.h"
#include "nabo_table_scene_ui.h"

#define WAIST 262              // NABO's waist line sits on the street edge
#define COL_FG ntg_rgb(246, 234, 216)
#define COL_MUTED ntg_rgb(196, 168, 148)
#define COL_INK ntg_rgb(18, 11, 13)
#define COL_BRASS ntg_rgb(216, 178, 90)
#define COL_YELLOW ntg_rgb(255, 210, 63)
#define COL_BAD ntg_rgb(226, 87, 76)
#define COL_WATER ntg_rgb(127, 208, 255)
#define COL_HOST ntg_rgb(240, 138, 60)
#define COL_GUEST ntg_rgb(69, 180, 240)
#define SHADE 184              // alpha of the dark veil behind menus

static const char *const AI_NAME[3] = {"新手", "老手", "神枪"};
static const char *const BO_NAME[6] = {"", "一局定胜负", "", "三局两胜", "", "五局三胜"};

static inline int32_t since(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

static void txt(ntg_t *g, const lv_font_t *f, const char *s, int x, int y, nt_align_t a, uint16_t c)
{
    nt_txt(g, f, s, x, y, a, c);   // y is the vertical middle
}

static void veil(ntg_t *g, int alpha) { ntg_rect(g, 0, 0, NTG_W, NTG_H, 0, COL_INK, (uint8_t)alpha, 0, 0); }

static void pill(ntg_t *g, const nt_fonts_t *f, const char *s, int y, uint16_t color)
{
    int w = ntg_text_width(f->f12, s) + 24;
    ntg_rect(g, (NTG_W - w) / 2, y - 12, w, 24, 12, COL_INK, 200, 0, 0);
    txt(g, f->f12, s, NTG_W / 2, y, NT_ALIGN_CENTER, color);
}

static void you_colors(const nd_t *m, uint16_t *mine, uint16_t *theirs)
{
    bool host = !m->has_match || m->match.solo || m->match.host;
    *mine = host ? COL_HOST : COL_GUEST;
    *theirs = host ? COL_GUEST : COL_HOST;
}

// Frame of a sprite animation started at t0 (loops or holds the last frame).
static void nabo(ntg_t *g, int anim, uint32_t t, uint32_t t0, int dx, uint16_t ring)
{
    nts_anim_t a;
    if (!nts_anim(g->sprite.pack, anim, &a) || !a.frames) return;
    int32_t ms = since(t, t0);
    int f = ms > 0 ? (int)((int64_t)ms * a.fps / 1000) : 0;
    f = a.loop ? f % a.frames : (f < a.frames ? f : a.frames - 1);
    if (ring) ntg_rect(g, NTG_W / 2 - 46, WAIST - 6, 92, 14, 7, ring, 140, 0, 0);   // the opponent's colour
    ntg_sprite(g, anim, f, NTG_W / 2 + dx, WAIST);
}

static uint32_t anim_ms(const ntg_t *g, int anim)
{
    nts_anim_t a;
    return nts_anim(g->sprite.pack, anim, &a) && a.fps ? (uint32_t)a.frames * 1000u / a.fps : 0;
}

static void menu_rows(ntg_t *g, const nt_fonts_t *f, int y0, int lh, int n, const char *const *labels,
                      const char *const *values, int sel)
{
    for (int i = 0; i < n; i++) {
        int y = y0 + i * lh;
        bool on = i == sel;
        if (on) ntg_rect(g, 20, y - lh / 2 + 2, 200, lh - 4, 8, COL_BRASS, 60, COL_BRASS, 1);
        txt(g, f->f16, labels[i], 34, y, NT_ALIGN_LEFT, on ? COL_FG : COL_MUTED);
        if (values && values[i] && values[i][0])
            txt(g, f->f16, values[i], 206, y, NT_ALIGN_RIGHT, on ? COL_BRASS : COL_MUTED);
    }
}

static void fmt_res(char *out, size_t cap, nd_res_t r, nd_res_t other)
{
    if (r.k == ND_RES_HIT) snprintf(out, cap, "%u.%03u s", r.r / 1000, r.r % 1000);
    else if (r.k == ND_RES_FOUL) snprintf(out, cap, "抢跑");
    else snprintf(out, cap, other.k == ND_RES_HIT ? "慢了" : "没按");   // locked out by the faster shot
}

// ---------- screens ----------

static void scene_cover(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f)
{
    const nd_t *m = &u->m;
    char best[32], bo[16];
    nabo(g, NDS_STANCE, u->ticked, 0, 0, 0);
    ntg_rect(g, 12, 18, 216, 290, 14, COL_INK, SHADE, 0, 0);
    txt(g, f->poster, "DUEL", NTG_W / 2, 46, NT_ALIGN_CENTER, COL_BRASS);
    txt(g, f->f18, "对 决", NTG_W / 2, 76, NT_ALIGN_CENTER, COL_FG);
    if (m->best) snprintf(best, sizeof(best), "个人最快 %u.%03u s", m->best / 1000, m->best % 1000);
    else snprintf(best, sizeof(best), "屏幕反色时按 OK");
    txt(g, f->f12, best, NTG_W / 2, 98, NT_ALIGN_CENTER, COL_MUTED);
    snprintf(bo, sizeof(bo), "%s", BO_NAME[m->set.bo <= 5 ? m->set.bo : 3]);
    static const char *const LABEL[ND_COVER_COUNT] = {"联机对战", "单人练习", "局数", "虚晃", "练习对手", "返回牌桌"};
    const char *value[ND_COVER_COUNT] = {"", "", bo, m->set.fake ? "开" : "关", AI_NAME[m->set.ai % 3], ""};
    menu_rows(g, f, 132, 28, ND_COVER_COUNT, LABEL, value, m->sel);
    nt_hint(g, f, "↑↓ 选择 · OK 确定");
}

static void bars(ntg_t *g, int x, int y, int rssi, uint16_t c)
{
    int n = (rssi + 90) / 12;
    n = n < 1 ? 1 : n > 4 ? 4 : n;
    for (int i = 0; i < 4; i++)
        ntg_rect(g, x + i * 5, y + 8 - 3 * (i + 1), 3, 3 * (i + 1), 0, i < n ? c : COL_MUTED, i < n ? 255 : 90, 0, 0);
}

static void scene_lobby(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f)
{
    const nd_t *m = &u->m;
    veil(g, SHADE);
    // Radar: three rings, a sweep of dots, one blip per badge heard.
    const int cx = NTG_W / 2, cy = 70;
    for (int r = 18; r <= 50; r += 16) ntg_rect(g, cx - r, cy - r, 2 * r, 2 * r, r, 0, 0, COL_BRASS, 1);
    int ang = (int)(u->ticked / 4 % 360);
    static const int16_t COS[8] = {256, 181, 0, -181, -256, -181, 0, 181};   // 45 degree steps
    int k = ang / 45, k2 = (k + 2) % 8;
    for (int i = 1; i <= 4; i++) {
        int px = cx + COS[k] * 12 * i / 256, py = cy + COS[k2] * 12 * i / 256;
        ntg_rect(g, px - 2, py - 2, 4, 4, 2, COL_BRASS, 255, 0, 0);
    }
    for (int i = 0; i < m->peer_n; i++) {
        const nd_peer_t *p = &m->peers[i];
        int a = (p->mac[5] * 7 + p->mac[4]) % 8, d = (-p->rssi - 30) * 6 / 5;
        d = d < 10 ? 10 : d > 46 ? 46 : d;
        int px = cx + COS[a] * d / 256, py = cy + COS[(a + 2) % 8] * d / 256;
        ntg_rect(g, px - 3, py - 3, 7, 7, 3, p->busy ? COL_MUTED : COL_YELLOW, 255, 0, 0);
    }
    ntg_rect(g, cx - 3, cy - 3, 7, 7, 3, COL_HOST, 255, 0, 0);
    txt(g, f->f18, "找对手", NTG_W / 2, 140, NT_ALIGN_CENTER, COL_FG);
    const nd_peer_t *rows[ND_LIST_PEERS];
    int n = nd_lobby_list(m, rows);
    const char *label[ND_LIST_PEERS + 1], *value[ND_LIST_PEERS + 1];
    for (int i = 0; i < n; i++) {
        label[i] = rows[i]->name;
        value[i] = rows[i]->ver != ND_VER ? "版本不同" : rows[i]->busy ? "对决中" : "";
    }
    label[n] = "返回", value[n] = "";
    int y0 = n ? 176 : 214;
    if (!n) txt(g, f->f12, "附近还没有 NABO……", NTG_W / 2, 180, NT_ALIGN_CENTER, COL_MUTED);
    menu_rows(g, f, y0, 28, n + 1, label, value, m->sel);
    for (int i = 0; i < n; i++)
        if (rows[i]->ver == ND_VER && !rows[i]->busy) bars(g, 190, y0 + i * 28 - 4, rows[i]->rssi, COL_YELLOW);
    char me[32];
    snprintf(me, sizeof(me), "%s · OK 邀请", m->name);
    txt(g, f->f12, me, NTG_W / 2, 306, NT_ALIGN_CENTER, COL_MUTED);
    if (m->scene == ND_INVITING || m->scene == ND_INVITED) {
        char s[48];
        veil(g, 220);
        ntg_rect(g, 18, 92, 204, 150, 14, ntg_rgb(42, 28, 28), 255, COL_BRASS, 1);
        if (m->scene == ND_INVITING) {
            txt(g, f->f18, "等待对方接受", NTG_W / 2, 126, NT_ALIGN_CENTER, COL_FG);
            txt(g, f->mono, m->invite.name, NTG_W / 2, 154, NT_ALIGN_CENTER, COL_BRASS);
            static const char *const DOTS[3] = {".", "..", "..."};
            txt(g, f->f18, DOTS[u->ticked / 400 % 3], NTG_W / 2, 182, NT_ALIGN_CENTER, COL_MUTED);
            txt(g, f->f12, "UP 取消", NTG_W / 2, 222, NT_ALIGN_CENTER, COL_MUTED);
        } else {
            txt(g, f->mono, m->invited.name, NTG_W / 2, 122, NT_ALIGN_CENTER, COL_YELLOW);
            txt(g, f->f24, "邀你对决", NTG_W / 2, 152, NT_ALIGN_CENTER, COL_FG);
            snprintf(s, sizeof(s), "%s · 虚晃%s", BO_NAME[m->invited.bo <= 5 ? m->invited.bo : 3], m->invited.fake ? "开" : "关");
            txt(g, f->f12, s, NTG_W / 2, 182, NT_ALIGN_CENTER, COL_MUTED);
            txt(g, f->f16, "OK 接受    UP 拒绝", NTG_W / 2, 220, NT_ALIGN_CENTER, COL_FG);
        }
    }
}

static void score_bar(ntg_t *g, const nd_t *m, const nt_fonts_t *f)
{
    uint16_t mc, oc;
    char s[8];
    you_colors(m, &mc, &oc);
    ntg_rect(g, 0, 0, NTG_W, 30, 0, COL_INK, SHADE, 0, 0);
    ntg_rect(g, 9, 10, 10, 10, 5, mc, 255, 0, 0);
    txt(g, f->f16, "你", 26, 15, NT_ALIGN_LEFT, COL_FG);
    snprintf(s, sizeof(s), "%d", m->match.me);
    txt(g, f->poster, s, 48, 16, NT_ALIGN_LEFT, COL_FG);
    ntg_rect(g, NTG_W - 19, 10, 10, 10, 5, oc, 255, 0, 0);
    txt(g, f->mono, m->match.opp_name, NTG_W - 26, 15, NT_ALIGN_RIGHT, COL_MUTED);
    snprintf(s, sizeof(s), "%d", m->match.opp);
    txt(g, f->poster, s, NTG_W - 34 - ntg_text_width(f->mono, m->match.opp_name), 16, NT_ALIGN_RIGHT, COL_FG);
}

static void reveal(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f, const nd_attempt_t *c)
{
    const nd_t *m = &u->m;
    uint32_t t = u->ticked, t0 = c->resolved_at;
    uint16_t mc, oc;
    you_colors(m, &mc, &oc);
    nd_outcome_t out = (nd_outcome_t)c->outcome;
    // You won: the opponent gets soaked. You lost: they draw, then gloat. You jumped: they laugh.
    int anim = NDS_STANCE;
    uint32_t start = t0;
    if (out == ND_OUT_WIN) anim = NDS_SPLASHED;
    else if (out == ND_OUT_LOSE && c->me.k == ND_RES_FOUL) anim = NDS_LAUGH;
    else if (out == ND_OUT_LOSE) {
        uint32_t d = anim_ms(g, NDS_DRAW);
        if (since(t, t0 + d) < 0) anim = NDS_DRAW;
        else anim = NDS_SMUG, start = t0 + d;
    } else if (c->me.k == ND_RES_NONE && c->opp.k == ND_RES_NONE) anim = NDS_SLEEPY;
    else if (c->me.k == ND_RES_FOUL) anim = NDS_LAUGH;
    nabo(g, anim, t, start, 0, oc);
    int32_t el = since(t, t0);
    if (out == ND_OUT_WIN) {
        static const int16_t DX[8] = {-58, 50, -40, 62, -66, 34, -20, 70}, DY[8] = {-60, -40, -96, -84, -20, -110, -126, -14};
        int grow = el > 0 ? (el > 600 ? 20 : el / 30) : 0;
        for (int i = 0; i < 8; i++) {
            int r = 3 + i % 3, x = NTG_W / 2 + DX[i] * (40 + grow) / 40, y = 200 + DY[i];
            ntg_rect(g, x - r, y - r, 2 * r, 2 * r, r, COL_WATER, 220, 0, 0);
        }
    } else if (out == ND_OUT_LOSE && c->me.k != ND_RES_FOUL && since(t, t0 + anim_ms(g, NDS_DRAW) / 2) >= 0) {
        int drip = el > 0 ? (el > 1500 ? 60 : el / 25) : 0;   // your screen got splashed
        for (int i = 0; i < 6; i++) {
            int x = (i * 71 + 23) % 200 + 20, y = (i * 47) % 180 + 40 + drip * (i % 3 + 1) / 3, w = 14 + i % 4 * 6;
            ntg_rect(g, x - w / 2, y - w * 3 / 4, w, w * 3 / 2, w / 2, COL_WATER, 110, 0, 0);
        }
    }
    const char *title = out == ND_OUT_WIN ? "你赢了这局"
                      : out == ND_OUT_LOSE ? (c->me.k == ND_RES_FOUL ? "你抢跑了" : "慢了一步")
                      : c->me.k == ND_RES_FOUL ? "都太急了"
                      : c->me.k == ND_RES_NONE ? "都睡着了？" : "一模一样！";
    int tw = ntg_text_width(f->f24, title) + 28;
    ntg_rect(g, (NTG_W - tw) / 2, 78, tw, 36, 18, COL_INK, 150, 0, 0);
    txt(g, f->f24, title, NTG_W / 2, 96, NT_ALIGN_CENTER, out == ND_OUT_WIN ? COL_YELLOW : out == ND_OUT_LOSE ? COL_FG : COL_BRASS);
    char a[24], b[32], s[40];
    fmt_res(a, sizeof(a), c->me, c->opp);
    fmt_res(b, sizeof(b), c->opp, c->me);
    ntg_rect(g, 16, 280, 208, 34, 8, COL_INK, 200, 0, 0);
    ntg_rect(g, 24, 290, 4, 14, 0, mc, 255, 0, 0);
    txt(g, f->f12, "你", 34, 297, NT_ALIGN_LEFT, COL_MUTED);
    txt(g, c->me.k == ND_RES_HIT ? f->mono : f->f12, a, 34 + ntg_text_width(f->f12, "你") + 6, 297, NT_ALIGN_LEFT, COL_FG);
    ntg_rect(g, 212, 290, 4, 14, 0, oc, 255, 0, 0);
    txt(g, c->opp.k == ND_RES_HIT ? f->mono : f->f12, b, 206 - 30, 297, NT_ALIGN_RIGHT, COL_MUTED);
    txt(g, f->f12, "对手", 206, 297, NT_ALIGN_RIGHT, COL_MUTED);
    if (c->new_best) {
        snprintf(s, sizeof(s), "新纪录 %u.%03u s", c->me.r / 1000, c->me.r % 1000);
        pill(g, f, s, 52, COL_YELLOW);
    } else if (out == ND_OUT_REPLAY) {
        snprintf(s, sizeof(s), "%s", m->match.attempt > 1 ? "重来一次" : "本局不计分");
        pill(g, f, s, 52, COL_BRASS);
    }
}

static void scene_duel(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f, const lv_font_t *big)
{
    const nd_t *m = &u->m;
    const nd_match_t *mt = &m->match;
    const nd_attempt_t *c = &m->cur;
    uint32_t t = u->ticked;
    uint16_t mc, oc;
    you_colors(m, &mc, &oc);
    if (m->scene == ND_END) {
        bool won = mt->me > mt->opp;
        char s[16];
        nabo(g, won ? NDS_SPLASHED : NDS_SMUG, t, m->scene_at, 0, oc);
        veil(g, SHADE);
        txt(g, f->f24, won ? "你赢下了这场" : "对手赢了", NTG_W / 2, 70, NT_ALIGN_CENTER, won ? COL_YELLOW : COL_FG);
        snprintf(s, sizeof(s), "%d : %d", mt->me, mt->opp);
        txt(g, f->poster, s, NTG_W / 2, 124, NT_ALIGN_CENTER, COL_BRASS);
        if (mt->want_next) txt(g, f->f16, "等对手决定……", NTG_W / 2, 200, NT_ALIGN_CENTER, COL_MUTED);
        else {
            static const char *const L1[2] = {"再来一场", "回大厅"}, *const L2[2] = {"再来一场", "回封面"};
            menu_rows(g, f, 196, 34, 2, mt->solo ? L2 : L1, NULL, m->sel);
        }
        if (mt->opp_next && !mt->want_next) pill(g, f, "对手想再来一场", 290, COL_YELLOW);
        return;
    }
    nd_phase_t ph = m->scene == ND_DUEL ? nd_phase(m, t) : ND_PH_WAIT;
    if (ph == ND_PH_WAIT) {
        if (mt->has_last) reveal(g, u, f, &mt->last);
        else {
            nabo(g, NDS_STANCE, t, 0, 0, oc);
            pill(g, f, mt->solo ? "准备开始" : "对时中…", 296, COL_MUTED);
        }
    } else if (ph == ND_PH_REVEAL) {
        reveal(g, u, f, c);
    } else {
        int anim = NDS_STANCE, dx = 0;
        uint32_t start = 0;
        if (ph == ND_PH_READY && since(t, c->ready + anim_ms(g, NDS_ENTER)) < 0) anim = NDS_ENTER, start = c->ready;
        if (c->has_opp && c->opp.k == ND_RES_HIT) anim = NDS_DRAW, start = c->go_shown + c->opp.r;
        const nd_fake_t *fake = NULL;
        for (int i = 0; i < c->fake_n; i++)
            if (since(t, c->fakes[i].at) >= 0 && since(t, c->fakes[i].at + ND_T_FAKE_SHOW) < 0) fake = &c->fakes[i];
        if (fake && fake->kind == ND_FAKE_TWITCH && anim == NDS_STANCE) anim = NDS_TWITCH, start = fake->at;   // the flinch
        nabo(g, anim, t, start, dx, oc);
        if (ph == ND_PH_READY) {
            char s[32];
            txt(g, f->f24, "准备……", NTG_W / 2, 96, NT_ALIGN_CENTER, COL_FG);
            snprintf(s, sizeof(s), "第 %d 局 · %s", c->round, BO_NAME[mt->bo <= 5 ? mt->bo : 3]);
            pill(g, f, s, 296, COL_MUTED);
        } else if (ph == ND_PH_STANDOFF) {
            txt(g, f->f18, "· · ·", NTG_W / 2, 96, NT_ALIGN_CENTER, COL_MUTED);
            if (fake && fake->kind == ND_FAKE_TEXT) txt(g, big, "拔？", NTG_W / 2, 100, NT_ALIGN_CENTER, COL_YELLOW);
            if (fake && fake->kind == ND_FAKE_TUMBLE) {   // tumbleweed rolling across the street
                int p = (int)since(t, fake->at) * 256 / ND_T_FAKE_SHOW, x = -20 + p * 280 / 256;
                int bounce = (p * 9 / 32) % 8, y = 276 - (bounce < 4 ? bounce : 8 - bounce) * 4;
                ntg_rect(g, x - 11, y - 11, 22, 22, 11, 0, 0, ntg_rgb(201, 154, 90), 2);
                ntg_rect(g, x - 6, y - 6, 12, 12, 6, 0, 0, ntg_rgb(201, 154, 90), 1);
            }
        } else {   // the draw: the panel is inverted, white text reads dark
            txt(g, big, "拔！", NTG_W / 2, 104, NT_ALIGN_CENTER, COL_FG);
        }
        if (c->has_me) {
            char s[40], r[24];
            if (c->me.k == ND_RES_FOUL) {
                ntg_rect(g, 0, 30, NTG_W, NTG_H - 30, 0, COL_BAD, 90, 0, 0);
                txt(g, big, "抢跑！", NTG_W / 2, 100, NT_ALIGN_CENTER, COL_BAD);
            }
            fmt_res(r, sizeof(r), c->me, c->opp);
            snprintf(s, sizeof(s), c->me.k == ND_RES_HIT ? "你 %s · 等对手…" : "%s · 等对手…", r);
            pill(g, f, s, 304, COL_FG);
        }
    }
    score_bar(g, m, f);
    if (m->scene == ND_PAUSE || m->scene == ND_OPPPAUSE) {
        veil(g, 220);
        if (m->scene == ND_PAUSE) {
            static const char *const L[2] = {"继续", "认输退出"};
            txt(g, f->f24, "暂停", NTG_W / 2, 130, NT_ALIGN_CENTER, COL_FG);
            menu_rows(g, f, 182, 34, 2, L, NULL, m->sel);
        } else {
            txt(g, f->f24, "对手暂停了", NTG_W / 2, 150, NT_ALIGN_CENTER, COL_FG);
            txt(g, f->f12, "等对方回来……", NTG_W / 2, 182, NT_ALIGN_CENTER, COL_MUTED);
        }
    }
}

static void scene_left(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f)
{
    const nd_t *m = &u->m;
    veil(g, SHADE);
    const char *why = m->left_why == ND_LEFT_QUIT ? "对手认输了" : m->left_why == ND_LEFT_BYE ? "对手离开了" : "信号断了";
    txt(g, f->f24, why, NTG_W / 2, 150, NT_ALIGN_CENTER, COL_FG);
    txt(g, f->f12, "回到大厅……", NTG_W / 2, 184, NT_ALIGN_CENTER, COL_MUTED);
}

static const char *const TOAST[] = {"", "对方固件版本不同", "对方正在对决", "没有回应", "对方拒绝了", "对方正忙", "对方取消了邀请"};

void nt_scene_duel(ntg_t *g, const nabo_duel_t *u, const nt_fonts_t *f, const lv_font_t *big)
{
    const nd_t *m = &u->m;
    // Scene ids 100+ keep the duel apart from the table screens (a change redraws everything).
    ntg_begin(g, 100 + m->scene);
    ntg_dusk(g);
    switch (m->scene) {
    case ND_COVER: scene_cover(g, u, f); break;
    case ND_LOBBY: case ND_INVITING: case ND_INVITED: scene_lobby(g, u, f); break;
    case ND_SYNC: case ND_DUEL: case ND_PAUSE: case ND_OPPPAUSE: case ND_END: scene_duel(g, u, f, big); break;
    case ND_LEFT: scene_left(g, u, f); break;
    default: break;
    }
    if (m->toast != ND_TOAST_NONE && m->toast < sizeof(TOAST) / sizeof(TOAST[0])) pill(g, f, TOAST[m->toast], 300, COL_YELLOW);
}
