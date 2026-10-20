#pragma once
// NABO DUEL rules and link protocol for one badge: a port of game-art/duel/prototype/duel_model.js.
// Pure logic: no display, audio, radio or RTOS. Time is the badge's own millisecond clock, passed
// in; packets to send go to an outbox, sound/animation cues to an event list. The same inputs give
// the same outputs as the prototype (test_duel.py replays recorded traces step by step).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ND_VER 1
#define ND_MAC_LEN 6
#define ND_NAME_LEN 10           // "NABO-3F2A" + NUL
#define ND_MAX_PEERS 8           // badges remembered in the lobby (4 are listed)
#define ND_LIST_PEERS 4
#define ND_MAX_CONNS 8
#define ND_MAX_PENDING 8
#define ND_SEEN 16
#define ND_OUTBOX 24
#define ND_EVENTS 16
#define ND_MAX_FAKES 2
#define ND_WIRE_MAX 40

// Timing (ms), identical to T in duel_model.js.
enum {
    ND_T_READY = 1500, ND_T_MIN_DELAY = 1000, ND_T_MAX_DELAY = 4000, ND_T_LEAD = 400, ND_T_LATE_MIN = 200,
    ND_T_TIMEOUT = 2000, ND_T_REVEAL = 3200, ND_T_HELLO = 300, ND_T_PEER_EXPIRE = 1500, ND_T_BEAT = 250,
    ND_T_DEAD = 3000, ND_T_RETX = 60, ND_T_GIVEUP = 2000, ND_T_PING_N = 8, ND_T_PING_GAP = 30,
    ND_T_PING_WAIT = 120, ND_T_LONG = 1000, ND_T_INVITE_WAIT = 10000, ND_T_LEFT_SHOW = 2200, ND_T_TOAST = 1800,
    ND_T_MAX_ATTEMPT = 3, ND_T_FAKE_SHOW = 450,
};

typedef enum { ND_KEY_UP, ND_KEY_DOWN, ND_KEY_OK } nd_key_t;

typedef enum {
    ND_COVER, ND_LOBBY, ND_INVITING, ND_INVITED, ND_SYNC, ND_DUEL, ND_PAUSE, ND_OPPPAUSE, ND_END, ND_LEFT,
    ND_SCENE_COUNT
} nd_scene_t;

typedef enum { ND_PH_WAIT, ND_PH_READY, ND_PH_STANDOFF, ND_PH_GO, ND_PH_REVEAL } nd_phase_t;
typedef enum { ND_COVER_ONLINE, ND_COVER_SOLO, ND_COVER_BO, ND_COVER_FAKE, ND_COVER_AI, ND_COVER_BACK, ND_COVER_COUNT } nd_cover_t;

// Packet types (wire byte values).
typedef enum {
    ND_HELLO = 1, ND_INVITE, ND_ACCEPT, ND_DECLINE, ND_PING, ND_PONG, ND_ROUND, ND_LATE, ND_RESULT, ND_NEXT,
    ND_PAUSE_PKT, ND_RESUME, ND_BEAT, ND_BYE, ND_ACK
} nd_type_t;

typedef enum { ND_RES_NONE, ND_RES_HIT, ND_RES_FOUL } nd_res_kind_t;   // "none" = did not fire
typedef enum { ND_WHY_NO, ND_WHY_BUSY, ND_WHY_CANCEL, ND_WHY_QUIT } nd_why_t;
typedef enum { ND_OUT_WIN, ND_OUT_LOSE, ND_OUT_REPLAY } nd_outcome_t;
typedef enum { ND_FAKE_TEXT, ND_FAKE_TWITCH, ND_FAKE_TUMBLE } nd_fake_kind_t;
typedef enum { ND_LEFT_LOST, ND_LEFT_QUIT, ND_LEFT_BYE } nd_left_t;
typedef enum {
    ND_TOAST_NONE, ND_TOAST_VERSION, ND_TOAST_PEER_BUSY, ND_TOAST_NO_ANSWER, ND_TOAST_DECLINED, ND_TOAST_BUSY,
    ND_TOAST_CANCELLED
} nd_toast_t;

// Sound / animation cues, in the order they happened.
typedef enum {
    ND_FX_TICK, ND_FX_INVITE, ND_FX_READY, ND_FX_STANDOFF, ND_FX_FAKE, ND_FX_GO, ND_FX_FOUL, ND_FX_SHOT,
    ND_FX_NONE, ND_FX_WIN, ND_FX_LOSE, ND_FX_REPLAY, ND_FX_PAUSE, ND_FX_LEFT
} nd_fx_kind_t;

typedef struct {
    uint8_t type, ver;
    uint32_t session;
    uint16_t seq, ack;               // seq 0: not a reliable message
    union {
        struct { char name[ND_NAME_LEN]; uint8_t busy; } hello;
        struct { char name[ND_NAME_LEN]; uint8_t bo, fake; } invite;
        struct { char name[ND_NAME_LEN]; } accept;
        struct { uint8_t why; } why;  // DECLINE, BYE
        struct { uint8_t id; uint32_t t1, t2; } ping;   // PING carries t1, PONG t1 and t2
        struct { uint8_t r, a, sh, sg; uint32_t seed, go; } round;
        struct { uint8_t r, a; } late;
        struct { uint8_t r, a, k; uint16_t ms; } result;
    } u;
} nd_pkt_t;

typedef struct { uint8_t to[ND_MAC_LEN]; bool broadcast; nd_pkt_t pkt; } nd_out_t;
typedef struct { uint32_t t; uint8_t kind, arg; } nd_fx_t;   // arg: fake kind for ND_FX_FAKE

