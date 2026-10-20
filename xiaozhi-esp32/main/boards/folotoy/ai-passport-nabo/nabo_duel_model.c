// NABO DUEL model: a line-by-line port of game-art/duel/prototype/duel_model.js. Keep the order
// of every step the same as the prototype; test_duel.py compares packets, cues and state against
// traces recorded from it.
#include "nabo_duel_model.h"

#include <stdio.h>
#include <string.h>

const nd_ai_t ND_AI[3] = {{330, 40, 50, "新手"}, {260, 30, 30, "老手"}, {205, 20, 10, "神枪"}};

static const uint8_t BCAST_NONE[ND_MAC_LEN] = {0};

static inline int32_t since(uint32_t a, uint32_t b) { return (int32_t)(a - b); }   // a - b
static inline bool mac_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, ND_MAC_LEN) == 0; }

uint32_t nd_mulberry32(uint32_t *a)
{
    *a += 0x6D2B79F5u;
    uint32_t t = *a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

static int32_t floordiv(int32_t a, int32_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

static int normal_int(uint32_t *rng, int mean, int sd)
{
    int32_t s = 0;
    for (int i = 0; i < 12; i++) s += (int32_t)(nd_mulberry32(rng) % 1000u);
    return mean + floordiv((s - 6000 + 6) * sd + 500, 1000);
}

void nd_plan(uint32_t seed, uint32_t go_at, int round, bool fake_on, nd_attempt_t *o)
{
    uint32_t rng = seed;
    memset(o, 0, sizeof(*o));
    o->seed = seed;
    o->go_at = go_at;
    o->delay = ND_T_MIN_DELAY + nd_mulberry32(&rng) % (uint32_t)(ND_T_MAX_DELAY - ND_T_MIN_DELAY + 1);
    o->standoff = go_at - o->delay;
    o->ready = o->standoff - ND_T_READY;
    if (fake_on && round >= 2) {
        uint32_t n = nd_mulberry32(&rng) % 3u;
        uint32_t lo = o->standoff + 400, hi = go_at - 350;
        for (uint32_t i = 0; i < n && since(hi, lo) > 0; i++) {
            nd_fake_t f;
            f.at = lo + nd_mulberry32(&rng) % (hi - lo);
            f.kind = (uint8_t)(nd_mulberry32(&rng) % 3u);
            int j = o->fake_n++;   // insertion sort, stable like Array.prototype.sort
            while (j > 0 && since(o->fakes[j - 1].at, f.at) > 0) { o->fakes[j] = o->fakes[j - 1]; j--; }
            o->fakes[j] = f;
        }
    }
}

nd_outcome_t nd_judge(nd_res_t me, nd_res_t opp)
{
    if (me.k == ND_RES_HIT && opp.k == ND_RES_HIT)
        return me.r < opp.r ? ND_OUT_WIN : me.r > opp.r ? ND_OUT_LOSE : ND_OUT_REPLAY;
    if (me.k == ND_RES_HIT) return ND_OUT_WIN;
    if (opp.k == ND_RES_HIT) return ND_OUT_LOSE;
    if (me.k == ND_RES_FOUL && opp.k == ND_RES_NONE) return ND_OUT_LOSE;
    if (me.k == ND_RES_NONE && opp.k == ND_RES_FOUL) return ND_OUT_WIN;
    return ND_OUT_REPLAY;
}

// ---------- small helpers ----------

static void go(nd_t *d, nd_scene_t s) { d->scene = s; d->scene_at = d->t; d->sel = 0; }
static void say(nd_t *d, nd_toast_t what) { d->toast = what; d->toast_until = d->t + ND_T_TOAST; }

static void fx(nd_t *d, nd_fx_kind_t k, uint8_t arg)
{
    if (d->fx_n >= ND_EVENTS) memmove(d->fx, d->fx + 1, sizeof(d->fx[0]) * (ND_EVENTS - 1)), d->fx_n--;
    d->fx[d->fx_n++] = (nd_fx_t){d->t, (uint8_t)k, arg};
}

static void make_name(char out[ND_NAME_LEN], const uint8_t mac[ND_MAC_LEN])
{
    snprintf(out, ND_NAME_LEN, "NABO-%02X%02X", mac[4], mac[5]);
}

static nd_conn_t *conn(nd_t *d, const uint8_t *mac)
{
    for (int i = 0; i < ND_MAX_CONNS; i++)
        if (d->conns[i].used && mac_eq(d->conns[i].mac, mac)) return &d->conns[i];
    for (int i = 0; i < ND_MAX_CONNS; i++)
        if (!d->conns[i].used) {
            memset(&d->conns[i], 0, sizeof(d->conns[i]));
            d->conns[i].used = true;
            memcpy(d->conns[i].mac, mac, ND_MAC_LEN);
            return &d->conns[i];
        }
    // Full: recycle the oldest entry that is not the linked peer.
    for (int i = 0; i < ND_MAX_CONNS; i++)
        if (!(d->link.active && mac_eq(d->conns[i].mac, d->link.mac))) {
            memmove(&d->conns[i], &d->conns[i + 1], sizeof(d->conns[0]) * (ND_MAX_CONNS - 1 - i));
            nd_conn_t *c = &d->conns[ND_MAX_CONNS - 1];
            memset(c, 0, sizeof(*c));
            c->used = true;
            memcpy(c->mac, mac, ND_MAC_LEN);
            return c;
        }
    return &d->conns[0];
}

static void drop_conn(nd_t *d, const uint8_t *mac)
{
    for (int i = 0; i < ND_MAX_CONNS; i++)
        if (d->conns[i].used && mac_eq(d->conns[i].mac, mac)) d->conns[i].pending_n = 0;
}

static nd_pkt_t pk(const nd_t *d, nd_type_t type, bool has_session, uint32_t session)
{
    nd_pkt_t p;
    memset(&p, 0, sizeof(p));
    p.type = (uint8_t)type;
    p.ver = ND_VER;
    p.session = has_session ? session : (d->link.active ? d->link.session : 0);
    return p;
}

static void raw(nd_t *d, const uint8_t *to, const nd_pkt_t *p)
{
    if (d->out_n >= ND_OUTBOX) { d->out_overflow = true; return; }
    nd_out_t *o = &d->out[d->out_n++];
    o->broadcast = to == NULL;
    memcpy(o->to, to ? to : BCAST_NONE, ND_MAC_LEN);
    o->pkt = *p;
}

static void rel(nd_t *d, const uint8_t *to, nd_pkt_t p)
{
    nd_conn_t *c = conn(d, to);
    c->tx_seq = (uint16_t)(c->tx_seq + 1);
    if (!c->tx_seq) c->tx_seq = 1;
    p.seq = c->tx_seq;
    if (c->pending_n < ND_MAX_PENDING) c->pending[c->pending_n++] = (nd_pending_t){p, d->t, d->t, 1};
    raw(d, to, &p);
}

// ---------- forward declarations ----------

static void enter_lobby(nd_t *d);
static void start_match(nd_t *d, bool solo, uint8_t bo, bool fake);
static void lost(nd_t *d, nd_left_t why);
static void begin_sync(nd_t *d, uint32_t t);
static bool host_tick(nd_t *d, uint32_t t);
static void press(nd_t *d, uint32_t t);
static void lock(nd_t *d, nd_res_t res);
static void opp_result(nd_t *d, nd_res_t res);
static void resolve(nd_t *d);
static void pause(nd_t *d);
static void resume(nd_t *d, bool mine);
static void quit(nd_t *d);

void nd_init(nd_t *d, const uint8_t mac[ND_MAC_LEN], uint32_t entropy, const nd_settings_t *set, uint16_t best,
             bool suppress, uint32_t now)
{
    memset(d, 0, sizeof(*d));
    memcpy(d->mac, mac, ND_MAC_LEN);
    make_name(d->name, mac);
    d->rng = entropy;
    d->set = set ? *set : (nd_settings_t){3, 1, true};
    d->best = best;
    d->suppress = suppress;
    d->t = now;
    go(d, ND_COVER);
}

// ---------- cover ----------

static void key_cover(nd_t *d, nd_key_t k)
{
    const int n = ND_COVER_COUNT;
    if (k == ND_KEY_UP) d->sel = (uint8_t)((d->sel + n - 1) % n);
    else if (k == ND_KEY_DOWN) d->sel = (uint8_t)((d->sel + 1) % n);
    else {
        switch (d->sel) {
        case ND_COVER_ONLINE: enter_lobby(d); break;
        case ND_COVER_SOLO: start_match(d, true, 0, false); break;
        case ND_COVER_BO: d->set.bo = d->set.bo == 1 ? 3 : d->set.bo == 3 ? 5 : 1; break;
        case ND_COVER_FAKE: d->set.fake = !d->set.fake; break;
        case ND_COVER_AI: d->set.ai = (uint8_t)((d->set.ai + 1) % 3); break;
        }
        fx(d, ND_FX_TICK, 0);
    }
}

// ---------- lobby ----------

static void enter_lobby(nd_t *d)
{
    d->link.active = false;
    d->has_match = false;
    d->has_cur = false;
    go(d, ND_LOBBY);
    d->hello_valid = false;
}

static int mac_cmp(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, ND_MAC_LEN); }

int nd_lobby_list(const nd_t *d, const nd_peer_t *rows[ND_LIST_PEERS])
{
    const nd_peer_t *all[ND_MAX_PEERS];
    int n = 0;
    for (int i = 0; i < d->peer_n; i++) {
        const nd_peer_t *p = &d->peers[i];
        int j = n++;
        // busy last, then strongest signal, then MAC
        while (j > 0) {
            const nd_peer_t *q = all[j - 1];
            int c = (int)q->busy - (int)p->busy;
            if (!c) c = p->rssi - q->rssi;
            if (!c) c = mac_cmp(q->mac, p->mac);
            if (c <= 0) break;
            all[j] = q;
            j--;
        }
        all[j] = p;
    }
    if (n > ND_LIST_PEERS) n = ND_LIST_PEERS;
    for (int i = 0; i < n; i++) rows[i] = all[i];
    return n;
}

static void hello(nd_t *d, const uint8_t *from, const nd_pkt_t *p, int rssi)
{
    if (d->scene != ND_LOBBY && d->scene != ND_INVITING && d->scene != ND_INVITED) return;
    nd_peer_t *slot = NULL;
    for (int i = 0; i < d->peer_n; i++)
        if (mac_eq(d->peers[i].mac, from)) slot = &d->peers[i];
    if (!slot) {
        if (d->peer_n >= ND_MAX_PEERS) return;
        slot = &d->peers[d->peer_n++];
    }
    memcpy(slot->mac, from, ND_MAC_LEN);
    memcpy(slot->name, p->u.hello.name, ND_NAME_LEN);
    slot->name[ND_NAME_LEN - 1] = 0;
    slot->busy = p->u.hello.busy != 0;
    slot->ver = p->ver;
    slot->rssi = (int8_t)rssi;
    slot->seen = d->t;
}

static void broadcast_hello(nd_t *d, uint32_t t, bool busy)
{
    if (d->hello_valid && since(t, d->hello_at) < ND_T_HELLO) return;
    d->hello_valid = true;
    d->hello_at = t;
    nd_pkt_t p;
    memset(&p, 0, sizeof(p));
    p.type = ND_HELLO;
    p.ver = ND_VER;
    memcpy(p.u.hello.name, d->name, ND_NAME_LEN);
    p.u.hello.busy = busy;
    raw(d, NULL, &p);
}

static void expire_peers(nd_t *d, uint32_t t)
{
    int w = 0;
    for (int i = 0; i < d->peer_n; i++)
        if (since(t, d->peers[i].seen) <= ND_T_PEER_EXPIRE) d->peers[w++] = d->peers[i];
    d->peer_n = (uint8_t)w;
}

static void tick_lobby(nd_t *d, uint32_t t)
{
    broadcast_hello(d, t, false);
    expire_peers(d, t);
    const nd_peer_t *rows[ND_LIST_PEERS];
    int n = nd_lobby_list(d, rows) + 1;
    if (d->sel >= n) d->sel = (uint8_t)(n - 1);
}

static void key_lobby(nd_t *d, nd_key_t k)
{
    const nd_peer_t *rows[ND_LIST_PEERS];
    int np = nd_lobby_list(d, rows), n = np + 1;
    if (k == ND_KEY_UP) d->sel = (uint8_t)((d->sel + n - 1) % n);
    else if (k == ND_KEY_DOWN) d->sel = (uint8_t)((d->sel + 1) % n);
    else {
        if (d->sel >= np) { go(d, ND_COVER); d->sel = 0; return; }
        const nd_peer_t *p = rows[d->sel];
        if (p->ver != ND_VER) { say(d, ND_TOAST_VERSION); return; }
        if (p->busy) { say(d, ND_TOAST_PEER_BUSY); return; }
        nd_invite_t *iv = &d->invite;
        memcpy(iv->mac, p->mac, ND_MAC_LEN);
        memcpy(iv->name, p->name, ND_NAME_LEN);
        iv->session = nd_mulberry32(&d->rng);
        iv->at = d->t;
        iv->bo = d->set.bo;
        iv->fake = d->set.fake;
        nd_pkt_t q = pk(d, ND_INVITE, true, iv->session);
        memcpy(q.u.invite.name, d->name, ND_NAME_LEN);
        q.u.invite.bo = d->set.bo;
        q.u.invite.fake = d->set.fake ? 1 : 0;
        rel(d, iv->mac, q);
        go(d, ND_INVITING);
        fx(d, ND_FX_TICK, 0);
    }
}

static void tick_inviting(nd_t *d, uint32_t t)
{
    broadcast_hello(d, t, true);
    expire_peers(d, t);
    if (since(t, d->invite.at) >= ND_T_INVITE_WAIT) {
        say(d, ND_TOAST_NO_ANSWER);
        drop_conn(d, d->invite.mac);
        enter_lobby(d);
    }
}

static void key_inviting(nd_t *d, nd_key_t k)
{
    if (k != ND_KEY_UP) return;
    nd_pkt_t p = pk(d, ND_BYE, true, d->invite.session);
    p.u.why.why = ND_WHY_CANCEL;
    raw(d, d->invite.mac, &p);
    drop_conn(d, d->invite.mac);
    enter_lobby(d);
}

static void decline(nd_t *d)
{
    nd_pkt_t p = pk(d, ND_DECLINE, true, d->invited.session);
    p.u.why.why = ND_WHY_NO;
    rel(d, d->invited.mac, p);
    enter_lobby(d);
}

static void tick_invited(nd_t *d, uint32_t t)
{
    broadcast_hello(d, t, true);
    expire_peers(d, t);
    if (since(t, d->invited.at) >= ND_T_INVITE_WAIT) decline(d);
}

static void accept(nd_t *d, const nd_invite_t *inv)
{
    nd_invite_t iv = *inv;
    nd_pkt_t p = pk(d, ND_ACCEPT, true, iv.session);
    memcpy(p.u.accept.name, d->name, ND_NAME_LEN);
    rel(d, iv.mac, p);
    d->link = (nd_link_t){.active = true, .session = iv.session, .last_rx = d->t, .last_beat = d->t, .host = false};
    memcpy(d->link.mac, iv.mac, ND_MAC_LEN);
    memcpy(d->link.name, iv.name, ND_NAME_LEN);
    start_match(d, false, iv.bo, iv.fake);
}

static void key_invited(nd_t *d, nd_key_t k)
{
    if (k == ND_KEY_OK) accept(d, &d->invited);
    else if (k == ND_KEY_UP) decline(d);
}

static void pkt_invite(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    nd_invite_t inv;
    memcpy(inv.mac, from, ND_MAC_LEN);
    memcpy(inv.name, p->u.invite.name, ND_NAME_LEN);
    inv.name[ND_NAME_LEN - 1] = 0;
    inv.session = p->session;
    inv.bo = p->u.invite.bo;
    inv.fake = p->u.invite.fake != 0;
    inv.at = d->t;
    if (d->scene == ND_INVITING && mac_eq(d->invite.mac, from)) {
        // Both invited each other: the larger MAC hosts, the smaller accepts the other invite.
        if (mac_cmp(d->mac, from) > 0) return;
        drop_conn(d, from);
        accept(d, &inv);
        return;
    }
    if (d->scene != ND_LOBBY) {
        nd_pkt_t q = pk(d, ND_DECLINE, true, p->session);
        q.u.why.why = ND_WHY_BUSY;
        rel(d, from, q);
        return;
    }
    d->invited = inv;
    go(d, ND_INVITED);
    fx(d, ND_FX_INVITE, 0);
}

static void pkt_accept(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    if (d->scene != ND_INVITING || !mac_eq(d->invite.mac, from) || p->session != d->invite.session) return;
    d->link = (nd_link_t){.active = true, .session = p->session, .last_rx = d->t, .last_beat = d->t, .host = true};
    memcpy(d->link.mac, from, ND_MAC_LEN);
    memcpy(d->link.name, d->invite.name, ND_NAME_LEN);
    start_match(d, false, d->invite.bo, d->invite.fake);
}

static void pkt_decline(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    if (d->scene != ND_INVITING || !mac_eq(d->invite.mac, from)) return;
    say(d, p->u.why.why == ND_WHY_BUSY ? ND_TOAST_BUSY : ND_TOAST_DECLINED);
    drop_conn(d, from);
    enter_lobby(d);
}

static void pkt_bye(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    if (d->scene == ND_INVITED && mac_eq(d->invited.mac, from)) { say(d, ND_TOAST_CANCELLED); enter_lobby(d); return; }
    if (!d->link.active || !mac_eq(d->link.mac, from) || p->session != d->link.session) return;
    lost(d, p->u.why.why == ND_WHY_QUIT ? ND_LEFT_QUIT : ND_LEFT_BYE);
}

// ---------- match ----------

static void start_match(nd_t *d, bool solo, uint8_t bo, bool fake)
{
    nd_match_t *m = &d->match;
    memset(m, 0, sizeof(*m));
    m->solo = solo;
    m->bo = solo ? d->set.bo : bo;
    m->fake = solo ? d->set.fake : fake;
    m->need = (uint8_t)((m->bo + 1) >> 1);
    m->round = 1;
    m->attempt = 1;
    m->host = solo || d->link.host;
    if (solo) strcpy(m->opp_name, "NABO");
    else memcpy(m->opp_name, d->link.name, ND_NAME_LEN);
    m->ai = d->set.ai;
    d->has_match = true;
    d->has_cur = false;
    go(d, ND_SYNC);
    if (m->host) {
        begin_sync(d, d->t);
        m->has_issue_at = true;
        m->issue_at = d->t;
    }
}

static void begin_sync(nd_t *d, uint32_t t)
{
    nd_match_t *m = &d->match;
    if (m->solo) { m->sync_done = true; m->sync_done_at = t; return; }
    m->pinging = true;
    m->ping_start = t;
    m->ping_sent = 0;
    m->has_best = false;
    m->sync_done = false;
}

static void issue_round(nd_t *d, uint32_t t);

// Returns true when it started a new attempt this tick.
static bool host_tick(nd_t *d, uint32_t t)
{
    nd_match_t *m = &d->match;
    if (!d->has_match || !m->host || m->done || d->scene == ND_PAUSE || d->scene == ND_OPPPAUSE) return false;
    if (!m->sync_done && m->pinging) {
        if (m->ping_sent < ND_T_PING_N && since(t, m->ping_start) >= m->ping_sent * ND_T_PING_GAP) {
            nd_pkt_t p = pk(d, ND_PING, false, 0);
            p.u.ping.id = m->ping_sent;
            p.u.ping.t1 = t;
            raw(d, d->link.mac, &p);
            m->ping_sent++;
        }
        if (m->ping_sent >= ND_T_PING_N && since(t, m->ping_start) >= (ND_T_PING_N - 1) * ND_T_PING_GAP + ND_T_PING_WAIT) {
            if (m->has_best) {
                m->offset = m->best_off;
                m->rtt = m->best_rtt;
                m->has_rtt = true;
                m->sync_done = true;
                m->sync_done_at = t;
            } else {
                begin_sync(d, t);   // not a single PONG: try again (the heartbeat notices a lost link)
            }
        }
    }
    if (m->sync_done && m->has_issue_at && since(t, m->issue_at) >= 0 && (!d->has_cur || d->cur.resolved)) {
        issue_round(d, t);
        return true;
    }
    return false;
}

static void pkt_ping(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    nd_pkt_t q = pk(d, ND_PONG, false, 0);
    q.u.ping.id = p->u.ping.id;
    q.u.ping.t1 = p->u.ping.t1;
    q.u.ping.t2 = d->t;
    raw(d, from, &q);
}

static void pkt_pong(nd_t *d, const nd_pkt_t *p)
{
    nd_match_t *m = &d->match;
    if (!d->has_match || !m->host || m->sync_done || !m->pinging) return;
    uint32_t rtt = d->t - p->u.ping.t1;
    // off = t2 - round((t1 + t3) / 2), halves rounded up like Math.floor(x + 1) / 2
    int64_t mid = ((int64_t)p->u.ping.t1 + (int64_t)d->t + 1) / 2;
    int32_t off = (int32_t)((int64_t)p->u.ping.t2 - mid);
    if (!m->has_best || rtt < m->best_rtt) { m->has_best = true; m->best_rtt = rtt; m->best_off = off; }
}

static void begin_attempt(nd_t *d, uint32_t seed, uint32_t go_at);

static void issue_round(nd_t *d, uint32_t t)
{
    nd_match_t *m = &d->match;
    uint32_t seed = nd_mulberry32(&d->rng);
    nd_attempt_t plan;
    nd_plan(seed, 0, m->round, false, &plan);
    uint32_t ready = t + (m->solo ? 300 : ND_T_LEAD);
    if (m->has_reveal_until && since(m->reveal_until, ready) > 0) ready = m->reveal_until;
    uint32_t go_at = ready + ND_T_READY + plan.delay;
    m->has_issue_at = false;
    if (!m->solo) {
        nd_pkt_t p = pk(d, ND_ROUND, false, 0);
        p.u.round.r = m->round;
        p.u.round.a = m->attempt;
        p.u.round.seed = seed;
        p.u.round.go = go_at + (uint32_t)m->offset;
        p.u.round.sh = m->me;
        p.u.round.sg = m->opp;
        rel(d, d->link.mac, p);
    }
    begin_attempt(d, seed, go_at);
}

static void pkt_round(nd_t *d, const uint8_t *from, const nd_pkt_t *p)
{
    if (!d->has_match || d->match.host) return;
    if (d->match.done) {   // rematch: the host sent the first round of the next match
        if (p->u.round.r != 1 || p->u.round.a != 1) return;
        start_match(d, false, d->match.bo, d->match.fake);
    }
    nd_match_t *m = &d->match;
    if (since(p->u.round.go, d->t) < ND_T_LATE_MIN) {
        nd_pkt_t q = pk(d, ND_LATE, false, 0);
        q.u.late.r = p->u.round.r;
        q.u.late.a = p->u.round.a;
        rel(d, from, q);
        return;
    }
    m->round = p->u.round.r;
    m->attempt = p->u.round.a;
    m->me = p->u.round.sg;   // the host's score is authoritative
    m->opp = p->u.round.sh;
    if (d->scene == ND_PAUSE || d->scene == ND_OPPPAUSE) return;
    begin_attempt(d, p->u.round.seed, p->u.round.go);
}

static void pkt_late(nd_t *d, const nd_pkt_t *p)
{
    nd_match_t *m = &d->match;
    if (!d->has_match || !m->host || p->u.late.r != m->round || p->u.late.a != m->attempt) return;
    d->has_cur = false;
    begin_sync(d, d->t);
    m->has_issue_at = true;
    m->issue_at = d->t;
}

static void begin_attempt(nd_t *d, uint32_t seed, uint32_t go_at)
{
    nd_match_t *m = &d->match;
    nd_attempt_t *c = &d->cur;
    nd_plan(seed, go_at, m->round, m->fake, c);
    c->round = m->round;
    c->attempt = m->attempt;
    d->has_cur = true;
    if (m->solo) {
        uint32_t r2 = seed ^ 0xA5A5A5A5u;
        const nd_ai_t *ai = &ND_AI[m->ai];
        if (nd_mulberry32(&r2) % 1000u < ai->foul_permille) {
            uint32_t span = c->delay > 301 ? c->delay - 300 : 1;
            c->ai_foul = c->standoff + 200 + nd_mulberry32(&r2) % span;
            c->ai_foul_set = true;
        } else {
            int r = normal_int(&r2, ai->mean, ai->sd);
            c->ai_r = (uint16_t)(r < 140 ? 140 : r > 900 ? 900 : r);
            c->ai_r_set = true;
        }
    }
    go(d, ND_DUEL);
}

nd_phase_t nd_phase(const nd_t *d, uint32_t t)
{
    const nd_attempt_t *c = &d->cur;
    if (!d->has_cur) return ND_PH_WAIT;
    if (c->resolved) return ND_PH_REVEAL;
    if (since(t, c->ready) < 0) return ND_PH_WAIT;
    if (since(t, c->standoff) < 0) return ND_PH_READY;
    if (since(t, c->go_at) < 0) return ND_PH_STANDOFF;
    return ND_PH_GO;
}

static void tick_duel(nd_t *d, uint32_t t)
{
    bool had = d->has_cur;
    if (host_tick(d, t)) return;   // the prototype keeps working on the attempt it had before this tick
    if (!had || !d->has_cur) return;
    nd_attempt_t *c = &d->cur;
    nd_match_t *m = &d->match;
    if (!c->resolved) {
        if (since(t, c->ready) >= 0 && !c->ready_fx) { c->ready_fx = true; fx(d, ND_FX_READY, 0); }
        if (since(t, c->standoff) >= 0 && !c->stand_fx) { c->stand_fx = true; fx(d, ND_FX_STANDOFF, 0); }
        while (c->fake_idx < c->fake_n && since(t, c->fakes[c->fake_idx].at) >= 0) {
            fx(d, ND_FX_FAKE, c->fakes[c->fake_idx].kind);
            c->fake_idx++;
        }
        if (since(t, c->go_at) >= 0 && !c->has_go_shown) {   // firmware: INVOFF at go_at, stamped
            c->has_go_shown = true;
            c->go_shown = t;
            fx(d, ND_FX_GO, 0);
        }
        if (m->solo && !c->has_opp) {
            if (c->ai_foul_set && since(t, c->ai_foul) >= 0) opp_result(d, (nd_res_t){ND_RES_FOUL, 0});
            else if (c->ai_r_set && c->has_go_shown && since(t, c->go_shown + c->ai_r) >= 0)
                opp_result(d, (nd_res_t){ND_RES_HIT, c->ai_r});
        }
        if (!c->has_me && c->has_go_shown && since(t, c->go_shown + ND_T_TIMEOUT) >= 0) lock(d, (nd_res_t){ND_RES_NONE, 0});
        if (!c->has_me && c->has_lock_at && since(t, c->lock_at) >= 0) lock(d, (nd_res_t){ND_RES_NONE, 0});
    } else if (m->done && m->has_reveal_until && since(t, m->reveal_until) >= 0 && d->scene == ND_DUEL) {
        go(d, ND_END);
    }
}

static void press(nd_t *d, uint32_t t)
{
    nd_attempt_t *c = &d->cur;
    if (!d->has_cur || c->has_me || c->resolved || since(t, c->standoff) < 0) return;
    if (since(t, c->go_at) < 0) { lock(d, (nd_res_t){ND_RES_FOUL, 0}); return; }
    if (!c->has_go_shown) {   // the key event came before this millisecond's tick
        c->has_go_shown = true;
        c->go_shown = c->go_at;
        fx(d, ND_FX_GO, 0);
    }
    uint32_t r = t - c->go_shown;
    lock(d, (nd_res_t){ND_RES_HIT, (uint16_t)(r > 65535 ? 65535 : r)});
}

static void lock(nd_t *d, nd_res_t res)
{
    nd_attempt_t *c = &d->cur;
    c->me = res;
    c->has_me = true;
    fx(d, res.k == ND_RES_FOUL ? ND_FX_FOUL : res.k == ND_RES_HIT ? ND_FX_SHOT : ND_FX_NONE, 0);
    if (!d->match.solo) {
        nd_pkt_t p = pk(d, ND_RESULT, false, 0);
        p.u.result.r = c->round;
        p.u.result.a = c->attempt;
        p.u.result.k = res.k;
        p.u.result.ms = res.k == ND_RES_HIT ? res.r : 0;
        rel(d, d->link.mac, p);
    }
    if (c->has_opp) resolve(d);
}

static void pkt_result(nd_t *d, const nd_pkt_t *p)
{
    nd_attempt_t *c = &d->cur;
    if (!d->has_cur || p->u.result.r != c->round || p->u.result.a != c->attempt || c->has_opp) return;
    nd_res_t r = {p->u.result.k, p->u.result.k == ND_RES_HIT ? p->u.result.ms : 0};
    opp_result(d, r);
}

static void opp_result(nd_t *d, nd_res_t res)
{
    nd_attempt_t *c = &d->cur;
    c->opp = res;
    c->has_opp = true;
    if (!c->has_me) {
        if (res.k == ND_RES_FOUL) lock(d, (nd_res_t){ND_RES_NONE, 0});
        else if (res.k == ND_RES_HIT) {
            // Firing now would be slower than the opponent anyway: past GO + b, lock as "none".
            c->lock_at = (c->has_go_shown ? c->go_shown : c->go_at) + res.r + 1;
            c->has_lock_at = true;
            if (since(d->t, c->lock_at) >= 0) lock(d, (nd_res_t){ND_RES_NONE, 0});
        }
        return;
    }
    resolve(d);
}

static void resolve(nd_t *d)
{
    nd_attempt_t *c = &d->cur;
    nd_match_t *m = &d->match;
    if (c->resolved) return;
    c->resolved = true;
    c->resolved_at = d->t;
    nd_outcome_t out = nd_judge(c->me, c->opp);
    c->outcome = (uint8_t)out;
    if (out == ND_OUT_WIN) m->me++;
    else if (out == ND_OUT_LOSE) m->opp++;
    if (c->me.k == ND_RES_HIT && !d->suppress && (!d->best || c->me.r < d->best)) { d->best = c->me.r; c->new_best = true; }
    m->last = *c;
    m->has_last = true;
    m->reveal_until = d->t + ND_T_REVEAL;
    m->has_reveal_until = true;
    if (out == ND_OUT_REPLAY && m->attempt < ND_T_MAX_ATTEMPT) m->attempt++;
    else { m->round++; m->attempt = 1; }
    if (m->me >= m->need || m->opp >= m->need) m->done = true;
    fx(d, out == ND_OUT_WIN ? ND_FX_WIN : out == ND_OUT_LOSE ? ND_FX_LOSE : ND_FX_REPLAY, 0);
    if (m->host && !m->done) {
        // The result stays up; the host re-syncs the clocks meanwhile.
        begin_sync(d, d->t + 300);
        m->ping_start = d->t + 300;
        m->has_issue_at = true;
        m->issue_at = d->t + 300;
    }
}

// ---------- pause ----------

static void pause(nd_t *d)
{
    d->has_cur = false;
    go(d, ND_PAUSE);
    if (!d->match.solo) { nd_pkt_t p = pk(d, ND_PAUSE_PKT, false, 0); rel(d, d->link.mac, p); }
    fx(d, ND_FX_PAUSE, 0);
}

static void key_pause(nd_t *d, nd_key_t k)
{
    if (k == ND_KEY_UP || k == ND_KEY_DOWN) { d->sel ^= 1; return; }
    if (d->sel == 0) resume(d, true);
    else quit(d);
}

static void resume(nd_t *d, bool mine)
{
    nd_match_t *m = &d->match;
    go(d, ND_DUEL);
    d->has_cur = false;
    if (mine && !m->solo) { nd_pkt_t p = pk(d, ND_RESUME, false, 0); rel(d, d->link.mac, p); }
    if (m->host) { begin_sync(d, d->t); m->has_issue_at = true; m->issue_at = d->t; }
}

static void quit(nd_t *d)
{
    if (!d->match.solo) {
        nd_pkt_t bye = pk(d, ND_BYE, false, 0);
        bye.u.why.why = ND_WHY_QUIT;
        for (int i = 0; i < 3; i++) raw(d, d->link.mac, &bye);
        drop_conn(d, d->link.mac);
        enter_lobby(d);
    } else {
        d->has_match = false;
        go(d, ND_COVER);
    }
}

// ---------- end of the match ----------

static void maybe_rematch(nd_t *d)
{
    nd_match_t *m = &d->match;
    if (m->host && m->want_next && m->opp_next) {
        start_match(d, false, m->bo, m->fake);
        d->match.has_issue_at = true;
        d->match.issue_at = d->t;
    }
}

static void key_end(nd_t *d, nd_key_t k)
{
    nd_match_t *m = &d->match;
    if (k == ND_KEY_UP || k == ND_KEY_DOWN) { if (!m->want_next) d->sel ^= 1; return; }
    if (m->want_next) return;
    if (d->sel == 1) { quit(d); return; }
    if (m->solo) { start_match(d, true, 0, false); return; }
    m->want_next = true;
    nd_pkt_t p = pk(d, ND_NEXT, false, 0);
    rel(d, d->link.mac, p);
    maybe_rematch(d);
}

// ---------- link loss ----------

static void lost(nd_t *d, nd_left_t why)
{
    if (!d->link.active) return;
    drop_conn(d, d->link.mac);
    d->link.active = false;
    d->has_cur = false;
    d->left_why = why;
    go(d, ND_LEFT);
    fx(d, ND_FX_LEFT, 0);
}

// ---------- entry points ----------

void nd_key(nd_t *d, uint32_t t, nd_key_t k, bool down)
{
    d->t = t;
    if (k == ND_KEY_UP || k == ND_KEY_DOWN) { d->hold[k] = down; d->hold_at[k] = t; }
    if (!down) return;
    if (k == ND_KEY_OK && d->scene == ND_DUEL) { press(d, t); return; }
    switch (d->scene) {
    case ND_COVER: key_cover(d, k); break;
    case ND_LOBBY: key_lobby(d, k); break;
    case ND_INVITING: key_inviting(d, k); break;
    case ND_INVITED: key_invited(d, k); break;
    case ND_PAUSE: key_pause(d, k); break;
    case ND_END: key_end(d, k); break;
    default: break;
    }
}

static bool reliable(uint8_t type)
{
    switch (type) {
    case ND_INVITE: case ND_ACCEPT: case ND_DECLINE: case ND_ROUND: case ND_LATE: case ND_RESULT: case ND_NEXT:
    case ND_PAUSE_PKT: case ND_RESUME: return true;
    default: return false;
    }
}

void nd_recv(nd_t *d, uint32_t t, const uint8_t from[ND_MAC_LEN], const nd_pkt_t *p, int rssi)
{
    d->t = t;
    if (!p) return;
    if (p->type == ND_HELLO) { hello(d, from, p, rssi); return; }
    if (p->ver != ND_VER) return;
    nd_conn_t *c = conn(d, from);
    if (p->type == ND_ACK) {
        int w = 0;
        for (int i = 0; i < c->pending_n; i++)
            if (c->pending[i].pkt.seq != p->ack) c->pending[w++] = c->pending[i];
        c->pending_n = (uint8_t)w;
        if (d->link.active && mac_eq(d->link.mac, from)) d->link.last_rx = t;
        return;
    }
    if (reliable(p->type)) {
        nd_pkt_t ack = pk(d, ND_ACK, true, p->session);
        ack.ack = p->seq;
        raw(d, from, &ack);
        for (int i = 0; i < c->seen_n; i++)
            if (c->seen[i] == p->seq) return;
        if (c->seen_n >= ND_SEEN) memmove(c->seen, c->seen + 1, sizeof(c->seen[0]) * (ND_SEEN - 1)), c->seen_n--;
        c->seen[c->seen_n++] = p->seq;
    }
    // Once paired, only this match's session counts.
    bool linked = d->link.active && mac_eq(d->link.mac, from);
    if (linked) {
        if (p->session != d->link.session) return;
        d->link.last_rx = t;
    } else if (p->type != ND_INVITE && p->type != ND_ACCEPT && p->type != ND_DECLINE && p->type != ND_BYE) {
        return;
    }
    switch (p->type) {
    case ND_INVITE: pkt_invite(d, from, p); break;
    case ND_ACCEPT: pkt_accept(d, from, p); break;
    case ND_DECLINE: pkt_decline(d, from, p); break;
    case ND_BYE: pkt_bye(d, from, p); break;
    case ND_PING: pkt_ping(d, from, p); break;
    case ND_PONG: pkt_pong(d, p); break;
    case ND_ROUND: pkt_round(d, from, p); break;
    case ND_LATE: pkt_late(d, p); break;
    case ND_RESULT: pkt_result(d, p); break;
    case ND_PAUSE_PKT: if (d->has_match && !d->match.done) { d->has_cur = false; go(d, ND_OPPPAUSE); } break;
    case ND_RESUME: if (d->scene == ND_OPPPAUSE) resume(d, false); break;
    case ND_NEXT: if (d->has_match && d->match.done) { d->match.opp_next = true; maybe_rematch(d); } break;
    default: break;
    }
}

void nd_tick(nd_t *d, uint32_t t)
{
    d->t = t;
    // Retransmit reliable messages; a message nobody acknowledged for GIVEUP means the link is gone.
    for (int i = 0; i < ND_MAX_CONNS; i++) {
        nd_conn_t *c = &d->conns[i];
        if (!c->used) continue;
        bool dead = false;
        int w = 0;
        for (int j = 0; j < c->pending_n; j++) {
            nd_pending_t *p = &c->pending[j];
            if (since(t, p->first) >= ND_T_GIVEUP) { dead = true; continue; }
            if (since(t, p->last) >= ND_T_RETX) { p->last = t; p->tries++; raw(d, c->mac, &p->pkt); }
            c->pending[w++] = *p;
        }
        if (dead) {
            c->pending_n = (uint8_t)w;
            if (d->link.active && mac_eq(d->link.mac, c->mac)) lost(d, ND_LEFT_LOST);
        }
    }
    if (d->toast != ND_TOAST_NONE && since(t, d->toast_until) >= 0) d->toast = ND_TOAST_NONE;
    // Hold UP or DOWN to pause.
    if ((d->scene == ND_DUEL || d->scene == ND_SYNC) && d->has_match) {
        for (int k = 0; k < 2; k++)
            if (d->hold[k] && since(t, d->hold_at[k]) >= ND_T_LONG) { d->hold[k] = false; pause(d); }
    }
    // Heartbeat and loss of the peer.
    if (d->link.active && !(d->has_match && d->match.solo)) {
        if (since(t, d->link.last_beat) >= ND_T_BEAT) {
            d->link.last_beat = t;
            nd_pkt_t p = pk(d, ND_BEAT, false, 0);
            raw(d, d->link.mac, &p);
        }
        if (since(t, d->link.last_rx) >= ND_T_DEAD) lost(d, ND_LEFT_LOST);
    }
    switch (d->scene) {
    case ND_LOBBY: tick_lobby(d, t); break;
    case ND_INVITING: tick_inviting(d, t); break;
    case ND_INVITED: tick_invited(d, t); break;
    case ND_SYNC: host_tick(d, t); break;
    case ND_DUEL: tick_duel(d, t); break;
    case ND_LEFT: if (since(t, d->scene_at) >= ND_T_LEFT_SHOW) enter_lobby(d); break;
    default: break;
    }
}

// ---------- wire format ----------

static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { put16(b, (uint16_t)v); put16(b + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t get32(const uint8_t *b) { return get16(b) | (uint32_t)get16(b + 2) << 16; }

#define ND_HDR 13

size_t nd_encode(const nd_pkt_t *p, uint8_t *b, size_t cap)
{
    if (cap < ND_WIRE_MAX) return 0;
    b[0] = 'N', b[1] = 'D', b[2] = p->ver, b[3] = p->type;
    put32(b + 4, p->session);
    put16(b + 8, p->seq);
    put16(b + 10, p->ack);
    b[12] = 0;
    uint8_t *q = b + ND_HDR;
    switch (p->type) {
    case ND_HELLO: memcpy(q, p->u.hello.name, ND_NAME_LEN); q[ND_NAME_LEN] = p->u.hello.busy; q += ND_NAME_LEN + 1; break;
    case ND_INVITE:
        memcpy(q, p->u.invite.name, ND_NAME_LEN);
        q[ND_NAME_LEN] = p->u.invite.bo, q[ND_NAME_LEN + 1] = p->u.invite.fake;
        q += ND_NAME_LEN + 2;
        break;
    case ND_ACCEPT: memcpy(q, p->u.accept.name, ND_NAME_LEN); q += ND_NAME_LEN; break;
    case ND_DECLINE: case ND_BYE: *q++ = p->u.why.why; break;
    case ND_PING: case ND_PONG: q[0] = p->u.ping.id; put32(q + 1, p->u.ping.t1); put32(q + 5, p->u.ping.t2); q += 9; break;
    case ND_ROUND:
        q[0] = p->u.round.r, q[1] = p->u.round.a, q[2] = p->u.round.sh, q[3] = p->u.round.sg;
        put32(q + 4, p->u.round.seed);
        put32(q + 8, p->u.round.go);
        q += 12;
        break;
    case ND_LATE: q[0] = p->u.late.r, q[1] = p->u.late.a; q += 2; break;
    case ND_RESULT: q[0] = p->u.result.r, q[1] = p->u.result.a, q[2] = p->u.result.k; put16(q + 3, p->u.result.ms); q += 5; break;
    default: break;
    }
    return (size_t)(q - b);
}

bool nd_decode(const uint8_t *b, size_t len, nd_pkt_t *p)
{
    memset(p, 0, sizeof(*p));
    if (len < ND_HDR || b[0] != 'N' || b[1] != 'D') return false;
    p->ver = b[2];
    p->type = b[3];
    p->session = get32(b + 4);
    p->seq = get16(b + 8);
    p->ack = get16(b + 10);
    const uint8_t *q = b + ND_HDR;
    size_t n = len - ND_HDR;
    switch (p->type) {
    case ND_HELLO:
        if (n < ND_NAME_LEN + 1) return false;
        memcpy(p->u.hello.name, q, ND_NAME_LEN);
        p->u.hello.name[ND_NAME_LEN - 1] = 0;
        p->u.hello.busy = q[ND_NAME_LEN];
        break;
    case ND_INVITE:
        if (n < ND_NAME_LEN + 2) return false;
        memcpy(p->u.invite.name, q, ND_NAME_LEN);
        p->u.invite.name[ND_NAME_LEN - 1] = 0;
        p->u.invite.bo = q[ND_NAME_LEN];
        p->u.invite.fake = q[ND_NAME_LEN + 1];
        if (p->u.invite.bo != 1 && p->u.invite.bo != 3 && p->u.invite.bo != 5) return false;
        break;
    case ND_ACCEPT:
        if (n < ND_NAME_LEN) return false;
        memcpy(p->u.accept.name, q, ND_NAME_LEN);
        p->u.accept.name[ND_NAME_LEN - 1] = 0;
        break;
    case ND_DECLINE: case ND_BYE: if (n < 1) return false; p->u.why.why = q[0]; break;
    case ND_PING: case ND_PONG:
        if (n < 9) return false;
        p->u.ping.id = q[0], p->u.ping.t1 = get32(q + 1), p->u.ping.t2 = get32(q + 5);
        break;
    case ND_ROUND:
        if (n < 12) return false;
        p->u.round.r = q[0], p->u.round.a = q[1], p->u.round.sh = q[2], p->u.round.sg = q[3];
        p->u.round.seed = get32(q + 4), p->u.round.go = get32(q + 8);
        break;
    case ND_LATE: if (n < 2) return false; p->u.late.r = q[0], p->u.late.a = q[1]; break;
    case ND_RESULT:
        if (n < 5) return false;
        p->u.result.r = q[0], p->u.result.a = q[1], p->u.result.k = q[2], p->u.result.ms = get16(q + 3);
        if (p->u.result.k > ND_RES_FOUL) return false;
        break;
    case ND_NEXT: case ND_PAUSE_PKT: case ND_RESUME: case ND_BEAT: case ND_ACK: break;
    default: return false;
    }
    return true;
}
