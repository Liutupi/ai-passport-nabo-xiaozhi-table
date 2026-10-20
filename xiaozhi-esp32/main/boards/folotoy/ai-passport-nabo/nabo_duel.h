#pragma once
// NABO DUEL runtime glue between the pure model (nabo_duel_model.c) and the badge: orders
// time-stamped key edges and received packets with the model's millisecond ticks, holds the
// draw back until the screen has really flipped, and turns the model's cues into sound bits.
// No ESP-IDF here, so the host tests drive it too; nabo_table.c does the radio, panel and timer.
//
// The model runs NDU_LAG_MS behind the wall clock. Key edges are stamped in the button task and
// reach the game a few ms later, packets are stamped in the radio callback: running slightly
// behind lets every input go in at its own time, in order.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nabo_duel_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NDU_LAG_MS 30
#define NDU_INPUTS 24
#define NDU_MAX_STEPS 400   // ticks per advance; a longer gap jumps

// Sound cues (bits), taken by the audio worker.
enum {
    NDU_CUE_TICK = 1 << 0, NDU_CUE_INVITE = 1 << 1, NDU_CUE_READY = 1 << 2, NDU_CUE_WIND = 1 << 3,
    NDU_CUE_HEART = 1 << 4, NDU_CUE_FAKE = 1 << 5, NDU_CUE_GO = 1 << 6, NDU_CUE_SHOT = 1 << 7,
    NDU_CUE_FOUL = 1 << 8, NDU_CUE_WIN = 1 << 9, NDU_CUE_LOSE = 1 << 10, NDU_CUE_REPLAY = 1 << 11,
    NDU_CUE_LEFT = 1 << 12,
};

typedef struct {
    uint32_t t;
    bool is_key;
    uint8_t key, down;
    uint8_t mac[ND_MAC_LEN];
    int8_t rssi;
    nd_pkt_t pkt;
} ndu_input_t;

typedef struct {
    nd_t m;
    uint32_t ticked;              // model time reached
    ndu_input_t in[NDU_INPUTS];   // pending inputs, sorted by time
    uint8_t in_n;
    bool in_dropped;
    // The draw: the model may not pass go_at until the panel has flipped (ndu_flipped).
    uint32_t gate_seed, gate_go;  // attempt the flip belongs to
    bool flipped;
    uint32_t flip_t;
    uint32_t cues;
    uint32_t wind_ms;             // stand-off length, for the wind cue
    uint32_t heart_at;
    uint8_t fake_kind;            // last fake cue (the renderer shows it from the plan anyway)
} nabo_duel_t;

void ndu_init(nabo_duel_t *u, const uint8_t mac[ND_MAC_LEN], uint32_t entropy, const nd_settings_t *set,
              uint16_t best, uint32_t now);
void ndu_key(nabo_duel_t *u, nd_key_t k, bool down, uint32_t t);
void ndu_rx(nabo_duel_t *u, const uint8_t mac[ND_MAC_LEN], const nd_pkt_t *p, int rssi, uint32_t t);
// Runs the model up to now - NDU_LAG_MS. Packets to send are left in u->m.out.
void ndu_advance(nabo_duel_t *u, uint32_t now);
// The current attempt's draw time if the panel still has to flip for it.
bool ndu_go_pending(const nabo_duel_t *u, uint32_t *go_at);
// The platform inverted the panel at t (>= go_at): reaction times count from here.
void ndu_flipped(nabo_duel_t *u, uint32_t t);
// Whether the panel should show the draw (inverted) right now.
bool ndu_inverted(const nabo_duel_t *u);
uint32_t ndu_take_cues(nabo_duel_t *u);

// Clock sync runs in the radio layer, not on frame time: the host's PING carries the moment it
// really left (ndu_stamp_ping, just before sending), and the guest answers in the receive path
// with the moment it arrived (ndu_ping_reply), so neither side's frame or model lag skews it.
void ndu_stamp_ping(uint8_t *wire, size_t len, uint32_t now);
// For a PING of this session, writes the PONG into out and returns its length (0: not a PING to answer).
size_t ndu_ping_reply(const uint8_t *wire, size_t len, uint32_t rx_t, uint32_t session, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