typedef struct { uint8_t k; uint16_t r; } nd_res_t;   // k: nd_res_kind_t, r: reaction ms for a hit

typedef struct {
    uint8_t mac[ND_MAC_LEN];
    char name[ND_NAME_LEN];
    bool busy;
    uint8_t ver;
    int8_t rssi;
    uint32_t seen;
} nd_peer_t;

typedef struct { nd_pkt_t pkt; uint32_t first, last; uint16_t tries; } nd_pending_t;

typedef struct {
    bool used;
    uint8_t mac[ND_MAC_LEN];
    uint16_t tx_seq;
    nd_pending_t pending[ND_MAX_PENDING];
    uint8_t pending_n;
    uint16_t seen[ND_SEEN];
    uint8_t seen_n;
} nd_conn_t;

typedef struct {
    uint8_t mac[ND_MAC_LEN];
    char name[ND_NAME_LEN];
    uint32_t session, at;
    uint8_t bo;
    bool fake;
} nd_invite_t;

typedef struct {
    bool active;
    uint8_t mac[ND_MAC_LEN];
    char name[ND_NAME_LEN];
    uint32_t session, last_rx, last_beat;
    bool host;
} nd_link_t;

typedef struct { uint32_t at; uint8_t kind; } nd_fake_t;

typedef struct {
    uint32_t seed, go_at, delay, standoff, ready;   // ready / standoff / go_at are local times
    nd_fake_t fakes[ND_MAX_FAKES];
    uint8_t fake_n, fake_idx;
    uint8_t round, attempt;
    bool has_me, has_opp, has_lock_at, has_go_shown, resolved, ready_fx, stand_fx, new_best;
    nd_res_t me, opp;
    uint32_t lock_at, go_shown, resolved_at;
    uint8_t outcome;
    bool ai_foul_set, ai_r_set;   // practice: NABO's own shot, from the seed
    uint32_t ai_foul;
    uint16_t ai_r;
} nd_attempt_t;

typedef struct { uint16_t mean, sd, foul_permille; } nd_ai_t;   // practice NABO: 新手 / 老手 / 神枪
extern const nd_ai_t ND_AI[3];

typedef struct {
    bool solo, fake, host, done, want_next, opp_next;
    uint8_t bo, need, me, opp, round, attempt, ai;
    char opp_name[ND_NAME_LEN];
    int32_t offset;                 // guest clock - host clock, ms
    bool has_rtt;
    uint32_t rtt;
    // clock sync (host)
    bool pinging, sync_done, has_best, has_issue_at, has_reveal_until;
    uint32_t ping_start, sync_done_at, issue_at, reveal_until;
    uint8_t ping_sent;
    int32_t best_off;
    uint32_t best_rtt;
    bool has_last;
    nd_attempt_t last;              // the latest resolved attempt (shown until the next one starts)
} nd_match_t;

typedef struct {
    uint8_t bo, ai;
    bool fake;
} nd_settings_t;

typedef struct {
    uint8_t mac[ND_MAC_LEN];
    char name[ND_NAME_LEN];
    uint32_t rng;
    nd_settings_t set;
    uint16_t best;                  // fastest hit, ms (0 = none)
    bool suppress;                  // scripted tests: never touch the record
    uint32_t t;
    nd_scene_t scene;
    uint32_t scene_at;
    uint8_t sel;
    nd_out_t out[ND_OUTBOX];
    uint8_t out_n;
    bool out_overflow;
    nd_fx_t fx[ND_EVENTS];
    uint8_t fx_n;
    nd_conn_t conns[ND_MAX_CONNS];
    nd_peer_t peers[ND_MAX_PEERS];
    uint8_t peer_n;
    bool hello_valid;
    uint32_t hello_at;
    nd_link_t link;
    bool has_match, has_cur;
    nd_match_t match;
    nd_attempt_t cur;
    bool hold[2];
    uint32_t hold_at[2];
    nd_toast_t toast;
    uint32_t toast_until;
    nd_invite_t invite, invited;
    nd_left_t left_why;
    bool exit_req;                  // "back to the table" chosen on the cover
} nd_t;

void nd_init(nd_t *d, const uint8_t mac[ND_MAC_LEN], uint32_t entropy, const nd_settings_t *set, uint16_t best,
             bool suppress, uint32_t now);
void nd_key(nd_t *d, uint32_t t, nd_key_t k, bool down);
void nd_recv(nd_t *d, uint32_t t, const uint8_t from[ND_MAC_LEN], const nd_pkt_t *pkt, int rssi);
void nd_tick(nd_t *d, uint32_t t);

nd_phase_t nd_phase(const nd_t *d, uint32_t t);
// Lobby rows: up to ND_LIST_PEERS peers (busy last, strongest first), then "back". Returns the
// number of peers written (the row after them is "back").
int nd_lobby_list(const nd_t *d, const nd_peer_t *rows[ND_LIST_PEERS]);
nd_outcome_t nd_judge(nd_res_t me, nd_res_t opp);
// Start-of-attempt plan from the seed (also used by the renderer to know the stand-off length).
void nd_plan(uint32_t seed, uint32_t go_at, int round, bool fake_on, nd_attempt_t *out);
uint32_t nd_mulberry32(uint32_t *state);

// Wire format: 13-byte header ('N' 'D' ver type session seq ack) + payload. Returns bytes written.
size_t nd_encode(const nd_pkt_t *p, uint8_t *buf, size_t cap);
bool nd_decode(const uint8_t *buf, size_t len, nd_pkt_t *out);

#ifdef __cplusplus
}
#endif
