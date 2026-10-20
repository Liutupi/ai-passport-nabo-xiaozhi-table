#include "nabo_duel.h"

#include <string.h>

static inline int32_t since(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

void ndu_init(nabo_duel_t *u, const uint8_t mac[ND_MAC_LEN], uint32_t entropy, const nd_settings_t *set,
              uint16_t best, uint32_t now)
{
    memset(u, 0, sizeof(*u));
    u->ticked = now - NDU_LAG_MS;
    nd_init(&u->m, mac, entropy, set, best, false, u->ticked);
}

static void push(nabo_duel_t *u, const ndu_input_t *in)
{
    if (u->in_n >= NDU_INPUTS) { u->in_dropped = true; return; }
    ndu_input_t e = *in;
    // Too late to go in at its own time (the model already passed it): the next tick takes it.
    if (since(e.t, u->ticked) <= 0) e.t = u->ticked + 1;
    int j = u->in_n++;
    while (j > 0 && since(u->in[j - 1].t, e.t) > 0) { u->in[j] = u->in[j - 1]; j--; }
    u->in[j] = e;
}

void ndu_key(nabo_duel_t *u, nd_key_t k, bool down, uint32_t t)
{
    ndu_input_t e = {.t = t, .is_key = true, .key = (uint8_t)k, .down = down};
    push(u, &e);
}

void ndu_rx(nabo_duel_t *u, const uint8_t mac[ND_MAC_LEN], const nd_pkt_t *p, int rssi, uint32_t t)
{
    ndu_input_t e = {.t = t, .is_key = false, .rssi = (int8_t)rssi, .pkt = *p};
    memcpy(e.mac, mac, ND_MAC_LEN);
    push(u, &e);
}

// The attempt whose draw is still ahead (or due) and not yet shown.
static bool draw_ahead(const nabo_duel_t *u)
{
    const nd_t *m = &u->m;
    return m->scene == ND_DUEL && m->has_cur && !m->cur.resolved && !m->cur.has_go_shown && !m->cur.has_me;
}

static bool gate_is_current(const nabo_duel_t *u)
{
    return u->gate_seed == u->m.cur.seed && u->gate_go == u->m.cur.go_at;
}

bool ndu_go_pending(const nabo_duel_t *u, uint32_t *go_at)
{
    if (!draw_ahead(u) || (u->flipped && gate_is_current(u))) return false;
    *go_at = u->m.cur.go_at;
    return true;
}

void ndu_flipped(nabo_duel_t *u, uint32_t t)
{
    if (!draw_ahead(u)) return;
    u->gate_seed = u->m.cur.seed;
    u->gate_go = u->m.cur.go_at;
    u->flipped = true;
    u->flip_t = since(t, u->m.cur.go_at) < 0 ? u->m.cur.go_at : t;
}

static void cues_from_model(nabo_duel_t *u)
{
    nd_t *m = &u->m;
    for (int i = 0; i < m->fx_n; i++) {
        switch (m->fx[i].kind) {
        case ND_FX_TICK: case ND_FX_PAUSE: u->cues |= NDU_CUE_TICK; break;
        case ND_FX_INVITE: u->cues |= NDU_CUE_INVITE; break;
        case ND_FX_READY: u->cues |= NDU_CUE_READY; break;
        case ND_FX_STANDOFF:
            u->cues |= NDU_CUE_WIND;
            u->wind_ms = m->has_cur ? m->cur.delay : 2000;
            u->heart_at = m->fx[i].t;
            break;
        case ND_FX_FAKE: u->cues |= NDU_CUE_FAKE; u->fake_kind = m->fx[i].arg; break;
        case ND_FX_GO: u->cues |= NDU_CUE_GO; break;
        case ND_FX_SHOT: u->cues |= NDU_CUE_SHOT; break;
        case ND_FX_FOUL: u->cues |= NDU_CUE_FOUL; break;
        case ND_FX_WIN: u->cues |= NDU_CUE_WIN; break;
        case ND_FX_LOSE: u->cues |= NDU_CUE_LOSE; break;
        case ND_FX_REPLAY: u->cues |= NDU_CUE_REPLAY; break;
        case ND_FX_LEFT: u->cues |= NDU_CUE_LEFT; break;
        default: break;
        }
    }
    m->fx_n = 0;
}

// Heartbeat during the stand-off: slow at first, faster towards the draw.
static void heartbeat(nabo_duel_t *u, uint32_t t)
{
    const nd_t *m = &u->m;
    if (m->scene != ND_DUEL || nd_phase(m, t) != ND_PH_STANDOFF) return;
    uint32_t left = since(m->cur.go_at, t) > 0 ? m->cur.go_at - t : 0;
    uint32_t gap = 380 + 500 * left / (m->cur.delay ? m->cur.delay : 1);
    if (since(t, u->heart_at) >= (int32_t)gap) { u->heart_at = t; u->cues |= NDU_CUE_HEART; }
}

static void apply(nabo_duel_t *u, ndu_input_t *e)
{
    if (e->is_key) {
        uint32_t t = e->t;
        // A press after go_at but before the panel flipped could not have been a reaction to it.
        if (e->key == ND_KEY_OK && e->down && draw_ahead(u) && since(t, u->m.cur.go_at) >= 0) t = u->m.cur.go_at - 1;
        nd_key(&u->m, t, (nd_key_t)e->key, e->down);
    } else {
        nd_recv(&u->m, e->t, e->mac, &e->pkt, e->rssi);
    }
    cues_from_model(u);
}

static void drain(nabo_duel_t *u, uint32_t t, bool before_only)
{
    while (u->in_n && (before_only ? since(u->in[0].t, t) < 0 : since(u->in[0].t, t) <= 0)) {
        ndu_input_t e = u->in[0];
        memmove(u->in, u->in + 1, sizeof(u->in[0]) * (size_t)(u->in_n - 1));
        u->in_n--;
        apply(u, &e);
    }
}

void ndu_advance(nabo_duel_t *u, uint32_t now)
{
    uint32_t target = now - NDU_LAG_MS;
    // Safety net: a draw the platform never flipped for (it always should) must not stall the game.
    if (draw_ahead(u) && !(u->flipped && gate_is_current(u)) && since(target, u->m.cur.go_at) > 500)
        ndu_flipped(u, target);
    int steps = 0;
    while (since(target, u->ticked) > 0) {
        uint32_t t = u->ticked + 1;
        if (++steps > NDU_MAX_STEPS) t = target;   // far behind: jump (every timer in the model compares >=)
        bool crossing = false;
        // The draw waits for the panel: hold at go_at - 1 until it flipped, then tick at the flip.
        if (draw_ahead(u) && since(t, u->m.cur.go_at) >= 0) {
            if (!(u->flipped && gate_is_current(u))) {
                t = u->m.cur.go_at - 1;
                if (since(t, u->ticked) <= 0) break;
            } else {
                if (since(t, u->flip_t) < 0) t = since(u->flip_t, target) > 0 ? target : u->flip_t;
                crossing = since(t, u->flip_t) >= 0;
            }
        }
        // Presses before the flip go in first (as fouls), then the draw tick, then the rest.
        drain(u, t, crossing);
        nd_tick(&u->m, t);
        cues_from_model(u);
        if (crossing) drain(u, t, false);
        heartbeat(u, t);
        u->ticked = t;
        if (since(t, target) >= 0) break;
    }
}

bool ndu_inverted(const nabo_duel_t *u)
{
    const nd_t *m = &u->m;
    if (m->scene != ND_DUEL || !m->has_cur || m->cur.resolved) return false;
    if (m->cur.has_me) return m->cur.me.k == ND_RES_HIT;
    return m->cur.has_go_shown || (u->flipped && gate_is_current(u));
}

uint32_t ndu_take_cues(nabo_duel_t *u)
{
    uint32_t c = u->cues;
    u->cues = 0;
    return c;
}

void ndu_stamp_ping(uint8_t *wire, size_t len, uint32_t now)
{
    nd_pkt_t p;
    if (!nd_decode(wire, len, &p) || p.type != ND_PING) return;
    p.u.ping.t1 = now;
    nd_encode(&p, wire, ND_WIRE_MAX);
}

size_t ndu_ping_reply(const uint8_t *wire, size_t len, uint32_t rx_t, uint32_t session, uint8_t *out, size_t cap)
{
    nd_pkt_t p;
    if (!nd_decode(wire, len, &p) || p.type != ND_PING || p.ver != ND_VER || p.session != session) return 0;
    nd_pkt_t q;
    memset(&q, 0, sizeof(q));
    q.type = ND_PONG;
    q.ver = ND_VER;
    q.session = session;
    q.u.ping.id = p.u.ping.id;
    q.u.ping.t1 = p.u.ping.t1;
    q.u.ping.t2 = rx_t;
    return nd_encode(&q, out, cap);
}
