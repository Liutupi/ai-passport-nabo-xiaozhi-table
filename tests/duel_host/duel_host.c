// Host shim for test_duel.py: several NABO DUEL models side by side, driven with wire-format
// packets, so the tests also exercise nd_encode / nd_decode.
#include <string.h>

#include "nabo_duel_model.h"

#define HOST_BADGES 4

static nd_t s_badge[HOST_BADGES];
static uint8_t s_out[ND_OUTBOX][ND_WIRE_MAX];
static int s_out_len[ND_OUTBOX], s_out_to[ND_OUTBOX];

static void mac16(uint8_t mac[ND_MAC_LEN], int v)
{
    memset(mac, 0, ND_MAC_LEN);
    mac[4] = (uint8_t)(v >> 8);
    mac[5] = (uint8_t)v;
}

int host_sizeof(void) { return (int)sizeof(nd_t); }

void host_new(int i, int mac, unsigned entropy, int bo, int fake, int ai, int best, int suppress)
{
    uint8_t m[ND_MAC_LEN];
    mac16(m, mac);
    nd_settings_t set = {(uint8_t)bo, (uint8_t)ai, fake != 0};
    nd_init(&s_badge[i], m, entropy, &set, (uint16_t)best, suppress != 0, 0);
}

void host_key(int i, unsigned t, int k, int down) { nd_key(&s_badge[i], t, (nd_key_t)k, down != 0); }
void host_tick(int i, unsigned t) { nd_tick(&s_badge[i], t); }

// Returns 0 when the bytes do not decode (the packet is then dropped, as the radio would).
int host_recv(int i, unsigned t, int from, const uint8_t *wire, int len, int rssi)
{
    nd_pkt_t p;
    if (!nd_decode(wire, (size_t)len, &p)) return 0;
    uint8_t m[ND_MAC_LEN];
    mac16(m, from);
    nd_recv(&s_badge[i], t, m, &p, rssi);
    return 1;
}

// Moves the outbox into encoded buffers; returns how many packets there were (-1: overflow).
int host_take_out(int i)
{
    nd_t *d = &s_badge[i];
    int n = d->out_n;
    for (int k = 0; k < n; k++) {
        s_out_len[k] = (int)nd_encode(&d->out[k].pkt, s_out[k], ND_WIRE_MAX);
        s_out_to[k] = d->out[k].broadcast ? -1 : (d->out[k].to[4] << 8 | d->out[k].to[5]);
    }
    d->out_n = 0;
    if (d->out_overflow) { d->out_overflow = false; return -1; }
    return n;
}

int host_out(int k, uint8_t *buf)
{
    memcpy(buf, s_out[k], (size_t)s_out_len[k]);
    return s_out_len[k];
}

int host_out_to(int k) { return s_out_to[k]; }

int host_take_fx(int i, unsigned *t, int *kind, int *arg, int cap)
{
    nd_t *d = &s_badge[i];
    int n = d->fx_n < cap ? d->fx_n : cap;
    for (int k = 0; k < n; k++) t[k] = d->fx[k].t, kind[k] = d->fx[k].kind, arg[k] = d->fx[k].arg;
    d->fx_n = 0;
    return n;
}

// scene sel phase | has_match me opp round attempt done host | has_cur me_has me_k me_r opp_has opp_k opp_r
// resolved go_at outcome | toast best | peers
void host_snap(int i, unsigned t, long long *o)
{
    const nd_t *d = &s_badge[i];
    const nd_match_t *m = &d->match;
    const nd_attempt_t *c = &d->cur;
    long long v[] = {d->scene, d->sel, d->scene == ND_DUEL ? (long long)nd_phase(d, t) : -1LL,
                     d->has_match, m->me, m->opp, m->round, m->attempt, m->done, m->host,
                     d->has_cur, c->has_me, c->me.k, c->me.r, c->has_opp, c->opp.k, c->opp.r,
                     c->resolved, c->go_at, c->resolved ? c->outcome : -1,
                     d->toast, d->best, d->peer_n};
    memcpy(o, v, sizeof(v));
}

int host_judge(int mk, int mr, int ok, int orr)
{
    return nd_judge((nd_res_t){(uint8_t)mk, (uint16_t)mr}, (nd_res_t){(uint8_t)ok, (uint16_t)orr});
}

// Lobby row r: peer MAC (low 16 bits), or -1 for the "back" row.
int host_lobby_row(int i, int r)
{
    const nd_peer_t *rows[ND_LIST_PEERS];
    int n = nd_lobby_list(&s_badge[i], rows);
    if (r >= n) return -1;
    return rows[r]->mac[4] << 8 | rows[r]->mac[5];
}

void host_set_sel(int i, int sel) { s_badge[i].sel = (uint8_t)sel; }
